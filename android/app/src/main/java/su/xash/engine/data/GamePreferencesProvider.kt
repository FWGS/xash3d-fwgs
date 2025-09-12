package su.xash.engine.data

import android.content.Context
import androidx.datastore.core.DataStore
import androidx.datastore.preferences.core.PreferenceDataStoreFactory
import androidx.datastore.preferences.core.Preferences
import androidx.datastore.preferences.preferencesDataStoreFile
import dagger.hilt.android.qualifiers.ApplicationContext
import java.util.concurrent.ConcurrentHashMap
import javax.inject.Inject
import javax.inject.Singleton

@Singleton
class GamePreferencesProvider @Inject constructor(
	@param:ApplicationContext private val context: Context
) {
	private val dataStoreMap = ConcurrentHashMap<String, DataStore<Preferences>>()

	fun provide(gameDir: String): DataStore<Preferences> {
		return dataStoreMap.getOrPut(gameDir) {
			PreferenceDataStoreFactory.create {
				context.preferencesDataStoreFile("preferences_$gameDir")
			}
		}
	}
}