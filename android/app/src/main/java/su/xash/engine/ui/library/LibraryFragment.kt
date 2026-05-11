package su.xash.engine.ui.library

import android.Manifest
import android.content.Intent
import android.content.pm.PackageManager
import android.content.SharedPreferences
import android.preference.PreferenceManager
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.os.Environment
import android.provider.Settings
import android.view.LayoutInflater
import android.view.Menu
import android.view.MenuInflater
import android.view.MenuItem
import android.view.View
import android.view.ViewGroup
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AlertDialog
import androidx.core.app.ActivityCompat
import androidx.core.content.ContextCompat
import androidx.core.view.MenuProvider
import androidx.fragment.app.Fragment
import androidx.fragment.app.activityViewModels
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.lifecycleScope
import androidx.navigation.fragment.findNavController
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import kotlinx.coroutines.launch
import su.xash.engine.BuildConfig
import su.xash.engine.R
import su.xash.engine.adapters.GameAdapter
import su.xash.engine.databinding.FragmentLibraryBinding
import su.xash.engine.model.GameLibDownloader

class LibraryFragment : Fragment(), MenuProvider {
    private var _binding: FragmentLibraryBinding? = null
    private val binding get() = _binding!!

    private val libraryViewModel: LibraryViewModel by activityViewModels()
    private lateinit var preferences: SharedPreferences

    private val startActivityForResult =
        registerForActivityResult(ActivityResultContracts.StartActivityForResult()) {
            if (checkStoragePermissions()) {
                libraryViewModel.reloadGames(requireContext())
            }
        }

    private val requiredPermissions = arrayOf(
        Manifest.permission.READ_EXTERNAL_STORAGE,
        Manifest.permission.WRITE_EXTERNAL_STORAGE
    )

    private val requestPermissionLauncher = registerForActivityResult(
        ActivityResultContracts.RequestMultiplePermissions()
    ) { permissions ->
        val granted = permissions.entries.all { it.value }
        if (granted) {
            libraryViewModel.reloadGames(requireContext())
        } else {
            checkStoragePermissions()
        }
    }

