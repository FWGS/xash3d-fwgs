package su.xash.engine.ui.library

import android.Manifest
import android.content.Intent
import android.net.Uri
import android.os.Build
import android.os.Environment
import android.provider.Settings
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.annotation.RequiresApi
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.IntrinsicSize
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.PlayArrow
import androidx.compose.material.icons.filled.Settings
import androidx.compose.material3.Button
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.ElevatedCard
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.VerticalDivider
import androidx.compose.material3.pulltorefresh.PullToRefreshBox
import androidx.compose.material3.rememberModalBottomSheetState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.unit.dp
import androidx.core.content.ContextCompat
import androidx.hilt.navigation.compose.hiltViewModel
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleEventObserver
import androidx.lifecycle.compose.LocalLifecycleOwner
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import coil.compose.AsyncImage
import com.google.accompanist.permissions.ExperimentalPermissionsApi
import com.google.accompanist.permissions.rememberMultiplePermissionsState
import kotlinx.coroutines.flow.filterNotNull
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.launch
import su.xash.engine.BuildConfig
import su.xash.engine.R
import su.xash.engine.XashActivity
import su.xash.engine.data.Game
import su.xash.engine.data.GameInfo
import su.xash.engine.ui.theme.AppTheme
import su.xash.engine.ui.theme.AppTypography


@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun LibraryScreen(viewModel: LibraryViewModel = hiltViewModel()) {
	val context = LocalContext.current
	val uiState by viewModel.uiState.collectAsStateWithLifecycle()
	val sheetState = rememberModalBottomSheetState()
	var selectedItem by remember { mutableStateOf<Game?>(null) }
	val scope = rememberCoroutineScope()

	LaunchedEffect(selectedItem) {
		if (selectedItem == null) {
			sheetState.hide()
		}
	}

	LaunchedEffect(Unit) {
		if (BuildConfig.IS_GOOGLE_PLAY_BUILD) {

		} else if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
			viewModel.updateStoragePermissionStatus(Environment.isExternalStorageManager())
		} else {
			viewModel.updateStoragePermissionStatus(
				ContextCompat.checkSelfPermission(context, Manifest.permission.READ_EXTERNAL_STORAGE) ==
						android.content.pm.PackageManager.PERMISSION_GRANTED &&
						ContextCompat.checkSelfPermission(context, Manifest.permission.WRITE_EXTERNAL_STORAGE) ==
						android.content.pm.PackageManager.PERMISSION_GRANTED
			)
		}
	}

	if (uiState.isLoading) {
		LoadingScreen()
	} else if (!uiState.hasStoragePermission) {
		PermissionRequestScreen(
			onPermissionUpdate = { viewModel.updateStoragePermissionStatus(it) }
		)
	} else if (uiState.gamesList.isEmpty()) {
		EmptyListScreen(viewModel.getBaseDirectory())
	} else {
		PullToRefreshBox(
			isRefreshing = uiState.isRefreshing,
			onRefresh = { viewModel.refreshGames() }
		) {
			LibraryScreen(
				games = uiState.gamesList,
				onItemPlayClick = {
					scope.launch {
						val gamePreferences = viewModel.getGamePreferences(it)
						val preferences = gamePreferences.preferences.filterNotNull().first()
						val intent = Intent(context, XashActivity::class.java).apply {
							flags = Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_CLEAR_TASK
							putExtra("gamedir", it.gameInfo.gameDir)
							putExtra("argv", preferences.arguments)
							putExtra("usevolume", preferences.useVolumeButtons)
							// putExtra("basedir", basedir.parent)
						}
						context.startActivity(intent)
					}
				},
				onItemSettingsClick = { selectedItem = it })
		}
	}

	if (selectedItem != null) {
		GameSettings(
			game = selectedItem!!
		) {
			selectedItem = null
		}
	}
}

@OptIn(ExperimentalPermissionsApi::class)
@Composable
fun PermissionRequestScreen(
	onPermissionUpdate: (Boolean) -> Unit
) {
	if (BuildConfig.IS_GOOGLE_PLAY_BUILD) {
		PermissionRequestScreen_Scoped()
	} else if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
		PermissionRequestScreen_AllFilesAccess(
			onPermissionUpdate = onPermissionUpdate
		)
	} else {
		PermissionRequestScreen_Runtime(
			onPermissionUpdate = onPermissionUpdate
		)
	}
}

@Composable
fun PermissionRequestScreen_Scoped() {

}

@RequiresApi(Build.VERSION_CODES.R)
@Composable
fun PermissionRequestScreen_AllFilesAccess(
	onPermissionUpdate: (Boolean) -> Unit
) {
	val context = LocalContext.current
	val lifecycleOwner = LocalLifecycleOwner.current
	val settingsLauncher = rememberLauncherForActivityResult(
		ActivityResultContracts.StartActivityForResult()
	) { result ->
		onPermissionUpdate(Environment.isExternalStorageManager())
	}

	DisposableEffect(lifecycleOwner) {
		val observer = LifecycleEventObserver { _, event ->
			if (event == Lifecycle.Event.ON_RESUME) {
				onPermissionUpdate(Environment.isExternalStorageManager())
			}
		}

		lifecycleOwner.lifecycle.addObserver(observer)

		onDispose {
			lifecycleOwner.lifecycle.removeObserver(observer)
		}
	}

	Column(
		modifier = Modifier
			.fillMaxHeight()
			.padding(all = 16.dp),
		horizontalAlignment = Alignment.CenterHorizontally,
		verticalArrangement = Arrangement.Center
	) {
		Text(
			text = stringResource(R.string.file_access_required),
			style = AppTypography.headlineSmall,
			textAlign = TextAlign.Center
		)
		Spacer(modifier = Modifier.height(16.dp))
		Button(onClick = {
			val intent = Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION)
			intent.data = Uri.fromParts("package", context.packageName, null)
			settingsLauncher.launch(intent)
		}) {
			Text(stringResource(R.string.request_permissions))
		}
	}
}

