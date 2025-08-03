package su.xash.engine.ui

import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.navigation.NavHostController
import androidx.navigation.NavType
import androidx.navigation.compose.NavHost
import androidx.navigation.compose.composable
import androidx.navigation.compose.currentBackStackEntryAsState
import androidx.navigation.compose.rememberNavController
import androidx.navigation.navArgument
import su.xash.engine.Screen
import su.xash.engine.ui.library.LibraryScreen

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun LauncherAppBar(currentScreen: Screen, modifier: Modifier = Modifier) {
	TopAppBar(
		title = { Text(stringResource(currentScreen.title)) },
//		colors = TopAppBarDefaults.topAppBarColors(
//			containerColor = MaterialTheme.colorScheme.primaryContainer,
//			titleContentColor = MaterialTheme.colorScheme.primary
//		),
		modifier = modifier
	)
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun LauncherApp(
	navController: NavHostController = rememberNavController()
) {
	val backStackEntry by navController.currentBackStackEntryAsState()
	val currentScreen = Screen.valueOf(
		backStackEntry?.destination?.route ?: Screen.Library.name
	)

	Scaffold(
		topBar = {
			LauncherAppBar(
				currentScreen = currentScreen
			)
		}) { innerPadding ->
		NavHost(
			navController = navController,
			startDestination = Screen.Library.name,
			modifier = Modifier
				.fillMaxSize()
				.padding(innerPadding)
		) {
			composable(Screen.Library.name) { LibraryScreen() }
		}
	}
}