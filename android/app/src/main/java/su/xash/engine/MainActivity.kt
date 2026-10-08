package su.xash.engine

import android.content.ActivityNotFoundException
import android.content.Context
import android.content.Intent
import android.os.Bundle
import android.provider.Settings
import android.view.LayoutInflater
import android.widget.TextView
import androidx.appcompat.app.AlertDialog
import androidx.appcompat.app.AppCompatActivity
import androidx.core.net.toUri
import androidx.lifecycle.lifecycleScope
import androidx.navigation.NavController
import androidx.navigation.fragment.NavHostFragment
import androidx.navigation.ui.AppBarConfiguration
import androidx.navigation.ui.navigateUp
import androidx.navigation.ui.setupActionBarWithNavController
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import kotlinx.coroutines.launch
import su.xash.engine.databinding.ActivityMainBinding
import su.xash.engine.model.AppUpdater
import su.xash.engine.util.CrashReports
import su.xash.engine.util.dialogContentView
import su.xash.engine.util.monospaceTextView
import su.xash.engine.util.showDownloadProgressDialog
import java.io.File
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

class MainActivity : AppCompatActivity() {
	private lateinit var binding: ActivityMainBinding
	private lateinit var appBarConfiguration: AppBarConfiguration
	private lateinit var navController: NavController

	override fun onCreate(savedInstanceState: Bundle?) {
		super.onCreate(savedInstanceState)

		binding = ActivityMainBinding.inflate(layoutInflater)
		setContentView(binding.root)

		setSupportActionBar(binding.toolbar)

		val navHostFragment =
			supportFragmentManager.findFragmentById(R.id.fragmentContainerView) as NavHostFragment
		navController = navHostFragment.navController
		appBarConfiguration = AppBarConfiguration(navController.graph)
		setupActionBarWithNavController(navController, appBarConfiguration)

		CrashReports.prune(this)
		showPendingCrashReport()

		checkForEngineUpdate()
	}

	private fun checkForEngineUpdate() {
		val prefs = getSharedPreferences(UPDATE_PREFS, Context.MODE_PRIVATE)
		val now = System.currentTimeMillis()
		if (now - prefs.getLong(KEY_LAST_CHECK, 0L) < CHECK_INTERVAL_MS)
			return

		val updater = AppUpdater(this)
		lifecycleScope.launch {
			val result = updater.checkForUpdate()
			prefs.edit().putLong(KEY_LAST_CHECK, now).apply()
			if (result !is AppUpdater.UpdateCheck.Available)
				return@launch
			val info = result.info
			if (prefs.getInt(KEY_DISMISSED_BUILDNUM, -1) >= info.buildNum)
				return@launch
			val changelog = updater.fetchChangelog(BuildConfig.GIT_HASH, info.tagName)
			showEngineUpdateDialog(updater, info.buildNum, changelog, prefs)
		}
	}

	// Manual check from Settings: bypasses the interval and "later" gates and
	// always reports back, so the user gets feedback ("up to date" / failure).
	fun checkForUpdatesManually() {
		val view = LayoutInflater.from(this).inflate(R.layout.dialog_download_progress, null)
		view.findViewById<TextView>(R.id.downloadStatus).text = getString(R.string.engine_update_checking)
		val dialog = MaterialAlertDialogBuilder(this)
			.setTitle(R.string.engine_update_checking)
			.setView(view)
			.setCancelable(true)
			.setNegativeButton(android.R.string.cancel) { d, _ -> d.dismiss() }
			.create()
		dialog.show()

		val updater = AppUpdater(this)
		val job = lifecycleScope.launch {
			val result = updater.checkForUpdate()
			if (!dialog.isShowing)
				return@launch
			dialog.dismiss()
			when (result) {
				is AppUpdater.UpdateCheck.Available -> {
					val prefs = getSharedPreferences(UPDATE_PREFS, Context.MODE_PRIVATE)
					prefs.edit().putLong(KEY_LAST_CHECK, System.currentTimeMillis()).apply()
					val changelog = updater.fetchChangelog(BuildConfig.GIT_HASH, result.info.tagName)
					showEngineUpdateDialog(updater, result.info.buildNum, changelog, prefs)
				}
				is AppUpdater.UpdateCheck.UpToDate,
				is AppUpdater.UpdateCheck.Disabled ->
					MaterialAlertDialogBuilder(this)
						.setTitle(R.string.check_updates)
						.setMessage(R.string.engine_update_up_to_date)
						.setPositiveButton(android.R.string.ok, null)
						.show()
				is AppUpdater.UpdateCheck.Failed ->
					MaterialAlertDialogBuilder(this)
						.setTitle(R.string.check_updates)
						.setMessage(R.string.engine_update_check_failed)
						.setPositiveButton(android.R.string.ok, null)
						.show()
			}
		}
		dialog.setOnDismissListener { job.cancel() }
	}