@OptIn(ExperimentalPermissionsApi::class)
@Composable
fun PermissionRequestScreen_Runtime(
	onPermissionUpdate: (Boolean) -> Unit
) {
	val permissionState = rememberMultiplePermissionsState(
		listOf(
			Manifest.permission.WRITE_EXTERNAL_STORAGE,
			Manifest.permission.READ_EXTERNAL_STORAGE
		)
	)

	LaunchedEffect(permissionState.allPermissionsGranted) {
		onPermissionUpdate(permissionState.allPermissionsGranted)
	}

	Column(
		modifier = Modifier
			.fillMaxHeight()
			.padding(all = 16.dp),
		horizontalAlignment = Alignment.CenterHorizontally,
		verticalArrangement = Arrangement.Center
	) {
		Text(
			text = stringResource(R.string.external_storage_required),
			style = AppTypography.headlineSmall,
			textAlign = TextAlign.Center
		)
		Spacer(modifier = Modifier.height(16.dp))
		Button(onClick = {
			permissionState.launchMultiplePermissionRequest()
		}) {
			Text(stringResource(R.string.request_permissions))
		}
	}
}

@Composable
fun LibraryScreen(
	games: List<Game>,
	onItemPlayClick: (Game) -> Unit = {},
	onItemSettingsClick: (Game) -> Unit = {}
) {
	Box(
		modifier = Modifier
			.fillMaxSize()
			.padding(all = 8.dp)
	) {
		LazyColumn(
			modifier = Modifier.fillMaxSize(),
			verticalArrangement = Arrangement.spacedBy(8.dp)
		) {
			items(games) { item -> LibraryItem(item, onItemPlayClick, onItemSettingsClick) }
		}
//		ExtendedFloatingActionButton(
//			onClick = { },
//			icon = { Icon(Icons.Default.Download, contentDescription = null) },
//			text = { Text(stringResource(R.string.downloads)) },
//			modifier = Modifier.align(Alignment.BottomEnd)
//		)
	}
}

@OptIn(ExperimentalPermissionsApi::class)
@Preview(showBackground = true)
@Composable
fun PermissionRequestScreenPreview() {
	AppTheme { PermissionRequestScreen {} }
}

@Preview(showBackground = true)
@Composable
fun LibraryScreenPreview() {
	AppTheme {
		LibraryScreen(
			listOf(
				Game(
					GameInfo(
						title = "Half-Death",
						hdBackground = false,
						icon = "game.ico",
						baseDir = "fwgs",
						gameDir = "fwgs"
					), null, null
				),
				Game(
					GameInfo(
						title = "Company Castle 3",
						hdBackground = false,
						icon = "game.ico",
						baseDir = "cc",
						gameDir = "fwgs"
					), null, null
				),
			)
		)
	}
}

@Composable
fun LibraryItem(
	item: Game,
	onItemPlayClick: (Game) -> Unit = {},
	onItemSettingsClick: (Game) -> Unit = {}
) {
	ElevatedCard(
		modifier = Modifier
			.fillMaxWidth()
			.height(96.dp)
	) {
		Box(
			modifier = Modifier
				.fillMaxSize()
		) {
			AsyncImage(
				model = item.background,
				contentDescription = null,
				modifier = Modifier.fillMaxSize(),
				contentScale = ContentScale.Crop
			)
			Row(
				modifier = Modifier
					.align(Alignment.BottomStart)
					.background(
						color = MaterialTheme.colorScheme.primaryContainer,
						shape = MaterialTheme.shapes.small
					)
					.padding(8.dp)
					.height(IntrinsicSize.Min),
				horizontalArrangement = Arrangement.spacedBy(8.dp)
			) {
				if (item.icon != null) {
					AsyncImage(
						model = item.icon,
						contentDescription = null,
						modifier = Modifier.fillMaxHeight()
					)
				}
				Text(
					text = item.gameInfo.title,
					style = AppTypography.titleMedium
				)
			}
			Row(modifier = Modifier.align(Alignment.CenterEnd)) {
				IconButton(
					modifier = Modifier
						.fillMaxHeight()
						.background(MaterialTheme.colorScheme.primaryContainer),
					onClick = { onItemPlayClick(item) }
				) {
					Icon(Icons.Default.PlayArrow, contentDescription = null)
				}
				VerticalDivider()
				IconButton(
					modifier = Modifier
						.fillMaxHeight()
						.background(MaterialTheme.colorScheme.primaryContainer),
					onClick = { onItemSettingsClick(item) }
				) {
					Icon(Icons.Default.Settings, contentDescription = null)
				}
			}
		}
	}
}

@Composable
fun LoadingScreen() {
	Box(
		modifier = Modifier.fillMaxSize(),
		contentAlignment = Alignment.Center
	) {
		CircularProgressIndicator()
	}
}

@Preview(showBackground = true)
@Composable
fun LoadingScreenPreview() {
	AppTheme { LoadingScreen() }
}

@Composable
fun EmptyListScreen(baseDirectory: String?) {
	Box(
		modifier = Modifier
			.fillMaxSize()
			.padding(16.dp),
		contentAlignment = Alignment.Center
	) {
		Text(
			stringResource(R.string.no_games_installed, baseDirectory ?: "the xash folder"),
			style = AppTypography.headlineSmall,
			textAlign = TextAlign.Center
		)
	}
}

@Preview(showBackground = true)
@Composable
fun EmptyListScreenPreview() {
	AppTheme { EmptyListScreen(null) }
}