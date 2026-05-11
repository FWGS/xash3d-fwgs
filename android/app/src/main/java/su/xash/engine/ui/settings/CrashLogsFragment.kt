package su.xash.engine.ui.settings

import android.os.Bundle
import androidx.appcompat.app.AlertDialog
import androidx.preference.Preference
import androidx.preference.PreferenceFragmentCompat
import su.xash.engine.R
import su.xash.engine.util.CrashReports
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

class CrashLogsFragment : PreferenceFragmentCompat() {
	override fun onCreatePreferences(savedInstanceState: Bundle?, rootKey: String?) {
		preferenceScreen = preferenceManager.createPreferenceScreen(requireContext())
		populate()
	}

	override fun onResume() {
		super.onResume()
		populate()
	}

	private fun populate() {
		val ctx = requireContext()
		preferenceScreen.removeAll()

		val dirs = CrashReports.historyDir(ctx).listFiles()
			?.filter { it.isDirectory }
			?.sortedByDescending { it.lastModified() }
			?: emptyList()

		if (dirs.isEmpty()) {
			preferenceScreen.addPreference(Preference(ctx).apply {
				setTitle(R.string.crash_logs_empty)
				isSelectable = false
			})
			return
		}

		val fmt = SimpleDateFormat("yyyy-MM-dd HH:mm:ss", Locale.US)
		dirs.forEach { dir ->
			preferenceScreen.addPreference(Preference(ctx).apply {
				title = fmt.format(Date(dir.lastModified()))
				summary = dir.name
				setOnPreferenceClickListener {
					showCrashLog(CrashReports.Entry(dir))
					true
				}
			})
		}
	}

	private fun showCrashLog(entry: CrashReports.Entry) {
		val ctx = requireContext()
		AlertDialog.Builder(ctx)
			.setTitle(entry.name)
			.setView(CrashReports.buildContentView(ctx, entry.summary()))
			.setPositiveButton(R.string.crash_send_to_developers) { _, _ -> CrashReports.sendByEmail(ctx, entry) }
			.setNeutralButton(R.string.crash_share) { _, _ -> CrashReports.share(ctx, entry) }
			.setNegativeButton(R.string.crash_log_delete) { _, _ ->
				entry.dir.deleteRecursively()
				populate()
			}
			.show()
	}
}
