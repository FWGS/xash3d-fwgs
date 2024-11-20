package su.xash.engine.ui.settings

import android.os.Bundle
import androidx.preference.ListPreference
import androidx.preference.PreferenceFragmentCompat
import androidx.preference.SwitchPreferenceCompat
import su.xash.engine.BuildConfig
import su.xash.engine.R
import su.xash.engine.model.Game

class AppSettingsPreferenceFragment() : PreferenceFragmentCompat() {
    override fun onCreatePreferences(savedInstanceState: Bundle?, rootKey: String?) {
        preferenceManager.sharedPreferencesName = "app_preferences";
        setPreferencesFromResource(R.xml.app_preferences, rootKey);
    }
}