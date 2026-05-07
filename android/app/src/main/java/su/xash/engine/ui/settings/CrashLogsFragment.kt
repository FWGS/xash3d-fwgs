package su.xash.engine.ui.settings

import android.os.Bundle
import androidx.appcompat.app.AlertDialog
import androidx.preference.Preference
import androidx.preference.PreferenceFragmentCompat
import su.xash.engine.R
import su.xash.engine.util.CrashReports
import java.io.File
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

		val files = CrashReports.historyDir(ctx).listFiles()?.sortedByDescending { it.lastModified() } ?: emptyList()

		if (files.isEmpty()) {
			preferenceScreen.addPreference(Preference(ctx).apply {
				setTitle(R.string.crash_logs_empty)
				isSelectable = false
			})
			return
		}

		val fmt = SimpleDateFormat("yyyy-MM-dd HH:mm:ss", Locale.US)
		files.forEach { file ->
			preferenceScreen.addPreference(Preference(ctx).apply {
				title = fmt.format(Date(file.lastModified()))
				summary = file.name
				setOnPreferenceClickListener {
					showCrashLog(file)
					true
				}
			})
		}
	}

	private fun showCrashLog(file: File) {
		val ctx = requireContext()
		val content = file.readText()
		AlertDialog.Builder(ctx)
			.setTitle(file.name)
			.setView(CrashReports.buildContentView(ctx, content))
			.setPositiveButton(R.string.crash_send_to_developers) { _, _ -> CrashReports.sendByEmail(ctx, content) }
			.setNeutralButton(R.string.crash_share) { _, _ -> CrashReports.share(ctx, file) }
			.setNegativeButton(R.string.crash_log_delete) { _, _ ->
				file.delete()
				populate()
			}
			.show()
	}
}
