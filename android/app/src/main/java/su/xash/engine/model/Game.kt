package su.xash.engine.model

import android.content.Context
import android.content.Intent
import android.content.pm.PackageInfo
import android.content.pm.PackageManager
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import androidx.core.net.toUri
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.launch
import su.xash.engine.R
import su.xash.engine.XashActivity
import su.xash.engine.util.showDownloadProgressDialog
import java.io.File
import java.io.FileInputStream

class Game(val ctx: Context, val basedir: File, val gameInfoFile: File) {
	private var iconName = "game.ico"
	var title = "Unknown Game"
	var icon: Bitmap? = null
	var cover: Bitmap? = null

	val mobileHacksGames = arrayOf("aom", "bdlands", "biglolly", "bshift", "caseclosed",
		"hl_urbicide", "induction", "redempt", "secret",
		"sewer_beta", "tot", "valve", "vendetta")

	// a1ba: follow the behavior of Xash3D's game_launch.
	// for hl mods we put `valve` as game directory
	// for any other game that's not hl this string must be replaced with your
	// main game directory
	// mods always use -game command line parameter
	var defaultGameDir = "valve"

	private val pref = ctx.getSharedPreferences(basedir.name, Context.MODE_PRIVATE)

	init {
		parseGameInfo(gameInfoFile)

		val iconFile = File(basedir, iconName)
		if (iconFile.exists()) {
			icon = BitmapFactory.decodeFile(iconFile.path)
		}

		try {
			cover = BackgroundBitmap.createBackground(basedir)
		} catch (e: Exception) {
			e.printStackTrace()
		}
	}

	fun startEngine(ctx: Context) {
		val packageNames = getPackageNamesForGameDir(basedir.name)
		var externalGame = false
		var commandLineArgs = ""

		if (basedir.name != defaultGameDir)
			commandLineArgs += "-game ${basedir.name} "

		if (packageNames != null) {
			if (packageNames.contains("su.xash.engine")) {
				commandLineArgs += "-dll @hl "
			} else if (packageNames.contains("su.xash.cs16client")) {
				if (pref.getBoolean("enable_yapb_bots", true)) {
					commandLineArgs += "-dll @yapb "
				}
				externalGame = true
			}
		}

		commandLineArgs += pref.getString("arguments", "-console -log") ?: ""

		if (externalGame && packageNames != null) {
			var packageName: String? = null
			var gameLibDir: String? = null

			for (pn in packageNames) {
				gameLibDir = try {
					getGameLibDir(ctx, pn)
				} catch (e: PackageManager.NameNotFoundException) {
					null
				} catch (e: Exception) {
					e.printStackTrace()
					null
				}

				if (gameLibDir != null) {
					packageName = pn
					break
				}
			}

			if (gameLibDir == null) {
				MaterialAlertDialogBuilder(ctx).apply {
					setTitle(R.string.game_apk_required)
					setMessage(R.string.game_apk_message)
					setPositiveButton(R.string.game_apk_install) { _, _ ->
						val intent = Intent(Intent.ACTION_VIEW,
							getDownloadPageForGameDir(basedir.name).toUri())
						ctx.startActivity(intent)
					}
					show()
				}
				return
			}

			launchEngine(ctx, commandLineArgs, packageName = packageName, gameLibDir = gameLibDir)
			return
		}

		if (packageNames == null) {
			// Unknown game — try to use downloaded libraries from hlsdk-mega-build
			val downloader = GameLibDownloader(ctx)
			val args = commandLineArgs

			if (downloader.isDownloaded(basedir.name)) {
				downloader.logExistingLibs(basedir.name)
				launchEngine(ctx, args)
				return
			}

			val scope = CoroutineScope(Dispatchers.Main + SupervisorJob())
			scope.launch {
				when (val r = downloader.lookupBuild(basedir.name)) {
					is GameLibDownloader.Lookup.Available -> showDownloadDialog(ctx, downloader, args)
					is GameLibDownloader.Lookup.NotInManifest -> launchEngine(ctx, args)
					is GameLibDownloader.Lookup.Error -> showManifestErrorDialog(ctx, args, r.cause)
				}
			}
			return
		}

		launchEngine(ctx, commandLineArgs)
	}

