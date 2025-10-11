package su.xash.engine.data

import androidx.datastore.core.DataStore
import androidx.datastore.preferences.core.Preferences
import androidx.datastore.preferences.core.booleanPreferencesKey
import androidx.datastore.preferences.core.edit
import androidx.datastore.preferences.core.stringPreferencesKey
import dagger.hilt.android.scopes.ViewModelScoped
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.map
import javax.inject.Inject

data class GamePreferences(
	val arguments: String = "-console -log",
	val useVolumeButtons: Boolean = false
)

@ViewModelScoped
class GamePreferencesRepository (
	private val dataStore: DataStore<Preferences>
) {
	companion object {
		private val ARGUMENTS = stringPreferencesKey("args")
		private const val DEFAULT_ARGUMENTS = "-console -log"

		private val USE_VOLUME_BUTTONS = booleanPreferencesKey("use_volume_buttons")
	}

	val preferences: Flow<GamePreferences> = dataStore.data.map { preferences ->
		GamePreferences(
			arguments = preferences[ARGUMENTS] ?: DEFAULT_ARGUMENTS,
			useVolumeButtons = preferences[USE_VOLUME_BUTTONS] ?: false
		)
	}

	suspend fun setArguments(args: String) {
		dataStore.edit { preferences ->
			preferences[ARGUMENTS] = args
		}
	}

	suspend fun setUseVolumeButtons(useVolumeButtons: Boolean) {
		dataStore.edit { preferences ->
			preferences[USE_VOLUME_BUTTONS] = useVolumeButtons
		}
	}
}