    private fun checkStoragePermissions(): Boolean {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            if (!Environment.isExternalStorageManager()) {
                MaterialAlertDialogBuilder(requireContext()).apply {
                    setTitle(R.string.file_access_required)
                    setMessage(R.string.file_access_message)
                    setPositiveButton(R.string.open_settings) { _, _ ->
                        startActivityForResult.launch(
                            Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION).setData(
                                Uri.fromParts("package", BuildConfig.APPLICATION_ID, null)
                            )
                        )
                    }

		    setNeutralButton(R.string.done_check_permissions) { dialog, _ ->
		    checkStoragePermissions()
		    }

                    setCancelable(false)
                    show()

                    return false
		    }
                } else {
                return true
            }
        } else if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
            val permissionsNeeded = requiredPermissions.filter {
                ContextCompat.checkSelfPermission(
                    requireContext(),
                    it
                ) != PackageManager.PERMISSION_GRANTED
            }.toTypedArray()

            if (!permissionsNeeded.isEmpty()) {
                val showRationale = permissionsNeeded.any {
                    ActivityCompat.shouldShowRequestPermissionRationale(requireActivity(), it)
                }

                MaterialAlertDialogBuilder(requireContext()).apply {
                    setTitle(R.string.external_storage_required)
                    setMessage(R.string.external_storage_message)
                    setPositiveButton(R.string.open_settings) { _, _ ->
                        if (showRationale) {
                            requestPermissionLauncher.launch(permissionsNeeded)
                        } else {
                            val intent =
                                Intent(Settings.ACTION_APPLICATION_DETAILS_SETTINGS).apply {
                                    data =
                                        Uri.fromParts("package", requireContext().packageName, null)
                                }
                            startActivity(intent)
                        }
                    }

		    setNeutralButton(R.string.done_check_permissions) { _, _ ->
		    checkStoragePermissions()
		    }

                    setCancelable(false)
                    show()
                }

                return false
            } else {
                return true
            }
        } else {
            return true
        }
    }

    override fun onCreateView(
        inflater: LayoutInflater, container: ViewGroup?, savedInstanceState: Bundle?
    ): View {
        _binding = FragmentLibraryBinding.inflate(inflater, container, false)
        
        // Preferences'ı yükle
        preferences = PreferenceManager.getDefaultSharedPreferences(requireContext())

        val adapter = GameAdapter(
            onLaunchGame = { game -> onGameLaunch(game) },
            onSettingsClick = { game ->
                libraryViewModel.setSelectedGame(game)
                findNavController().navigate(R.id.action_libraryFragment_to_gameSettingsFragment)
            }
        )
        binding.gamesList.adapter = adapter

        requireActivity().addMenuProvider(this, viewLifecycleOwner, Lifecycle.State.RESUMED)

        return binding.root
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        binding.swipeRefresh.setOnRefreshListener { 
            libraryViewModel.reloadGames(requireContext())
        }

        libraryViewModel.isReloading.observe(viewLifecycleOwner) {
            binding.swipeRefresh.isRefreshing = it
        }

        libraryViewModel.installedGames.observe(viewLifecycleOwner) {
            (binding.gamesList.adapter as GameAdapter).submitList(it)
        }

        if (checkStoragePermissions()) {
            libraryViewModel.reloadGames(requireContext())
        }
    }

    override fun onDestroyView() {
        super.onDestroyView()
        _binding = null
    }

    override fun onCreateMenu(menu: Menu, menuInflater: MenuInflater) {
        menuInflater.inflate(R.menu.menu_library, menu)
    }

    override fun onMenuItemSelected(menuItem: MenuItem): Boolean {
        when (menuItem.itemId) {
            R.id.action_settings -> {
                findNavController().navigate(R.id.action_libraryFragment_to_appSettingsFragment)
            }
        }

        return false
    }

    private fun onGameLaunch(game: su.xash.engine.model.Game) {
        val ctx = requireContext()
        if (!preferences.getBoolean("enable_gamelib_downloader", false)) {
            libraryViewModel.startEngine(ctx, game)
            return
        }

        val downloader = GameLibDownloader(ctx)
        if (downloader.isDownloaded(game.basedir.name)) {
            lifecycleScope.launch {
                if (downloader.isUpdateAvailable(game.basedir.name)) {
                    AlertDialog.Builder(ctx)
                        .setTitle(R.string.update_available)
                        .setMessage(R.string.update_available_message)
                        .setPositiveButton(R.string.update_download) { _, _ ->
                            performDownloadAndLaunch(game, downloader)
                        }
                        .setNegativeButton(R.string.update_skip) { _, _ ->
                            libraryViewModel.startEngine(ctx, game)
                        }
                        .show()
                } else {
                    libraryViewModel.startEngine(ctx, game)
                }
            }
            return
        }

        lifecycleScope.launch {
            val lookup = downloader.lookupBuild(game.basedir.name)
            when (lookup) {
                is GameLibDownloader.Lookup.Available -> {
                    AlertDialog.Builder(ctx)
                        .setTitle(R.string.downloading_game_libs)
                        .setMessage(ctx.getString(R.string.game_apk_message))
                        .setPositiveButton(R.string.game_apk_install) { _, _ ->
                            performDownloadAndLaunch(game, downloader)
                        }
                        .setNegativeButton(android.R.string.cancel, null)
                        .show()
                }
                is GameLibDownloader.Lookup.NotInManifest -> {
                    libraryViewModel.startEngine(ctx, game)
                }
                is GameLibDownloader.Lookup.Error -> {
                    AlertDialog.Builder(ctx)
                        .setTitle(R.string.manifest_error_title)
                        .setMessage(ctx.getString(R.string.manifest_error_message, lookup.cause.message))
                        .setPositiveButton(R.string.launch_anyway) { _, _ ->
                            libraryViewModel.startEngine(ctx, game)
                        }
                        .setNegativeButton(android.R.string.cancel, null)
                        .show()
                }
            }
        }
    }

    private fun performDownloadAndLaunch(game: su.xash.engine.model.Game, downloader: GameLibDownloader) {
        val ctx = requireContext()
        val dialogView = LayoutInflater.from(ctx).inflate(R.layout.dialog_download_progress, null)
        val dialog = AlertDialog.Builder(ctx)
            .setTitle(R.string.downloading_game_libs)
            .setView(dialogView)
            .setCancelable(false)
            .create()
        dialog.show()

        lifecycleScope.launch {
            val result = downloader.download(game.basedir.name) { progress ->
                dialogView.findViewById<com.google.android.material.progressindicator.LinearProgressIndicator>(
                    R.id.downloadProgress
                )?.setProgress((progress * 100).toInt())
                dialogView.findViewById<com.google.android.material.textview.MaterialTextView>(
                    R.id.downloadStatus
                )?.text = ctx.getString(R.string.download_progress, (progress * 100).toInt())
            }
            dialog.dismiss()
            result.onSuccess {
                libraryViewModel.startEngine(ctx, game)
            }.onFailure { error ->
                AlertDialog.Builder(ctx)
                    .setTitle(R.string.download_failed)
                    .setMessage(ctx.getString(R.string.download_error) + "\n\n" + error.message)
                    .setPositiveButton(R.string.launch_anyway) { _, _ ->
                        libraryViewModel.startEngine(ctx, game)
                    }
                    .setNegativeButton(android.R.string.cancel, null)
                    .show()
            }
        }
    }

    override fun onResume() {
        super.onResume()

        if (checkStoragePermissions()) {
            libraryViewModel.reloadGames(requireContext())
        }
    }
}