	private fun showEngineUpdateDialog(
		updater: AppUpdater,
		remoteBuildNum: Int,
		changelog: List<AppUpdater.CommitInfo>?,
		prefs: android.content.SharedPreferences,
	) {
		val changelogText = changelog?.takeIf { it.isNotEmpty() }?.let { commits ->
			buildString {
				append(getString(R.string.engine_update_changelog_header))
				val shown = commits.take(CHANGELOG_MAX_LINES)
				for (c in shown)
					append("\n* ").append(c.subject)
				val extra = commits.size - shown.size
				if (extra > 0)
					append("\n").append(getString(R.string.engine_update_changelog_more, extra))
			}
		}

		MaterialAlertDialogBuilder(this)
			.setTitle(R.string.engine_update_available)
			// Message and changelog share one custom view: with both setMessage()
			// and setView() the dialog layout stops prioritizing the buttons, and
			// a long changelog pushes them off screen. As a single view the
			// buttons are measured first and the changelog shrinks and scrolls.
			.setView(dialogContentView(
				this,
				getString(R.string.engine_update_message, remoteBuildNum),
				changelogText,
			))
			.setPositiveButton(R.string.engine_update_download) { _, _ ->
				showEngineDownloadDialog(updater)
			}
			.setNegativeButton(R.string.engine_update_later) { _, _ ->
				prefs.edit().putInt(KEY_DISMISSED_BUILDNUM, remoteBuildNum).apply()
			}
			.show()
	}

	private fun showEngineDownloadDialog(updater: AppUpdater) {
		if (!updater.canInstall()) {
			promptForInstallPermission()
			return
		}
		showDownloadProgressDialog(
			ctx = this,
			titleRes = R.string.engine_update_downloading,
			cancelable = true,
			scope = lifecycleScope,
			download = { onProgress -> updater.downloadAndInstall(onProgress) },
		)
	}

	private fun promptForInstallPermission() {
		MaterialAlertDialogBuilder(this)
			.setTitle(R.string.engine_update_permission_needed)
			.setMessage(R.string.engine_update_permission_message)
			.setPositiveButton(R.string.engine_update_open_settings) { _, _ ->
				val packageIntent = Intent(Settings.ACTION_MANAGE_UNKNOWN_APP_SOURCES,
					"package:$packageName".toUri())
				try {
					startActivity(packageIntent)
				} catch (_: ActivityNotFoundException) {
					try {
						startActivity(Intent(Settings.ACTION_MANAGE_UNKNOWN_APP_SOURCES))
					} catch (_: ActivityNotFoundException) {
						// no settings screen — nothing more we can do
					}
				}
			}
			.setNegativeButton(android.R.string.cancel, null)
			.show()
	}

	override fun onSupportNavigateUp(): Boolean {
		return navController.navigateUp(appBarConfiguration) || super.onSupportNavigateUp()
	}

	private fun showPendingCrashReport() {
		val pending = CrashReports.pendingStacktrace(this)
		if (!pending.exists() || pending.length() == 0L)
			return

		val historyDir = CrashReports.historyDir(this).apply { mkdirs() }
		val ts = SimpleDateFormat("yyyyMMdd-HHmmss", Locale.US).format(Date())
		val entryDir = File(historyDir, "crash-$ts").apply { mkdirs() }

		moveOrCopy(pending, File(entryDir, CrashReports.STACKTRACE_NAME))
		moveOrCopy(CrashReports.pendingSysinfo(this), File(entryDir, CrashReports.SYSINFO_NAME))
		moveOrCopy(CrashReports.pendingIntent(this), File(entryDir, CrashReports.INTENT_NAME))
		moveOrCopy(CrashReports.pendingEngineLog(this), File(entryDir, CrashReports.ENGINELOG_NAME))

		val entry = CrashReports.Entry(entryDir)
		AlertDialog.Builder(this)
			.setTitle(R.string.crash_dialog_title)
			.setView(monospaceTextView(this, entry.summary()))
			.setPositiveButton(R.string.crash_send_to_developers) { _, _ -> CrashReports.sendByEmail(this, entry) }
			.setNeutralButton(R.string.crash_share) { _, _ -> CrashReports.share(this, entry) }
			.setNegativeButton(R.string.crash_dismiss, null)
			.show()
	}

	private fun moveOrCopy(src: File, dst: File) {
		if (!src.exists())
			return

		if (src.renameTo(dst))
			return

		src.copyTo(dst, overwrite = true)
		src.delete()
	}

	companion object {
		private const val CHANGELOG_MAX_LINES = 15
		private const val UPDATE_PREFS = "app_updater"
		private const val KEY_LAST_CHECK = "last_check_ms"
		private const val KEY_DISMISSED_BUILDNUM = "dismissed_buildnum"
		private const val CHECK_INTERVAL_MS = 24 * 60 * 60 * 1000L
	}
}
