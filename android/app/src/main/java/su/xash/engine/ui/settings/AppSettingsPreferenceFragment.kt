package su.xash.engine.ui.settings

import android.content.ActivityNotFoundException
import android.content.Context
import android.net.Uri
import android.os.Bundle
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AlertDialog
import androidx.lifecycle.lifecycleScope
import androidx.navigation.fragment.findNavController
import androidx.preference.Preference
import androidx.preference.PreferenceFragmentCompat
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import su.xash.engine.BuildConfig
import su.xash.engine.MainActivity
import su.xash.engine.R
import su.xash.engine.model.Game
import su.xash.engine.util.GameDataDirectory
import su.xash.engine.util.GameDataDirectory.Result
import su.xash.engine.util.Prefs
import java.io.File

class AppSettingsPreferenceFragment() : PreferenceFragmentCompat() {
	private var gameDirectoryJob: Job? = null
	private var gameDirectoryRequestId = 0
	// Only one error or confirmation dialog can be active at a time
	private var activeDialog: AlertDialog? = null

	private val appPreferences
		get() = requireContext().getSharedPreferences(Prefs.APP_PREFERENCES, Context.MODE_PRIVATE)

	private val gameDirectoryPicker = registerForActivityResult(
		ActivityResultContracts.OpenDocumentTree()
	) { uri ->
		if (uri == null)
			return@registerForActivityResult

		handlePickedGameDirectory(uri)
	}

	override fun onCreatePreferences(savedInstanceState: Bundle?, rootKey: String?) {
		preferenceManager.sharedPreferencesName = Prefs.APP_PREFERENCES;
		setPreferencesFromResource(R.xml.app_preferences, rootKey);

		findPreference<GamePathPreference>(Prefs.GAME_PATH)?.apply {
			setOnPreferenceClickListener {
				cancelPendingGameDirectorySelection()
				try {
					gameDirectoryPicker.launch(null)
				} catch (e: ActivityNotFoundException) {
					// Some devices have no document picker
					showGameDirectoryError(R.string.game_data_location_picker_unavailable)
				}
				true
			}
			onResetClickListener = { resetGamePath() }
		}
		updateGamePathPreference()

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

	private fun handlePickedGameDirectory(uri: Uri) {
		cancelPendingGameDirectorySelection()
		val requestId = gameDirectoryRequestId
		val context = requireContext()

		// Resolving the URI and scanning the picked folder for game
		// directories hits the filesystem and can be slow on large folders,
		// so keep it off the main thread.
		gameDirectoryJob = lifecycleScope.launch {
			val result = withContext(Dispatchers.IO) {
				GameDataDirectory.resolve(context, uri)
			}
			if (requestId != gameDirectoryRequestId) return@launch

			val success = when (result) {
				is Result.Success -> result
				is Result.Failure -> {
					showGameDirectoryError(result.messageRes, *result.formatArgs.toTypedArray())
					return@launch
				}
			}

			val directory = success.directory

			// The library expects a root containing game directories, not a single game's directory
			val isSingleGame = withContext(Dispatchers.IO) {
				Game.checkIfGamedir(directory) != null
			}
			if (requestId != gameDirectoryRequestId) return@launch
			if (isSingleGame) {
				showGameDirectoryError(R.string.game_data_location_select_parent)
				return@launch
			}

			val hasGames = withContext(Dispatchers.IO) { hasGameDirectories(directory) }
			if (requestId != gameDirectoryRequestId) return@launch

			when (hasGames) {
				null -> showGameDirectoryError(R.string.game_data_location_unreadable)
				true -> saveGamePath(directory, requestId)
				false -> confirmGamePathWithoutGames(directory, requestId)
			}
		}
	}

	// Returns null if the directory cannot be listed
	private fun hasGameDirectories(directory: File): Boolean? = try {
		directory.listFiles()?.any { it.isDirectory && Game.checkIfGamedir(it) != null }
	} catch (e: SecurityException) {
		null
	}

	private fun cancelPendingGameDirectorySelection() {
		gameDirectoryRequestId++
		gameDirectoryJob?.cancel()
		gameDirectoryJob = null
		activeDialog?.dismiss()
		activeDialog = null
	}

	private fun saveGamePath(directory: File, requestId: Int) {
		if (requestId != gameDirectoryRequestId) return

		// Selecting the default directory is equivalent to resetting the preference
		if (Prefs.isDefaultGamePath(directory)) {
			appPreferences.edit().remove(Prefs.GAME_PATH).apply()
		} else {
			appPreferences.edit().putString(Prefs.GAME_PATH, directory.absolutePath).apply()
		}

		updateGamePathPreference()
	}

	private fun resetGamePath() {
		cancelPendingGameDirectorySelection()
		appPreferences.edit().remove(Prefs.GAME_PATH).apply()
		updateGamePathPreference()
	}

	private fun updateGamePathPreference() {
		val gamePath = appPreferences.getString(Prefs.GAME_PATH, null)
		findPreference<GamePathPreference>(Prefs.GAME_PATH)?.apply {
			summary = gamePath ?: Prefs.defaultGamePath.absolutePath
			isResetEnabled = gamePath != null
		}
	}

	private fun confirmGamePathWithoutGames(directory: File, requestId: Int) {
		if (requestId != gameDirectoryRequestId) return
		showDialog(
			MaterialAlertDialogBuilder(requireContext())
				.setTitle(R.string.game_data_location_empty_title)
				.setMessage(R.string.game_data_location_empty_message)
				.setPositiveButton(R.string.game_data_location_use_anyway) { _, _ ->
					saveGamePath(directory, requestId)
				}
				.setNegativeButton(android.R.string.cancel, null)
		)
	}

	override fun onDestroyView() {
		cancelPendingGameDirectorySelection()
		super.onDestroyView()
	}

	private fun showGameDirectoryError(messageRes: Int, vararg formatArgs: Any) {
		val message = if (formatArgs.isEmpty()) {
			getString(messageRes)
		} else {
			getString(messageRes, *formatArgs)
		}

		showDialog(
			MaterialAlertDialogBuilder(requireContext())
				.setTitle(R.string.game_data_location)
				.setMessage(message)
				.setPositiveButton(android.R.string.ok, null)
		)
	}

	// Track the dialog so onDestroyView() can dismiss it
	private fun showDialog(builder: MaterialAlertDialogBuilder) {
		activeDialog?.dismiss()

		val dialog = builder.create()
		dialog.setOnDismissListener {
			if (activeDialog === dialog) activeDialog = null
		}
		activeDialog = dialog
		dialog.show()
	}
}
