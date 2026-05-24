package su.xash.engine.model

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Context
import android.content.Intent
import android.os.Build
import android.os.IBinder
import androidx.core.app.NotificationCompat
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import su.xash.engine.MainActivity
import java.io.File

class DownloadService : Service() {

    private val scope = CoroutineScope(Dispatchers.IO + SupervisorJob())
    private var downloadJob: Job? = null
    private val downloader by lazy { GameDataDownloader(this) }
    private var lastProgress = 0
    private var downloadTargetDir: File? = null
    private var completedGamedir: String? = null
    private var displayName: String? = null

    override fun onCreate() {
        super.onCreate()
        createNotificationChannel()
        startForeground(NOTIFICATION_ID, buildNotification(0, "Starting..."))
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        when (intent?.action) {
            ACTION_START -> {
                val gamedir = intent.getStringExtra(EXTRA_GAMEDIR) ?: return START_NOT_STICKY
                val targetPath = intent.getStringExtra(EXTRA_TARGET) ?: return START_NOT_STICKY
                val depotIds = intent.getIntegerArrayListExtra(EXTRA_DEPOT_IDS)?.filterNotNull()
                startDownload(gamedir, File(targetPath), depotIds)
            }
            ACTION_CANCEL -> cancelDownload()
            ACTION_STOP -> stopSelf()
        }
        return START_NOT_STICKY
    }

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onDestroy() {
        scope.cancel()
        super.onDestroy()
    }

    private fun startDownload(gamedir: String, targetDir: File, depotIds: List<Int>?) {
        downloadJob?.cancel()
        completedGamedir = gamedir
        displayName = GameDataDownloader.GAMES[gamedir]?.displayName ?: gamedir
        downloadTargetDir = targetDir
        _state.value = DownloadState.Downloading(0, 0, "Starting...")
        downloadJob = scope.launch {
            try {
                val result = downloader.downloadGame(
                    gamedir, targetDir,
                    onProgress = { current, total ->
                        val pct = if (total > 0) ((current * 100) / total).toInt() else 0
                        lastProgress = pct
                        _state.value = DownloadState.Downloading(current, total, "$current / $total files")
                        updateNotification(pct, "$current / $total files")
                    },
                    onStatus = { status ->
                        _state.value = DownloadState.Downloading(0, 0, status)
                        updateNotification(lastProgress, status)
                    },
                    depotIds = depotIds
                )

                if (result.isSuccess) {
                    lastCompletedGamedir = gamedir
                    _state.value = DownloadState.Success
                    showCompletionNotification(true, null)
                } else {
                    val err = result.exceptionOrNull()?.message ?: "Unknown error"
                    _state.value = DownloadState.Error(err)
                    showCompletionNotification(false, err)
                }
            } catch (e: CancellationException) {
                _state.value = DownloadState.Cancelled
            } catch (e: Exception) {
                _state.value = DownloadState.Error(e.message ?: "Unknown error")
                showCompletionNotification(false, e.message)
            } finally {
                cleanupTmpFiles()
                stopForeground(STOP_FOREGROUND_REMOVE)
                stopSelf()
            }
        }
    }

    private fun cancelDownload() {
        downloadJob?.cancel()
        downloadJob = null
        cleanupTmpFiles()
        _state.value = DownloadState.Cancelled
    }

    private fun cleanupTmpFiles() {
        val dir = downloadTargetDir ?: return
        dir.walkTopDown().forEach { file ->
            if (file.isFile && file.name.endsWith(".tmp")) {
                file.delete()
            }
        }
    }

