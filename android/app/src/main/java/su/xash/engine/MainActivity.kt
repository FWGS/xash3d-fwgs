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
		val pending = CrashReports.pendingFile(this)
		if (!pending.exists() || pending.length() == 0L)
			return

		val historyDir = CrashReports.historyDir(this).apply { mkdirs() }
		val ts = SimpleDateFormat("yyyyMMdd-HHmmss", Locale.US).format(Date())
		val archived = File(historyDir, "crash-$ts.log")
		if (!pending.renameTo(archived)) {
			// fall back to in-place read if move failed; still consume the file
			archived.writeText(pending.readText())
			pending.delete()
		}

		val content = archived.readText()
		AlertDialog.Builder(this)
			.setTitle(R.string.crash_dialog_title)
			.setView(CrashReports.buildContentView(this, content))
			.setPositiveButton(R.string.crash_send_to_developers) { _, _ -> CrashReports.sendByEmail(this, content) }
			.setNeutralButton(R.string.crash_share) { _, _ -> CrashReports.share(this, archived) }
			.setNegativeButton(R.string.crash_dismiss, null)
			.show()
	}
}
