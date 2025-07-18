package su.xash.engine.ui.library

import android.app.Application
import android.content.Context
import android.content.SharedPreferences
import android.os.Environment
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.LiveData
import androidx.lifecycle.MutableLiveData
import androidx.lifecycle.viewModelScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import su.xash.engine.model.Game
import java.io.File

class LibraryViewModel(application: Application) : AndroidViewModel(application) {
	val installedGames: LiveData<List<Game>> get() = _installedGames
	private val _installedGames = MutableLiveData(emptyList<Game>())

	val isReloading: LiveData<Boolean> get() = _isReloading
	private val _isReloading = MutableLiveData(false)

	val selectedItem: LiveData<Game> get() = _selectedItem
	private val _selectedItem = MutableLiveData<Game>()

	private val appPreferences: SharedPreferences =
		application.getSharedPreferences("app_preferences", Context.MODE_PRIVATE)

	fun reloadGames(ctx: Context) {
		if (isReloading.value == true) {
			return
		}
		_isReloading.value = true

		viewModelScope.launch {
			withContext(Dispatchers.IO) {
				val rootPath = appPreferences.getString("game_path", null)
					?: (Environment.getExternalStorageDirectory().absolutePath + "/xash")
				val root = File(rootPath)

				_installedGames.postValue(Game.getGames(ctx, root))
				_isReloading.postValue(false)
			}
		}
	}

	fun setSelectedGame(game: Game) {
		_selectedItem.value = game
	}

	fun startEngine(ctx: Context, game: Game) {
		game.startEngine(ctx)
	}
}
