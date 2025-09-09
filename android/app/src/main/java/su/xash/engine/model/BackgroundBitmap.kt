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

	fun createBackground(file: File): Bitmap {
		var bitmap =
			Bitmap.createBitmap(BACKGROUND_WIDTH, BACKGROUND_HEIGHT, Bitmap.Config.ARGB_8888)
		var canvas = Canvas(bitmap)
		var x: Int
		var y = 0
		var width: Int
		var height = 0

		val resourceFolder = File(file, "resource")
		var bgLayout = File(resourceFolder, "HD_BackgroundLayout.txt")
		if (!bgLayout.exists()) {
			bgLayout = File(resourceFolder, "BackgroundLayout.txt")
		}

		if (!bgLayout.exists()) {
			val dir = File(resourceFolder, "background")
			for (i in 0 until BACKGROUND_ROWS) {
				x = 0
				for (j in 0 until BACKGROUND_COLUMNS) {
					val filename = "${BACKGROUND_WIDTH}_${i + 1}_${'a' + j}_loading.tga"
					val bmpFile = File(dir, filename)
					val bmpImage = loadTga(bmpFile)

					canvas.drawBitmap(bmpImage, x.toFloat(), y.toFloat(), null)
					x += bmpImage.width
					height = bmpImage.height

				}
				y += height
			}
			return bitmap
		}

		FileInputStream(bgLayout).use { inputStream ->
			Scanner(inputStream).use { scanner ->
				while (scanner.hasNext()) {
					when (val str = scanner.next()) {
						"resolution" -> {
							width = scanner.nextInt()
							height = scanner.nextInt()
							bitmap = Bitmap.createBitmap(width, height, Bitmap.Config.ARGB_8888)
							canvas = Canvas(bitmap)
						}

						else -> {
							var bmpFile = file
							str.split("/").forEach { bmpFile = File(bmpFile, it) }
							//skip
							scanner.next()
							x = scanner.nextInt()
							y = scanner.nextInt()
							val bmp = loadTga(bmpFile)
							canvas.drawBitmap(bmp, x.toFloat(), y.toFloat(), null)
						}
					}
				}
			}
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
