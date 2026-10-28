package su.xash.engine.util

import android.content.Context
import android.os.Environment
import java.io.File
import java.io.IOException

object Prefs {
	const val APP_PREFERENCES = "app_preferences"
	const val GAME_PATH = "game_path"

	// Game data root used when the user has not picked a custom folder.
	val defaultGamePath: File
		get() = File(Environment.getExternalStorageDirectory(), "xash")

	// Selected game data location, or the default one
	@JvmStatic
	fun getGamePath(context: Context): File {
		val path = context.getSharedPreferences(APP_PREFERENCES, Context.MODE_PRIVATE)
			.getString(GAME_PATH, null)

		return if (path != null) File(path) else defaultGamePath
	}

	// Checks whether path matches the default game root.
	fun isDefaultGamePath(path: File): Boolean =
		path.resolved() == defaultGamePath.resolved()

	private fun File.resolved(): File = try {
		canonicalFile
	} catch (e: IOException) {
		absoluteFile
	}
}
