package su.xash.engine.ui.settings

import android.app.AlertDialog
import android.os.Bundle
import android.widget.EditText
import androidx.preference.Preference
import androidx.preference.PreferenceFragmentCompat
import su.xash.engine.MainActivity
import su.xash.engine.R
import android.content.SharedPreferences
import android.preference.PreferenceManager

class AppSettingsPreferenceFragment() : PreferenceFragmentCompat(),
    SharedPreferences.OnSharedPreferenceChangeListener {

    private lateinit var preferences: SharedPreferences
    private lateinit var gamePathPreference: Preference
    private lateinit var globalArgsPreference: Preference
    private lateinit var renderResolutionPreference: Preference

    override fun onCreatePreferences(savedInstanceState: Bundle?, rootKey: String?) {
        preferenceManager.sharedPreferencesName = "app_preferences"
        setPreferencesFromResource(R.xml.app_preferences, rootKey)

        preferences = PreferenceManager.getDefaultSharedPreferences(requireContext())
        preferences.registerOnSharedPreferenceChangeListener(this)

        gamePathPreference = findPreference("game_path") ?: return
        globalArgsPreference = findPreference("global_arguments") ?: return
        renderResolutionPreference = findPreference("render_resolution") ?: return

        globalArgsPreference.setOnPreferenceClickListener {
            showGlobalArgumentsDialog()
            true
        }

        renderResolutionPreference.setOnPreferenceClickListener {
            showRenderResolutionDialog()
            true
        }

        updateGamePathSummary()
        updateGlobalArgsSummary()
        updateRenderResolutionSummary()
    }

    override fun onSharedPreferenceChanged(sharedPreferences: SharedPreferences, key: String?) {
        when (key) {
            "storage_toggle" -> {
                updateGamePathSummary()
            }
            "global_arguments" -> {
                updateGlobalArgsSummary()
            }
            "render_resolution" -> {
                updateRenderResolutionSummary()
            }
        }
    }

    private fun updateGamePathSummary() {
        (activity as? MainActivity)?.let { mainActivity ->
            gamePathPreference.summary = mainActivity.getStorageSummary()
        } ?: run {
            val useInternalStorage = preferences.getBoolean("storage_toggle", false)
            gamePathPreference.summary = if (useInternalStorage) {
                "Internal Storage (Android/data)"
            } else {
                "External Storage (/storage/emulated/0/xash)"
            }
        }
    }

    private fun updateGlobalArgsSummary() {
        val globalArgs = preferences.getString("global_arguments", "")
        if (globalArgs.isNullOrEmpty()) {
            globalArgsPreference.summary = "No global arguments set"
        } else {
            globalArgsPreference.summary = globalArgs
        }
    }

    private fun updateRenderResolutionSummary() {
        val resolution = preferences.getString("render_resolution", "")
        if (resolution.isNullOrEmpty()) {
            renderResolutionPreference.summary = getString(R.string.render_resolution_summary)
        } else {
            renderResolutionPreference.summary = resolution
        }
    }

    private fun showGlobalArgumentsDialog() {
        val currentArgs = preferences.getString("global_arguments", "") ?: ""
        
        val editText = EditText(requireContext())
        editText.setText(currentArgs)
        editText.hint = "e.g., -dev -log"
        
        AlertDialog.Builder(requireContext())
            .setTitle("Global Command-line Arguments")
            .setMessage("These arguments will be added to all games")
            .setView(editText)
            .setPositiveButton("OK") { dialog, which ->
                val newArgs = editText.text.toString().trim()
                preferences.edit().putString("global_arguments", newArgs).commit()
                updateGlobalArgsSummary()
            }
            .setNegativeButton("Cancel", null)
            .setNeutralButton("Clear") { dialog, which ->
                preferences.edit().putString("global_arguments", "").commit()
                updateGlobalArgsSummary()
            }
            .show()
    }

    private fun showRenderResolutionDialog() {
        val currentResolution = preferences.getString("render_resolution", "") ?: ""

        val editText = EditText(requireContext())
        editText.setText(currentResolution)
        editText.hint = "e.g., 640x480"

        AlertDialog.Builder(requireContext())
            .setTitle(R.string.render_resolution_dialog)
            .setMessage("Leave empty to use native display resolution")
            .setView(editText)
            .setPositiveButton("OK") { dialog, which ->
                val newResolution = editText.text.toString().trim()
                preferences.edit().putString("render_resolution", newResolution).commit()
                updateRenderResolutionSummary()
            }
            .setNegativeButton("Cancel", null)
            .setNeutralButton("Clear") { dialog, which ->
                preferences.edit().putString("render_resolution", "").commit()
                updateRenderResolutionSummary()
            }
            .show()
    }

    override fun onResume() {
        super.onResume()
        preferences.registerOnSharedPreferenceChangeListener(this)
        updateGamePathSummary()
        updateGlobalArgsSummary()
        updateRenderResolutionSummary()
    }

    override fun onPause() {
        super.onPause()
        preferences.unregisterOnSharedPreferenceChangeListener(this)
    }
}
