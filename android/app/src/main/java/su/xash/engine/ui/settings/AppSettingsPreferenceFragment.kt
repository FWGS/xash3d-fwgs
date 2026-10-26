package su.xash.engine.ui.settings

import android.os.Bundle
import androidx.navigation.fragment.findNavController
import androidx.preference.Preference
import androidx.preference.PreferenceFragmentCompat
import su.xash.engine.BuildConfig
import su.xash.engine.MainActivity
import su.xash.engine.R

class AppSettingsPreferenceFragment() : PreferenceFragmentCompat() {
	override fun onCreatePreferences(savedInstanceState: Bundle?, rootKey: String?) {
		preferenceManager.sharedPreferencesName = "app_preferences";
		setPreferencesFromResource(R.xml.app_preferences, rootKey);

		findPreference<Preference>("crash_logs")?.setOnPreferenceClickListener {
			findNavController().navigate(R.id.action_appSettingsFragment_to_crashLogsFragment)
			true
		}

		findPreference<Preference>("check_updates")?.apply {
			// Auto-update is only compiled into the continuous build; hide the
			// manual check elsewhere, since the updater is a no-op there.
			isVisible = BuildConfig.ENABLE_AUTO_UPDATE
			setOnPreferenceClickListener {
				(requireActivity() as MainActivity).checkForUpdatesManually()
				true
			}
		}
	}
}
