package su.xash.engine

import android.os.Bundle
import androidx.appcompat.app.AlertDialog
import androidx.appcompat.app.AppCompatActivity
import androidx.navigation.NavController
import androidx.navigation.fragment.NavHostFragment
import androidx.navigation.ui.AppBarConfiguration
import androidx.navigation.ui.navigateUp
import androidx.navigation.ui.setupActionBarWithNavController
import su.xash.engine.databinding.ActivityMainBinding
import su.xash.engine.util.CrashReports
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

		val entry = CrashReports.Entry(entryDir)
		AlertDialog.Builder(this)
			.setTitle(R.string.crash_dialog_title)
			.setView(CrashReports.buildContentView(this, entry.summary()))
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

		dst.writeText(src.readText())
		src.delete()
	}
}
