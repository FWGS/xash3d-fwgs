package su.xash.engine.ui.downloader

import android.content.Intent
import android.content.SharedPreferences
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.os.Environment
import android.preference.PreferenceManager
import android.provider.Settings
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.Button
import android.widget.TextView
import androidx.fragment.app.Fragment
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.lifecycleScope
import androidx.lifecycle.repeatOnLifecycle
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import com.google.android.material.progressindicator.LinearProgressIndicator
import kotlinx.coroutines.launch
import su.xash.engine.R
import su.xash.engine.model.DownloadService
import su.xash.engine.model.DownloadState
import su.xash.engine.model.GameDataDownloader
import java.io.File

class GameDataDownloaderFragment : Fragment() {

    private val downloader by lazy { GameDataDownloader(requireContext()) }
    private var statusText: TextView? = null
    private var progressBar: LinearProgressIndicator? = null
    private var progressContainer: View? = null
    private var cancelButton: View? = null
    private val gameButtons = mutableMapOf<String, Button>()
    private var lastTerminalState: DownloadState? = null

    override fun onCreateView(
        inflater: LayoutInflater, container: ViewGroup?, savedInstanceState: Bundle?
    ): View {
        return inflater.inflate(R.layout.fragment_game_data_downloader, container, false)
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        super.onViewCreated(view, savedInstanceState)

        statusText = view.findViewById(R.id.downloaderStatus)
        progressBar = view.findViewById(R.id.downloaderProgress)
        progressContainer = view.findViewById(R.id.downloadProgressContainer)
        cancelButton = view.findViewById(R.id.cancelButton)
        cancelButton?.setOnClickListener { cancelDownload() }

        observeDownloadState()

        val gameList = view.findViewById<ViewGroup>(R.id.gameList)

        var idx = 0
        for ((gamedir, info) in GameDataDownloader.GAMES) {
            val gameView = layoutInflater.inflate(R.layout.item_downloadable_game, gameList, false)
            gameView.findViewById<TextView>(R.id.gameName).text = info.displayName
            gameView.findViewById<TextView>(R.id.gameSize).text =
                getString(R.string.game_data_size, GameDataDownloader.formatSize(info.estimatedSize))

            val downloadBtn = gameView.findViewById<Button>(R.id.downloadButton)
            downloadBtn.setText(
                if (checkGameExists(gamedir)) R.string.overwrite else R.string.download_now
            )
            gameButtons[gamedir] = downloadBtn

            downloadBtn.setOnClickListener {
                if (DownloadService.state.value !is DownloadState.Downloading) {
                    onGameSelected(gamedir, info.displayName)
                }
            }

            gameList.addView(gameView, idx)
            idx++
        }
    }

    private fun checkGameExists(gamedir: String): Boolean {
        val basedir = getBaseDir()
        return downloader.hasGamedir(basedir, gamedir)
    }

    private fun getBaseDir(): File {
        val prefs = PreferenceManager.getDefaultSharedPreferences(requireContext())
        val path = prefs.getString("game_path", null)
            ?: "${Environment.getExternalStorageDirectory().absolutePath}/xash"
        return File(path)
    }

