package su.xash.engine.ui.library

import android.content.Context
import android.content.Intent
import androidx.compose.runtime.Immutable
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import dagger.hilt.android.lifecycle.HiltViewModel
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch
import su.xash.engine.XashActivity
import su.xash.engine.data.Game
import su.xash.engine.data.GamePreferencesProvider
import su.xash.engine.data.GamePreferencesRepository
import su.xash.engine.data.GameRepository
import javax.inject.Inject

@HiltViewModel
class LibraryViewModel @Inject constructor(
	private val gameRepository: GameRepository,
//	appPreferencesRepository: AppPreferencesRepository,
	private val gamePreferencesProvider: GamePreferencesProvider
) : ViewModel() {
	private val _uiState = MutableStateFlow(LibraryScreenUiState())
	val uiState: StateFlow<LibraryScreenUiState> = _uiState.asStateFlow()

	fun updateStoragePermissionStatus(granted: Boolean) {
		viewModelScope.launch {
			_uiState.update {
				it.copy(
					hasStoragePermission = granted,
					isLoading = false
				)
			}

			if (granted) {
				_uiState.update {
					it.copy(
						gamesList = gameRepository.scanForGames()
					)
				}
			}
		}
	}

	fun refreshGames() {
		viewModelScope.launch {
			_uiState.update {
				it.copy(
					gamesList = gameRepository.scanForGames(),
					isRefreshing = true
				)
			}
			delay(1000L) // stupid bug?
			_uiState.update {
				it.copy(
					isRefreshing = false
				)
			}
		}
	}

	fun getBaseDirectory(): String? = gameRepository.currentBaseDir.value

	fun getGamePreferences(game: Game) =
		GamePreferencesRepository(gamePreferencesProvider.provide(game.gameInfo.gameDir))
}

@Immutable
data class LibraryScreenUiState(
	val isLoading: Boolean = true,
	val gamesList: List<Game> = emptyList(),
	val hasStoragePermission: Boolean = false,
	val baseDirectory: String? = null,
	val isRefreshing: Boolean = false
)