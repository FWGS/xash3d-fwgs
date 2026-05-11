package su.xash.engine.ui.settings

import android.os.Bundle
import androidx.preference.ListPreference
import androidx.preference.PreferenceFragmentCompat
import androidx.preference.SwitchPreferenceCompat
import su.xash.engine.R
import su.xash.engine.model.Game

class GameSettingsPreferenceFragment(val game: Game) : PreferenceFragmentCompat() {
	override fun onCreatePreferences(savedInstanceState: Bundle?, rootKey: String?) {
		preferenceManager.sharedPreferencesName = game.basedir.name;
		setPreferencesFromResource(R.xml.game_preferences, rootKey);

		val packageList = findPreference<ListPreference>("package_name")!!
		packageList.entries = arrayOf(getString(R.string.app_name))
		packageList.entryValues = arrayOf(requireContext().packageName)

		if (packageList.value == null) {
			packageList.setValueIndex(0);
		}

		val separatePackages = findPreference<SwitchPreferenceCompat>("separate_libraries")!!
		val clientPackage = findPreference<ListPreference>("client_package")!!
		val serverPackage = findPreference<ListPreference>("server_package")!!
		separatePackages.setOnPreferenceChangeListener { _, newValue ->
			if (newValue == true) {
				packageList.isVisible = false
				clientPackage.isVisible = true
				serverPackage.isVisible = true
			} else {
				packageList.isVisible = true
				clientPackage.isVisible = false
				serverPackage.isVisible = false
			}

			true
		}
	}
}
