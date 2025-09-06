package su.xash.engine.data

import android.content.Context
import android.os.Environment
import androidx.datastore.preferences.core.edit
import androidx.datastore.preferences.core.stringPreferencesKey
import dagger.hilt.android.qualifiers.ApplicationContext
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.map
import su.xash.engine.dataStore
import javax.inject.Inject
import javax.inject.Singleton

data class AppPreferences(
	val baseDirectory: String
)

@Singleton
class AppPreferencesRepository @Inject constructor(
	@param:ApplicationContext private val context: Context,
	applicationScope: CoroutineScope
) {
	companion object {
		private val BASE_DIRECTORY = stringPreferencesKey("base_dir")
		private val DEFAULT_BASE_DIRECTORY =
			Environment.getExternalStorageDirectory().resolve("xash").path
	}

	val baseDirectory: Flow<String> = context.dataStore.data.map { preferences ->
		preferences[BASE_DIRECTORY] ?: DEFAULT_BASE_DIRECTORY
	}

	suspend fun setBaseDirectory(baseDir: String) {
		context.dataStore.edit { preferences ->
			preferences[BASE_DIRECTORY] = baseDir
		}
	}
}