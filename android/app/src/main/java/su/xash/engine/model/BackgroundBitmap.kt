package su.xash.engine.model

import android.graphics.Bitmap
import android.graphics.Canvas
import su.xash.engine.util.TGAReader
import java.io.File
import java.io.FileInputStream
import java.util.Scanner


object BackgroundBitmap {
	private const val BACKGROUND_ROWS = 3
	private const val BACKGROUND_COLUMNS = 4
	private const val BACKGROUND_WIDTH = 800
	private const val BACKGROUND_HEIGHT = 600

	private class Tile(val file: File, val x: Int, val y: Int)

	private class Layout(var width: Int, var height: Int, val tiles: MutableList<Tile>)

	private fun findLayoutFile(file: File): File? {
		val resourceFolder = File(file, "resource")

		return sequenceOf("HD_BackgroundLayout.txt", "BackgroundLayout.txt")
			.map { File(resourceFolder, it) }
			.firstOrNull { it.exists() }
	}

	private fun defaultTile(file: File, row: Int, column: Int): File {
		val dir = File(File(file, "resource"), "background")

		return File(dir, "${BACKGROUND_WIDTH}_${row + 1}_${'a' + column}_loading.tga")
	}

	private fun parseLayout(file: File, bgLayout: File): Layout {
		val layout = Layout(BACKGROUND_WIDTH, BACKGROUND_HEIGHT, mutableListOf())

		FileInputStream(bgLayout).use { inputStream ->
			Scanner(inputStream).use { scanner ->
				while (scanner.hasNext()) {
					when (val str = scanner.next()) {
						"resolution" -> {
							layout.width = scanner.nextInt()
							layout.height = scanner.nextInt()
							// the bitmap is recreated, what was placed before is lost
							layout.tiles.clear()
						}

						else -> {
							var bmpFile = file
							str.split("/").forEach { bmpFile = File(bmpFile, it) }
							//skip
							scanner.next()
							val x = scanner.nextInt()
							val y = scanner.nextInt()
							layout.tiles.add(Tile(bmpFile, x, y))
						}
					}
				}
			}
		}

		return layout
	}

	// Fingerprint background source files to detect artwork changes without decoding.
	fun getBackgroundSignature(file: File): String {
		val bgLayout = findLayoutFile(file)
		val sources = if (bgLayout == null) {
			(0 until BACKGROUND_ROWS).flatMap { i ->
				(0 until BACKGROUND_COLUMNS).map { j -> defaultTile(file, i, j) }
			}
		} else {
			listOf(bgLayout) + parseLayout(file, bgLayout).tiles.map { it.file }
		}

		return sources.joinToString("|") {
			"${it.absolutePath}:${it.exists()}:${it.length()}:${it.lastModified()}"
		}
	}

	fun createBackground(file: File): Bitmap {
		val bgLayout = findLayoutFile(file)

		if (bgLayout == null) {
			val bitmap =
				Bitmap.createBitmap(BACKGROUND_WIDTH, BACKGROUND_HEIGHT, Bitmap.Config.ARGB_8888)
			val canvas = Canvas(bitmap)
			var y = 0

			for (i in 0 until BACKGROUND_ROWS) {
				var x = 0
				var height = 0

				for (j in 0 until BACKGROUND_COLUMNS) {
					val bmpImage = loadTga(defaultTile(file, i, j))

					canvas.drawBitmap(bmpImage, x.toFloat(), y.toFloat(), null)
					x += bmpImage.width
					height = bmpImage.height
				}
				y += height
			}
			return bitmap
		}

		val layout = parseLayout(file, bgLayout)
		val bitmap = Bitmap.createBitmap(layout.width, layout.height, Bitmap.Config.ARGB_8888)
		val canvas = Canvas(bitmap)

		for (tile in layout.tiles) {
			canvas.drawBitmap(loadTga(tile.file), tile.x.toFloat(), tile.y.toFloat(), null)
		}
		return bitmap
	}

	private fun loadTga(file: File): Bitmap {
		FileInputStream(file).use {
			val buffer = it.readBytes()
			val pixels = TGAReader.read(buffer, TGAReader.ARGB)

			val width = TGAReader.getWidth(buffer)
			val height = TGAReader.getHeight(buffer)

			return Bitmap.createBitmap(pixels, 0, width, width, height, Bitmap.Config.ARGB_8888)
		}
	}
}