	private fun showDownloadDialog(ctx: Context, downloader: GameLibDownloader, commandLineArgs: String) {
		showDownloadProgressDialog(
			ctx = ctx,
			titleRes = R.string.downloading_game_libs,
			cancelable = true,
			scope = CoroutineScope(Dispatchers.Main + SupervisorJob()),
			download = { onProgress -> downloader.download(basedir.name, onProgress) },
			onSuccess = { launchEngine(ctx, commandLineArgs) },
		)
	}

	private fun showManifestErrorDialog(ctx: Context, commandLineArgs: String, cause: Throwable) {
		MaterialAlertDialogBuilder(ctx)
			.setTitle(R.string.manifest_error_title)
			.setMessage(ctx.getString(R.string.manifest_error_message, cause.message ?: cause.javaClass.simpleName))
			.setPositiveButton(R.string.launch_anyway) { _, _ -> launchEngine(ctx, commandLineArgs) }
			.setNegativeButton(android.R.string.cancel, null)
			.show()
	}

	private fun launchEngine(
		ctx: Context,
		commandLineArgs: String,
		packageName: String? = null,
		gameLibDir: String? = null
	) {
		ctx.startActivity(Intent(ctx, XashActivity::class.java).apply {
			flags = Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_CLEAR_TASK

			putExtra("gamedir", defaultGameDir)
			putExtra("argv", commandLineArgs)
			putExtra("usevolume", pref.getBoolean("use_volume_buttons", false))
			putExtra("basedir", basedir.parent)

			if (gameLibDir != null) putExtra("gamelibdir", gameLibDir)
			if (packageName != null) putExtra("package", packageName)
		})
	}

	private fun parseGameInfo(file: File) {
		FileInputStream(file).use { inputStream ->
			inputStream.bufferedReader().use { reader ->
				reader.forEachLine {
					val tokens = it.split("\\s+".toRegex(), limit = 2)
					if (tokens.size >= 2) {
						val k = tokens[0]
						val v = tokens[1].trim('"')

						if (k == "title" || k == "game") title = v
						if (k == "icon") iconName = v
					}
				}
			}
		}
	}

	private fun getPackageNamesForGameDir(gamedir: String): Array<String>? {
		if (gamedir.equals("cstrike", ignoreCase = true)
			|| gamedir.equals("czero", ignoreCase = true))
			return arrayOf("su.xash.cs16client.test", "su.xash.cs16client")

		if (gamedir.equals("tfc", ignoreCase = true))
			return arrayOf("su.xash.tf15client.test", "su.xash.tf15client")

		// mobile_hacks hlsdk-portable branch allows us to have few more mods out of the box
		if (mobileHacksGames.any { it.equals(gamedir, ignoreCase = true) })
			return arrayOf("su.xash.engine")

		return null
	}

	private fun getDownloadPageForGameDir(gamedir: String): String {
		if (gamedir.equals("cstrike", ignoreCase = true)
			|| gamedir.equals("czero", ignoreCase = true))
			return "https://github.com/Velaron/cs16-client/releases/download/continuous/CS16Client-Android.apk"

		if (gamedir.equals("tfc", ignoreCase = true))
			return "https://github.com/Velaron/tf15-client/releases/download/continuous/TF15Client-Android.apk"

		// just so we don't return null
		return "https://github.com/FWGS/xash3d-fwgs/releases/download/continuous/xash3d-fwgs-android.apk"
	}

	private fun getGameLibDir(ctx: Context, packageName: String): String? {
		val packageInfo: PackageInfo = ctx.packageManager.getPackageInfo(packageName, 0)
		return packageInfo.applicationInfo?.nativeLibraryDir
	}

	companion object {
		fun getGames(ctx: Context, root: File): List<Game> {
			val games = mutableListOf<Game>()

			root.listFiles()?.forEach {
				if (it.isDirectory) {
					val subDirGameInfoFile = checkIfGamedir(it)
					if (subDirGameInfoFile != null) {
						games.add(Game(ctx, it, subDirGameInfoFile))
					}
				}
			}

			return games
		}

		fun checkIfGamedir(gamedir: File): File? {
			gamedir.listFiles()?.forEach {
				if (it.isFile) {
					if (it.name.equals("liblist.gam", ignoreCase = true))
						return it

					if (it.name.equals("gameinfo.txt", ignoreCase = true))
						return it
				}
			}

			return null
		}
	}
}
