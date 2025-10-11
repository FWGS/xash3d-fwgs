package su.xash.engine.ui.library

import androidx.compose.runtime.Immutable
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import dagger.assisted.Assisted
import dagger.assisted.AssistedFactory
import dagger.assisted.AssistedInject
import dagger.hilt.android.lifecycle.HiltViewModel
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.stateIn
import kotlinx.coroutines.launch
import su.xash.engine.data.Game
import su.xash.engine.data.GamePreferences
import su.xash.engine.data.GamePreferencesProvider
import su.xash.engine.data.GamePreferencesRepository

@HiltViewModel(assistedFactory = GameSettingsViewModel.Factory::class)
class GameSettingsViewModel @AssistedInject constructor(
	private val gamePreferencesProvider: GamePreferencesProvider,
	@Assisted val game: Game
) : ViewModel() {
	//	private val _uiState = MutableStateFlow(GameSettingsUiState())
//	val uiState: StateFlow<GameSettingsUiState> = _uiState.asStateFlow()
	val gamePreferencesRepository =
		GamePreferencesRepository(gamePreferencesProvider.provide(game.gameInfo.gameDir))

	val preferences: StateFlow<GamePreferences> = gamePreferencesRepository.preferences.stateIn(
		scope = viewModelScope,
		started = SharingStarted.WhileSubscribed(5000L),
		initialValue = GamePreferences()
	)

	fun updateArguments(arguments: String) =
		viewModelScope.launch { gamePreferencesRepository.setArguments(arguments) }

	fun updateUseVolumeButtons(useVolumeButtons: Boolean) =
		viewModelScope.launch { gamePreferencesRepository.setUseVolumeButtons(useVolumeButtons) }

//	fun getGamePreferences(game: Game) = gamePreferencesRepository.create(game.gameInfo.gameDir)

	@AssistedFactory
	interface Factory {
		fun create(game: Game): GameSettingsViewModel
	}
}

@Immutable
data class GameSettingsUiState(
	val arguments: String = "",
	val useVolumeButtons: Boolean = false
)