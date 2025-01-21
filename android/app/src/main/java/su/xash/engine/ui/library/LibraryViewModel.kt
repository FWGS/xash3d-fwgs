package su.xash.engine.ui.library

import android.app.Application
import android.content.Context
import android.net.Uri
import androidx.documentfile.provider.DocumentFile
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.LiveData
import androidx.lifecycle.MutableLiveData
import androidx.lifecycle.viewModelScope
import androidx.work.Data
import androidx.work.OneTimeWorkRequestBuilder
import androidx.work.WorkInfo
import androidx.work.WorkManager
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import su.xash.engine.model.Game
import su.xash.engine.model.ModDatabase
import su.xash.engine.workers.FileCopyWorker
import su.xash.engine.workers.KEY_FILE_URI

const val TAG_INSTALL = "TAG_INSTALL"

class LibraryViewModel(application: Application) : AndroidViewModel(application) {
	val installedGames: LiveData<List<Game>> get() = _installedGames
	private val _installedGames = MutableLiveData(emptyList<Game>())

	val downloads: LiveData<List<Game>> get() = _downloads
	private val _downloads = MutableLiveData(emptyList<Game>())

	val isReloading: LiveData<Boolean> get() = _isReloading
	private val _isReloading = MutableLiveData(false)

	private val workManager = WorkManager.getInstance(application.applicationContext)
	val workInfos: LiveData<List<WorkInfo>> = workManager.getWorkInfosByTagLiveData(TAG_INSTALL)

	val selectedItem: LiveData<Game> get() = _selectedItem
	private val _selectedItem = MutableLiveData<Game>()

	val appPreferences = application.getSharedPreferences("app_preferences", Context.MODE_PRIVATE)
	val modDb: ModDatabase

	init {
		modDb = application.assets.open(ModDatabase.getFilename()).use { ModDatabase(it) }
		reloadGames(application.applicationContext)
	}

	fun reloadGames(ctx: Context) {
		if (isReloading.value == true) {
			return
		}
		_isReloading.value = true

		viewModelScope.launch {
			withContext(Dispatchers.IO) {
				val games = mutableListOf<Game>()
				val root = DocumentFile.fromFile(ctx.getExternalFilesDir(null)!!)

				val installedGames = Game.getGames(ctx, root)
					.filter { p -> _downloads.value?.any { p.basedir.name == it.basedir.name } == false }

				games.addAll(installedGames)
				downloads.value?.let { games.addAll(it) }

				_installedGames.postValue(games)
				_isReloading.postValue(false)
			}
		}
	}

	fun refreshDownloads(ctx: Context) {
		viewModelScope.launch {
			withContext(Dispatchers.IO) {
				val games = mutableListOf<Game>()

				workInfos.value?.filter {
					it.state == WorkInfo.State.RUNNING && !it.progress.getString(FileCopyWorker.Input)
						.isNullOrEmpty()
				}?.forEach {
					val uri = Uri.parse(it.progress.getString(FileCopyWorker.Input))
					val file = DocumentFile.fromTreeUri(ctx, uri)
					games.addAll(Game.getGames(ctx, file!!))
					games.forEach { g -> g.installed = false }
				}

				_downloads.postValue(games)
			}
		}
	}

	fun installGame(uri: Uri) {
		val data = Data.Builder().putString(KEY_FILE_URI, uri.toString()).build()
		val request = OneTimeWorkRequestBuilder<FileCopyWorker>().run {
			setInputData(data)
			addTag(TAG_INSTALL)
			build()
		}
		workManager.enqueue(request)
	}

	fun setSelectedGame(game: Game) {
		_selectedItem.value = game
	}

	fun uninstallGame(game: Game) {
		viewModelScope.launch {
			withContext(Dispatchers.IO) {
				game.installed = false
				game.basedir.delete()
				_installedGames.postValue(_installedGames.value)
			}
		}
	}

	fun startEngine(ctx: Context, game: Game) {
		game.startEngine(ctx)
	}
}
