package su.xash.engine.ui.library

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.IntrinsicSize
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Save
import androidx.compose.material3.Button
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.ModalBottomSheet
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import androidx.hilt.navigation.compose.hiltViewModel
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import coil.compose.AsyncImage
import su.xash.engine.R
import su.xash.engine.data.Game
import su.xash.engine.ui.theme.AppTypography

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun GameSettings(
	game: Game,
	onDismissRequest: () -> Unit = { }
) {
	val viewModel =
		hiltViewModel<GameSettingsViewModel, GameSettingsViewModel.Factory>(creationCallback = { factory ->
			factory.create(game)
		})

	val preferences by viewModel.preferences.collectAsStateWithLifecycle()

	var argumentsInput by remember { mutableStateOf(preferences.arguments) }
	var useVolumeButtons by remember { mutableStateOf(preferences.useVolumeButtons) }

	LaunchedEffect(preferences) {
		argumentsInput = preferences.arguments
		useVolumeButtons = preferences.useVolumeButtons
	}

	ModalBottomSheet(
		onDismissRequest = {
			viewModel.updateArguments(argumentsInput)
			viewModel.updateUseVolumeButtons(useVolumeButtons)

			onDismissRequest()
		},
		/*modifier = Modifier.heightIn(min = 256.dp)*/
	) {
		Column(
			modifier = Modifier
				.fillMaxWidth()
				.padding(all = 16.dp),
			verticalArrangement = Arrangement.spacedBy(8.dp),
			horizontalAlignment = Alignment.CenterHorizontally
		) {
			Row(modifier = Modifier.height(IntrinsicSize.Min),
				horizontalArrangement = Arrangement.spacedBy(8.dp)) {
				if (game.icon != null) {
					AsyncImage(
						model = game.icon,
						contentDescription = null,
						modifier = Modifier.fillMaxHeight()
					)
				}
				Text(
					game.gameInfo.title,
					style = AppTypography.headlineSmall
				)
			}
			OutlinedTextField(
				value = argumentsInput,
				onValueChange = { argumentsInput = it },
				label = {
					Text(
						stringResource(R.string.game_settings_command_line)
					)
				},
				modifier = Modifier.fillMaxWidth()
			)
			Row(
				modifier = Modifier.fillMaxWidth(),
				verticalAlignment = Alignment.CenterVertically
			) {
				Text(
					stringResource(R.string.game_settings_volume_buttons),
				)
				Spacer(modifier = Modifier.weight(1f))
				Switch(
					checked = useVolumeButtons,
					onCheckedChange = { useVolumeButtons = it }
				)
			}
			Button(onClick = {
				viewModel.updateArguments(argumentsInput)
				viewModel.updateUseVolumeButtons(useVolumeButtons)

				onDismissRequest()
			}, modifier = Modifier.align(Alignment.End)) {
				Row(verticalAlignment = Alignment.CenterVertically) {
					Icon(Icons.Default.Save, contentDescription = null)
					Spacer(Modifier.width(8.dp))
					Text(stringResource(R.string.save))
				}
			}
		}
	}
}

//@Preview(showBackground = true)
//@Composable
//fun GameSettingsPreview() {
//	AppTheme {
//		GameSettings(
//			Game(
//				GameInfo(
//					title = "Half-Death",
//					hdBackground = false,
//					icon = null,
//					baseDir = "fwgs",
//					gameDir = "fwgs"
//				), null
//			), null
//		)
//	}
//}