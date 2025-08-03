package su.xash.engine.data

import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.graphics.Canvas
import androidx.core.graphics.createBitmap
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.filterNotNull
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.launch
import su.xash.engine.util.TGAReader
import java.io.File
import java.io.FileInputStream
import javax.inject.Inject
import javax.inject.Singleton

data class BackgroundElement(
	val path: String, val x: Int, val y: Int
)

data class GameInfo(
	val title: String,
	val hdBackground: Boolean,
	val icon: String,
	val baseDir: String,
	val gameDir: String
)

data class Game(
	val gameInfo: GameInfo,
	val background: Bitmap?,
	val icon: Bitmap?
)

@Singleton
class GameRepository @Inject constructor(
	private val appPreferencesRepository: AppPreferencesRepository,
	private val applicationScope: CoroutineScope
) {
	private val _currentBaseDir = MutableStateFlow<String?>(null)
	val currentBaseDir: StateFlow<String?> = _currentBaseDir.asStateFlow()

	init {
		applicationScope.launch {
			appPreferencesRepository.baseDirectory.collect { baseDir ->
				_currentBaseDir.value = baseDir
			}
		}
	}

	suspend fun scanForGames(): List<Game> {
		val baseDir = File(_currentBaseDir.filterNotNull().first())

		// create .nomedia file to avoid garbage in the gallery
		if (baseDir.exists() && !baseDir.resolve(".nomedia").exists()) {
			baseDir.resolve(".nomedia").createNewFile()
		}

		val games = mutableListOf<Game>()

		baseDir.listFiles()?.forEach { file ->
			if (file.isDirectory) {
				createGame(file)?.let { game -> games.add(game) }
			}
		}

		return games
	}

	fun createGame(directory: File): Game? {
		val gameInfo = parseGameInfo(directory) ?: return null

		return Game(
			gameInfo = gameInfo,
			background = parseBackground(gameInfo, directory),
			icon = loadIcon(gameInfo, directory)
		)
	}

	fun parseBackgroundLayout(gameInfo: GameInfo, directory: File): Bitmap? {
		val fileName =
			if (gameInfo.hdBackground) "HD_BackgroundLayout.txt" else "BackgroundLayout.txt"
		var background: Bitmap? = null
		val backgroundElements = mutableListOf<BackgroundElement>()
		var file: File = directory.resolve("resource").resolve(fileName)

		if (!file.exists()) {
			file = directory.parentFile
				?.resolve(gameInfo.baseDir)
				?.resolve("resource")
				?.resolve(fileName) ?: return null
		}

		try {
			file.forEachLine { line ->
				val trimmedLine = line.trim()
				if (trimmedLine.isEmpty() || trimmedLine.startsWith("//")) {
					return@forEachLine
				}

				val parts = trimmedLine.split("\\s+".toRegex())
				if (parts[0] == "resolution") {
					background = createBitmap(parts[1].toInt(), parts[2].toInt())
				} else {
					backgroundElements.add(
						BackgroundElement(
							parts[0], parts[2].toInt(), parts[3].toInt()
						)
					)
				}
			}

			// stitch the background!
			background?.let { bg ->
				val canvas = Canvas(bg)

				for (element in backgroundElements) {
					bitmapFromTga(directory.resolve(element.path))?.let {
						canvas.drawBitmap(
							it,
							element.x.toFloat(),
							element.y.toFloat(),
							null
						)
						it.recycle()
					}

				}
			}
		} catch (_: Exception) {
			return background
		}

		return background
	}

	fun loadSplash(directory: File): Bitmap? {
		val file = directory.resolve("gfx").resolve("shell").resolve("splash.bmp")

		return if (file.exists()) {
			BitmapFactory.decodeFile(file.path)
		} else {
			null
		}
	}

	fun parseKeyValues(file: File): Map<String, String>? {
		if (!file.exists()) {
			return null
		}

		val keyValues = mutableMapOf<String, String>()
		try {
			file.forEachLine { line ->
				val trimmedLine = line.trim()
				if (trimmedLine.isNotBlank() && !trimmedLine.startsWith("//")) {
					val firstSpaceIndex = trimmedLine.indexOfFirst { it.isWhitespace() }

					if (firstSpaceIndex != -1) {
						val key = trimmedLine.substring(0, firstSpaceIndex).trim()
						val value = trimmedLine
							.substring(firstSpaceIndex)
							.trim()
							.removePrefix("\"")
							.removeSuffix("\"")
							.trim()

						if (key.isNotEmpty()) {
							keyValues[key] = value
						}
					}
				}
			}
		} catch (_: Exception) {
			return null
		}

		return keyValues
	}

	fun parseLibListGam(directory: File): GameInfo? {
		val keyValues = parseKeyValues(directory.resolve("liblist.gam"))

		keyValues?.let {
			return GameInfo(
				title = keyValues["game"] ?: directory.name,
				hdBackground = keyValues["hd_background"] == "1",
				icon = keyValues["icon"] ?: "game.ico",
				baseDir = "valve",
				gameDir = directory.name
			)
		}

//		return GameInfo(
//			title = directory.name,
//			hd_background = false,
//			icon = null,
//			baseDir = "valve",
//			gameDir = directory.name
//		)

		return null
	}

	fun parseGameInfoTxt(directory: File): GameInfo? {
		val keyValues = parseKeyValues(directory.resolve("gameinfo.txt"))

		keyValues?.let {
			return GameInfo(
				title = keyValues["title"] ?: directory.name,
				hdBackground = keyValues["hd_background"] == "1",
				icon = keyValues["icon"] ?: "game.ico",
				baseDir = "valve",
				gameDir = directory.name
			)
		}

		return null
	}

	fun parseBackground(gameInfo: GameInfo, directory: File): Bitmap? {
		return parseBackgroundLayout(gameInfo, directory) ?: loadSplash(directory)
	}

	fun parseGameInfo(directory: File): GameInfo? {
		return parseGameInfoTxt(directory) ?: parseLibListGam(directory)
	}

	fun loadIcon(gameInfo: GameInfo, directory: File): Bitmap? {
		return BitmapFactory.decodeFile(directory.resolve(gameInfo.icon).path)
			?: bitmapFromTga(directory.resolve(gameInfo.icon))
	}

	fun bitmapFromTga(file: File): Bitmap? {
		var bitmap: Bitmap? = null

		try {
			FileInputStream(file).use {
				val buffer = it.readBytes()
				val pixels = TGAReader.read(buffer, TGAReader.ARGB)

				val width = TGAReader.getWidth(buffer)
				val height = TGAReader.getHeight(buffer)

				bitmap = createBitmap(width, height)
				bitmap.setPixels(pixels, 0, width, 0, 0, width, height)
			}
		} catch (_: Exception) {
			return null
		}

		return bitmap
	}
}