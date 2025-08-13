package su.xash.engine

import android.app.Application
import android.content.Context
import android.os.Environment
import android.os.StrictMode
import androidx.annotation.StringRes
import androidx.datastore.core.DataStore
import androidx.datastore.preferences.core.Preferences
import androidx.datastore.preferences.preferencesDataStore
import com.google.firebase.crashlytics.FirebaseCrashlytics
import dagger.Module
import dagger.Provides
import dagger.assisted.Assisted
import dagger.hilt.InstallIn
import dagger.hilt.android.HiltAndroidApp
import dagger.hilt.android.qualifiers.ApplicationContext
import dagger.hilt.components.SingletonComponent
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.flow.first
import su.xash.engine.data.AppPreferencesRepository
import su.xash.engine.data.GamePreferencesProvider
import su.xash.engine.data.GamePreferencesRepository
import su.xash.engine.data.GameRepository
import java.io.File
import javax.inject.Singleton

enum class Screen(@param:StringRes val title: Int) {
	Library(title = R.string.library),
	GameSettings(title = R.string.game_settings)
}


@Module
@InstallIn(SingletonComponent::class)
object ApplicationModule {
	@Provides
	@Singleton
	fun provideApplicationScope(): CoroutineScope {
		return CoroutineScope(SupervisorJob() + Dispatchers.Default)
	}
}

val Context.dataStore: DataStore<Preferences> by preferencesDataStore(name = "app_preferences")

@HiltAndroidApp
class MainApplication : Application() {
	override fun onCreate() {
		super.onCreate()

		if (BuildConfig.DEBUG) {
			StrictMode.setThreadPolicy(
				StrictMode.ThreadPolicy.Builder()
					.detectDiskReads()
					.detectDiskWrites()
					.detectAll()
					.penaltyLog()
					.build()
			)
			StrictMode.setVmPolicy(
				StrictMode.VmPolicy.Builder()
					.detectLeakedSqlLiteObjects()
					.detectLeakedClosableObjects()
					.penaltyLog()
					.build()
			)
		}

		FirebaseCrashlytics.getInstance().isCrashlyticsCollectionEnabled = !BuildConfig.DEBUG
	}
}
