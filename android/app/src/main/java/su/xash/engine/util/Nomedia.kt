/*
Nomedia.kt - ensure that .nomedia file exists in directory
Copyright (C) 2025 Alibek Omarov

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/
package su.xash.engine.util
import java.io.File

object Nomedia {
	fun ensureNomedia(directory: File): Boolean {
		try {
			// ensure paths exists
			if (!directory.exists() && !directory.mkdirs()) {
				return false
			}

			// check if it's a directory
			if (!directory.isDirectory) {
				return false
			}

			val nomediaFile = File(directory, ".nomedia")

			// if the .nomedia is directory, remove it
			if (nomediaFile.isDirectory) {
				// it should be empty to be deleted, but that's good
				// because this way we're not deleting something important
				// (who would stash important files in .nomedia directory though...)
				if (!nomediaFile.delete()) {
					return false
				}
			}

			// it returns false if file already exists, which is fine for us
			nomediaFile.createNewFile()

			return true
		} catch (e: Exception) {
			e.printStackTrace()
			return false
		}
	}
}