    private fun onGameSelected(gamedir: String, displayName: String) {
        val basedir = getBaseDir()
        val targetDir = File(basedir, gamedir)

        // Permission guard for Android 11+
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R && !Environment.isExternalStorageManager()) {
            MaterialAlertDialogBuilder(requireContext())
                .setTitle(R.string.file_access_required)
                .setMessage(R.string.file_access_message)
                .setPositiveButton(R.string.open_settings) { _, _ ->
                    startActivity(Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION).setData(
                        Uri.fromParts("package", requireContext().packageName, null)
                    ))
                }
                .setNegativeButton(R.string.cancel, null)
                .show()
            return
        }

        // Dependency checks for mods
        val valveDir = File(basedir, "valve")
        val cstrikeDir = File(basedir, "cstrike")
        val valveExists = valveDir.isDirectory && valveDir.listFiles()?.isNotEmpty() == true
        val cstrikeExists = cstrikeDir.isDirectory && cstrikeDir.listFiles()?.isNotEmpty() == true
        val depsValid = when (gamedir) {
            "valve" -> true
            "cstrike", "tfc", "gearbox" -> {
                if (!valveExists) {
                    MaterialAlertDialogBuilder(requireContext())
                        .setTitle(R.string.download_failed)
                        .setMessage(R.string.dependency_half_life_required)
                        .setPositiveButton(android.R.string.ok, null)
                        .show()
                    false
                } else true
            }
            "czero" -> {
                when {
                    !valveExists && !cstrikeExists -> {
                        MaterialAlertDialogBuilder(requireContext())
                            .setTitle(R.string.download_failed)
                            .setMessage(R.string.dependency_hl_cs_required)
                            .setPositiveButton(android.R.string.ok, null)
                            .show()
                        false
                    }
                    valveExists && !cstrikeExists -> {
                        MaterialAlertDialogBuilder(requireContext())
                            .setTitle(R.string.download_failed)
                            .setMessage(R.string.dependency_cs_required)
                            .setPositiveButton(android.R.string.ok, null)
                            .show()
                        false
                    }
                    !valveExists && cstrikeExists -> {
                        MaterialAlertDialogBuilder(requireContext())
                            .setTitle(R.string.download_failed)
                            .setMessage(R.string.dependency_half_life_required)
                            .setPositiveButton(android.R.string.ok, null)
                            .show()
                        false
                    }
                    else -> true
                }
            }
            else -> true
        }
        if (!depsValid) return

        if (targetDir.isDirectory && targetDir.listFiles()?.isNotEmpty() == true) {
            MaterialAlertDialogBuilder(requireContext())
                .setTitle(R.string.overwrite_confirm_title)
                .setMessage(getString(R.string.overwrite_confirm_message, displayName))
                .setPositiveButton(R.string.overwrite) { _, _ -> startDownload(gamedir, targetDir, displayName) }
                .setNegativeButton(R.string.keep, null)
                .show()
        } else {
            startDownload(gamedir, targetDir, displayName)
        }
    }

    private fun observeDownloadState() {
        lifecycleScope.launch {
            repeatOnLifecycle(Lifecycle.State.STARTED) {
                DownloadService.state.collect { state ->
                    when (state) {
                        is DownloadState.Idle -> {
                            lastTerminalState = null
                            if (progressContainer?.visibility == View.VISIBLE) {
                                progressContainer?.visibility = View.GONE
                                setButtonsEnabled(true)
                            }
                        }
                        is DownloadState.Downloading -> {
                            lastTerminalState = null
                            progressContainer?.visibility = View.VISIBLE
                            setButtonsEnabled(false)
                            if (state.total > 0) {
                                progressBar?.isIndeterminate = false
                                progressBar?.progress = ((state.current * 100) / state.total).toInt()
                            }
                            statusText?.text = state.status
                        }
                        is DownloadState.Success -> {
                            if (lastTerminalState != state) {
                                lastTerminalState = state
                                progressContainer?.visibility = View.GONE
                                setButtonsEnabled(true)
                                val gamedir = DownloadService.consumeCompletion()
                                if (gamedir != null) {
                                    gameButtons[gamedir]?.setText(
                                        if (checkGameExists(gamedir)) R.string.overwrite else R.string.download_now
                                    )
                                }
                                MaterialAlertDialogBuilder(requireContext())
                                    .setTitle(R.string.download_complete)
                                    .setPositiveButton(android.R.string.ok) { _, _ ->
                                        DownloadService.resetState()
                                    }
                                    .show()
                            }
                        }
                        is DownloadState.Error -> {
                            if (lastTerminalState != state) {
                                lastTerminalState = state
                                progressContainer?.visibility = View.GONE
                                setButtonsEnabled(true)
                                val errorMsg = state.message
                                MaterialAlertDialogBuilder(requireContext())
                                    .setTitle(R.string.download_failed)
                                    .setMessage(errorMsg)
                                    .setPositiveButton(android.R.string.ok) { _, _ ->
                                        DownloadService.resetState()
                                    }
                                    .setNeutralButton("Copy") { _, _ ->
                                        val clipboard = requireContext().getSystemService(android.content.Context.CLIPBOARD_SERVICE) as android.content.ClipboardManager
                                        val clip = android.content.ClipData.newPlainText("Error", errorMsg)
                                        clipboard.setPrimaryClip(clip)
                                    }
                                    .show()
                            }
                        }
                        is DownloadState.Cancelled -> {
                            if (lastTerminalState != state) {
                                lastTerminalState = state
                                statusText?.text = getString(R.string.download_cancelled)
                                progressContainer?.postDelayed({
                                    progressContainer?.visibility = View.GONE
                                    setButtonsEnabled(true)
                                    DownloadService.resetState()
                                }, 1500)
                            }
                        }
                    }
                }
            }
        }
    }

    private fun cancelDownload() {
        DownloadService.cancel(requireContext())
    }

    private fun setButtonsEnabled(enabled: Boolean) {
        for ((_, btn) in gameButtons) {
            btn.isEnabled = enabled
        }
    }

    private fun startDownload(gamedir: String, targetDir: File, displayName: String) {
        progressContainer?.visibility = View.VISIBLE
        progressBar?.isIndeterminate = true
        statusText?.text = getString(R.string.downloading_steam_data, displayName)
        setButtonsEnabled(false)

        val prefs = requireContext().getSharedPreferences("depot_settings", 0)
        val info = GameDataDownloader.GAMES[gamedir] ?: return
        val enabledDepots = info.depotIds.filter { depotId ->
            prefs.getBoolean("${gamedir}_depot_$depotId", depotId == 1)
        }

        DownloadService.start(requireContext(), gamedir, targetDir, enabledDepots)
    }
}