    private fun createNotificationChannel() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            val channel = NotificationChannel(
                CHANNEL_ID, "Game Data Download",
                NotificationManager.IMPORTANCE_LOW
            ).apply { setShowBadge(false) }
            val channelDone = NotificationChannel(
                CHANNEL_ID_COMPLETION, "Download Complete",
                NotificationManager.IMPORTANCE_DEFAULT
            ).apply { setShowBadge(true) }
            val nm = getSystemService(NotificationManager::class.java)
            nm.createNotificationChannel(channel)
            nm.createNotificationChannel(channelDone)
        }
    }

    private fun buildNotification(progress: Int, text: String): Notification {
        val openIntent = Intent(this, MainActivity::class.java).apply {
            flags = Intent.FLAG_ACTIVITY_SINGLE_TOP
        }
        val flags = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M)
            PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT
        else
            PendingIntent.FLAG_UPDATE_CURRENT

        val openPend = PendingIntent.getActivity(this, 0, openIntent, flags)

        val cancelIntent = Intent(this, DownloadService::class.java).apply { action = ACTION_CANCEL }
        val cancelPend = PendingIntent.getService(this, 1, cancelIntent, flags)

        return NotificationCompat.Builder(this, CHANNEL_ID)
            .setContentTitle("Downloading Game Data")
            .setContentText(text)
            .setSmallIcon(android.R.drawable.stat_sys_download)
            .setContentIntent(openPend)
            .addAction(android.R.drawable.ic_menu_close_clear_cancel, "Cancel", cancelPend)
            .setOngoing(true)
            .apply {
                if (progress > 0) {
                    setProgress(100, progress, false)
                } else {
                    setProgress(0, 0, true)
                }
            }
            .build()
    }

    private fun nm() = getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager

    private fun updateNotification(progress: Int, text: String) {
        nm().notify(NOTIFICATION_ID, buildNotification(progress, text))
    }

    private fun showCompletionNotification(success: Boolean, error: String?) {
        val name = displayName ?: "Game"
        val title = if (success) "$name Installed!" else "Download Failed"
        val text = if (success) "Game data downloaded successfully" else error ?: "Unknown error"
        val icon = if (success) android.R.drawable.stat_sys_download_done else android.R.drawable.stat_notify_error

        val openIntent = Intent(this, MainActivity::class.java).apply {
            flags = Intent.FLAG_ACTIVITY_SINGLE_TOP
        }
        val flags = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M)
            PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT
        else
            PendingIntent.FLAG_UPDATE_CURRENT
        val openPend = PendingIntent.getActivity(this, 0, openIntent, flags)

        val notification = NotificationCompat.Builder(this, CHANNEL_ID_COMPLETION)
            .setContentTitle(title)
            .setContentText(text)
            .setSmallIcon(icon)
            .setContentIntent(openPend)
            .setAutoCancel(true)
            .build()

        nm().notify(COMPLETION_NOTIFICATION_ID, notification)
    }

    companion object {
        const val CHANNEL_ID = "game_data_download"
        const val CHANNEL_ID_COMPLETION = "game_data_download_complete"
        const val NOTIFICATION_ID = 1001
        const val COMPLETION_NOTIFICATION_ID = 1002
        const val ACTION_START = "su.xash.engine.action.START_DOWNLOAD"
        const val ACTION_CANCEL = "su.xash.engine.action.CANCEL_DOWNLOAD"
        const val ACTION_STOP = "su.xash.engine.action.STOP_DOWNLOAD"
        const val EXTRA_GAMEDIR = "gamedir"
        const val EXTRA_TARGET = "target"
        const val EXTRA_DEPOT_IDS = "depot_ids"

        private val _state = MutableStateFlow<DownloadState>(DownloadState.Idle)
        val state: StateFlow<DownloadState> = _state.asStateFlow()

        var lastCompletedGamedir: String? = null
            private set

        fun consumeCompletion(): String? {
            val v = lastCompletedGamedir
            lastCompletedGamedir = null
            return v
        }

        fun resetState() {
            _state.value = DownloadState.Idle
            lastCompletedGamedir = null
        }

        fun start(context: Context, gamedir: String, targetDir: File, depotIds: List<Int>?) {
            _state.value = DownloadState.Idle
            val intent = Intent(context, DownloadService::class.java).apply {
                action = ACTION_START
                putExtra(EXTRA_GAMEDIR, gamedir)
                putExtra(EXTRA_TARGET, targetDir.absolutePath)
                if (depotIds != null) {
                    putIntegerArrayListExtra(EXTRA_DEPOT_IDS, ArrayList(depotIds))
                }
            }
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                context.startForegroundService(intent)
            } else {
                context.startService(intent)
            }
        }

        fun cancel(context: Context) {
            val intent = Intent(context, DownloadService::class.java).apply { action = ACTION_CANCEL }
            context.startService(intent)
        }
    }
}

sealed class DownloadState {
    object Idle : DownloadState()
    data class Downloading(val current: Long, val total: Long, val status: String) : DownloadState()
    object Success : DownloadState()
    data class Error(val message: String) : DownloadState()
    object Cancelled : DownloadState()
}
