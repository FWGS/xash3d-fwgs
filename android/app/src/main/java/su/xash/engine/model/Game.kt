package su.xash.engine.model

import android.content.Context
import android.content.Intent
import android.content.pm.PackageInfo
import android.content.pm.PackageManager
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.view.LayoutInflater
import android.widget.TextView
import androidx.core.net.toUri
import androidx.preference.PreferenceManager
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import com.google.android.material.progressindicator.LinearProgressIndicator
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.launch
import su.xash.engine.R
import su.xash.engine.XashActivity
import java.io.File
import java.io.FileInputStream


class Game(val ctx: Context, val basedir: File) {
	private var iconName = "game.ico"
	var title = "Unknown Game"
	var icon: Bitmap? = null
	var cover: Bitmap? = null

	val mobileHacksGames = arrayOf("aom", "bdlands", "biglolly", "bshift", "caseclosed",
		"hl_urbicide", "induction", "redempt", "secret",
		"sewer_beta", "tot", "valve", "vendetta")

	var defaultGameDir = "valve"

	private val pref = ctx.getSharedPreferences(basedir.name, Context.MODE_PRIVATE)

	init {
		val gameInfo = File(basedir, "gameinfo.txt")
		if (gameInfo.exists()) {
			parseGameInfo(gameInfo)
		} else {
			val libListGam = File(basedir, "liblist.gam")
			if (libListGam.exists()) parseGameInfo(libListGam)
		}

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
				val appPref = PreferenceManager.getDefaultSharedPreferences(ctx)
				if (appPref.getBoolean("enable_yapb_bots", true)) {
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
			val appPref = PreferenceManager.getDefaultSharedPreferences(ctx)
			val downloaderEnabled = appPref.getBoolean("enable_downloader", false)

			if (downloaderEnabled) {
				val downloader = GameLibDownloader(ctx)
				if (downloader.isDownloaded(basedir.name)) {
					downloader.logExistingLibs(basedir.name)
					launchEngine(ctx, commandLineArgs)
					return
				}

				val scope = CoroutineScope(Dispatchers.Main + SupervisorJob())
				scope.launch {
					when (val r = downloader.lookupBuild(basedir.name)) {
						is GameLibDownloader.Lookup.Available -> showDownloadDialog(ctx, downloader, commandLineArgs)
						is GameLibDownloader.Lookup.NotInManifest -> launchEngine(ctx, commandLineArgs)
						is GameLibDownloader.Lookup.Error -> showManifestErrorDialog(ctx, commandLineArgs, r.cause)
					}
				}
				return
			}

			launchEngine(ctx, commandLineArgs)
			return
		}

		launchEngine(ctx, commandLineArgs)
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

	private fun showDownloadDialog(ctx: Context, downloader: GameLibDownloader, commandLineArgs: String) {
		val view = LayoutInflater.from(ctx).inflate(R.layout.dialog_download_progress, null)
		val progressBar = view.findViewById<LinearProgressIndicator>(R.id.downloadProgress)
		val statusText = view.findViewById<TextView>(R.id.downloadStatus)

		val dialog = MaterialAlertDialogBuilder(ctx)
			.setTitle(R.string.downloading_game_libs)
			.setView(view)
			.setCancelable(true)
			.setNegativeButton(android.R.string.cancel) { d, _ -> d.dismiss() }
			.create()

		dialog.show()

		val scope = CoroutineScope(Dispatchers.Main + SupervisorJob())
		val job = scope.launch {
			val result = downloader.download(basedir.name) { progress ->
				progressBar.isIndeterminate = false
				progressBar.progress = (progress * 100).toInt()
				statusText.text = ctx.getString(R.string.download_progress, (progress * 100).toInt())
			}

			if (!dialog.isShowing) return@launch

			dialog.dismiss()

			if (result.isSuccess) {
				launchEngine(ctx, commandLineArgs)
			} else {
				MaterialAlertDialogBuilder(ctx)
					.setTitle(R.string.download_failed)
					.setMessage(result.exceptionOrNull()?.message
						?: ctx.getString(R.string.download_error))
					.setPositiveButton(android.R.string.ok, null)
					.show()
			}
		}

		dialog.setOnDismissListener { job.cancel() }
	}

	private fun showManifestErrorDialog(ctx: Context, commandLineArgs: String, cause: Throwable) {
		MaterialAlertDialogBuilder(ctx)
			.setTitle(R.string.manifest_error_title)
			.setMessage(ctx.getString(R.string.manifest_error_message, cause.message ?: cause.javaClass.simpleName))
			.setPositiveButton(R.string.launch_anyway) { _, _ -> launchEngine(ctx, commandLineArgs) }
			.setNegativeButton(android.R.string.cancel, null)
			.show()
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

		return "https://github.com/FWGS/xash3d-fwgs/releases/download/continuous/xash3d-fwgs-android.apk"
	}

	private fun getGameLibDir(ctx: Context, packageName: String): String? {
		val packageInfo: PackageInfo = ctx.packageManager.getPackageInfo(packageName, 0)
		return packageInfo.applicationInfo?.nativeLibraryDir
	}

	companion object {
		fun getGames(ctx: Context, file: File): List<Game> {
			val games = mutableListOf<Game>()

			if (checkIfGamedir(file)) {
				games.add(Game(ctx, file))
			} else {
				file.listFiles()?.forEach {
					if (it.isDirectory) {
						if (checkIfGamedir(it)) {
							games.add(Game(ctx, it))
						}
					}
				}
			}

			return games
		}

		fun checkIfGamedir(file: File): Boolean {
			if (File(file, "liblist.gam").exists()) return true
			if (File(file, "gameinfo.txt").exists()) return true

			return false
		}
	}
}
