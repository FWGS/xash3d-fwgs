package su.xash.engine.model

import org.json.JSONArray
import org.json.JSONObject
import java.io.InputStream

class ModDatabase(inputStream: InputStream) {
	val entries = mutableListOf<Entry>()

	companion object {
		const val VERSION = 1

		fun getFilename(): String {
			return "v${VERSION}.json"
		}
	}

	init {
		inputStream.bufferedReader().use {
			val jsonArray = JSONArray(it.readText())

			for (i in 0..<jsonArray.length()) {
				entries.add(Entry(jsonArray.getJSONObject(i)))
			}
		}
	}

	fun getByGameDir(gamedir: String): Entry? {
		return entries.filter { it.gamedir.equals(gamedir) }.firstOrNull()
	}


	inner class Entry(jsonObject: JSONObject) {
		var name: String? = null
		var appid: Int? = null
		var gamedir: String? = null

		// TODO Depots
		var pkgname: String? = null

		init {
			if (jsonObject.has("name")) {
				name = jsonObject.getString("name");
			}

			if (jsonObject.has("app_id")) {
				appid = jsonObject.getInt("app_id");
			}

			if (jsonObject.has("gamedir")) {
				gamedir = jsonObject.getString("gamedir");
			}

			if (jsonObject.has("package_name")) {
				pkgname = jsonObject.getString("package_name");
			}
		}
	}
}
