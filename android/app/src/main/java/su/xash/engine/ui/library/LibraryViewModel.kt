package su.xash.engine.ui.library

import android.app.Application
import android.content.Context
import android.util.Log
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.LiveData
import androidx.lifecycle.MutableLiveData
import androidx.lifecycle.viewModelScope
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CoroutineStart
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import su.xash.engine.model.Game
import su.xash.engine.util.Nomedia
import su.xash.engine.util.Prefs
import java.io.File

class LibraryViewModel(application: Application) : AndroidViewModel(application) {
	val installedGames: LiveData<List<Game>> get() = _installedGames
	private val _installedGames = MutableLiveData(emptyList<Game>())

	val isReloading: LiveData<Boolean> get() = _isReloading
	private val _isReloading = MutableLiveData(false)

	val selectedItem: LiveData<Game> get() = _selectedItem
	private val _selectedItem = MutableLiveData<Game>()

	// Only one scan runs at a time; a new scan cancels the previous one
	private var scanJob: Job? = null

	// Root path of the running scan, or null when idle
	private var scanningRootPath: String? = null

	// Root path of the last completed scan, including failed scans
	private var scannedRootPath: String? = null

	// Rescans unless a scan of the same location is already running
	fun reloadGames() {
		val rootPath = Prefs.getGamePath(getApplication<Application>()).absolutePath

		if (rootPath != scanningRootPath) {
			startScan(rootPath)
		}
	}

	private fun startScan(rootPath: String) {
		scanJob?.cancel()
		scanningRootPath = rootPath
		_isReloading.value = true

		// The scan outlives the fragment that asked for it, so it must not
		// hold on to an Activity context through the Game objects.
		val appContext = getApplication<Application>()
		val previousGames = _installedGames.value.orEmpty()

		val job = viewModelScope.launch(start = CoroutineStart.LAZY) {
			val games = try {
				withContext(Dispatchers.IO) {
					val root = File(rootPath)

					if (!Nomedia.ensureNomedia(root)) {
						Log.w(TAG, "Cannot create .nomedia in $rootPath")
					}

					Game.getGames(appContext, root, previousGames)
				}
			} catch (e: CancellationException) {
				throw e
			} catch (e: Exception) {
				Log.e(TAG, "Failed to scan $rootPath", e)
				null
			}

			if (games != null) {
				_installedGames.value = games
			} else if (scannedRootPath != rootPath) {
				// Clear games from another location, but keep the current list on refresh failure
				_installedGames.value = emptyList()
			}
			scannedRootPath = rootPath

			scanJob = null
			scanningRootPath = null
			_isReloading.value = false
		}

		scanJob = job
		job.start()
	}

	fun setSelectedGame(game: Game) {
		_selectedItem.value = game
	}

	fun startEngine(ctx: Context, game: Game) {
		game.startEngine(ctx)
	}

	private companion object {
		const val TAG = "LibraryViewModel"
	}
}
