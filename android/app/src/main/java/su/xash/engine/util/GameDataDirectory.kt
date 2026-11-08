package su.xash.engine.util

import android.Manifest
import android.content.Context
import android.content.pm.PackageManager
import android.net.Uri
import android.os.Build
import android.os.Environment
import android.os.storage.StorageManager
import android.provider.DocumentsContract
import android.util.Log
import androidx.core.content.ContextCompat
import su.xash.engine.R
import java.io.File
import java.io.IOException

object GameDataDirectory {
	private const val TAG = "GameDataDirectory"
	private const val EXTERNAL_STORAGE_AUTHORITY = "com.android.externalstorage.documents"
	private const val PRIMARY_VOLUME_ID = "primary"
	private const val HOME_ROOT_ID = "home"
	private const val HOME_DIRECTORY = "Documents"

	// Media directories and everything inside them cannot be game roots,
	// as .nomedia would hide their contents from media apps.
	private val STANDARD_MEDIA_DIRECTORIES = setOf(
		"alarms", "audiobooks", "dcim", "download", "movies", "music",
		"notifications", "pictures", "podcasts", "recordings", "ringtones"
	)

	sealed class Result {
		data class Success(val directory: File) : Result()

		sealed class Failure(
			val messageRes: Int,
			val formatArgs: List<Any> = emptyList()
		) : Result() {
			object NotLocalStorage :
				Failure(R.string.game_data_location_not_local_storage)

			object InvalidPath :
				Failure(R.string.game_data_location_invalid_path)

			object VolumeRoot :
				Failure(R.string.game_data_location_volume_root)

			object StorageAccessDenied : Failure(
				if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R)
					R.string.file_access_message
				else
					R.string.external_storage_message
			)

			object VolumeUnavailable :
				Failure(R.string.game_data_location_volume_unavailable)

			object NotADirectory :
				Failure(R.string.game_data_location_not_a_directory)

			object ReadOnly :
				Failure(R.string.game_data_location_read_only)

			object MediaDirectory :
				Failure(R.string.game_data_location_media_directory)

			// Reject selected directories outside the app-specific directory
			class RemovableStorageRestricted(appDirectory: File) : Failure(
				R.string.game_data_location_removable_restricted,
				listOf(appDirectory.path)
			)
		}
	}

	fun resolve(context: Context, treeUri: Uri): Result {
		if (treeUri.scheme != "content" || treeUri.authority != EXTERNAL_STORAGE_AUTHORITY)
			return Result.Failure.NotLocalStorage

		// Without it the checks below would report a perfectly fine folder as
		// read-only.
		if (!hasStorageAccess(context))
			return Result.Failure.StorageAccessDenied

		val documentId = try {
			DocumentsContract.getTreeDocumentId(treeUri)
		} catch (e: IllegalArgumentException) {
			Log.w(TAG, "Malformed tree uri: $treeUri", e)
			return Result.Failure.InvalidPath
		}

		val separator = documentId.indexOf(':')
		if (separator <= 0)
			return Result.Failure.InvalidPath

		var volumeId = documentId.substring(0, separator)
		var relativePath = documentId.substring(separator + 1).trim('/')

		// The "Documents" entry of the system picker is not a volume, its
		// document ids are relative to the Documents folder of the primary one.
		if (volumeId == HOME_ROOT_ID) {
			volumeId = PRIMARY_VOLUME_ID
			relativePath = if (relativePath.isEmpty()) HOME_DIRECTORY else "$HOME_DIRECTORY/$relativePath"
		}

		val volumeRoot = try {
			findVolumeRoot(context, volumeId)?.canonicalFile
		} catch (e: IOException) {
			Log.w(TAG, "Failed to canonicalize the root of volume \"$volumeId\"", e)
			null
		} ?: return Result.Failure.VolumeUnavailable

		if (!volumeRoot.isDirectory)
			return Result.Failure.VolumeUnavailable

		// Restrict secondary volumes to the app-specific files directory before Android 11
		val restrictedDirectory = if (
			Build.VERSION.SDK_INT < Build.VERSION_CODES.R &&
			!volumeId.equals(PRIMARY_VOLUME_ID, ignoreCase = true)
		) {
			try {
				appFilesDirectory(context, volumeRoot)
			} catch (e: IOException) {
				Log.w(TAG, "Failed to canonicalize the app directory of \"$volumeRoot\"", e)
				return Result.Failure.InvalidPath
			}
		} else {
			null
		}

		if (relativePath.isEmpty()) {
			return if (restrictedDirectory != null)
				Result.Failure.RemovableStorageRestricted(restrictedDirectory)
			else
				Result.Failure.VolumeRoot
		}
		if (relativePath.split('/').any { it.isEmpty() || it == "." || it == ".." })
			return Result.Failure.InvalidPath

		val directory = try {
			File(volumeRoot, relativePath).canonicalFile
		} catch (e: IOException) {
			Log.w(TAG, "Failed to canonicalize \"$volumeRoot/$relativePath\"", e)
			return Result.Failure.InvalidPath
		}

		// Defensive check: canonicalization must keep us inside the volume.
		if (!directory.path.startsWith(volumeRoot.path + File.separator))
			return Result.Failure.InvalidPath

		if (!directory.isDirectory || !directory.canRead())
			return Result.Failure.NotADirectory

		if (restrictedDirectory != null &&
			directory != restrictedDirectory &&
			!directory.path.startsWith(restrictedDirectory.path + File.separator)
		)
			return Result.Failure.RemovableStorageRestricted(restrictedDirectory)

		// Use the resolved path, not the document id
		val topLevelName = directory.path
			.substring(volumeRoot.path.length + 1)
			.substringBefore(File.separatorChar)
			.lowercase()
		if (topLevelName in STANDARD_MEDIA_DIRECTORIES)
			return Result.Failure.MediaDirectory

		if (!canWriteTo(directory))
			return Result.Failure.ReadOnly

		return Result.Success(directory)
	}

	// App files directory on the volume, the only place to write on removable storage
	private fun appFilesDirectory(context: Context, volumeRoot: File): File {
		val directory = context.getExternalFilesDirs(null).filterNotNull().firstOrNull {
			it.canonicalFile.path.startsWith(volumeRoot.path + File.separator)
		} ?: File(volumeRoot, "Android/data/${context.packageName}/files")

		return directory.canonicalFile
	}

	// canWrite() doesn't say what the filesystem really allows, so write a file
	private fun canWriteTo(directory: File): Boolean {
		var probe: File? = null

		return try {
			probe = File.createTempFile(".write_test", null, directory)
			probe.writeBytes(ByteArray(1))
			true
		} catch (e: IOException) {
			false
		} catch (e: SecurityException) {
			false
		} finally {
			if (probe != null && !probe.delete())
				Log.w(TAG, "Failed to delete ${probe.path}")
		}
	}

	private fun hasStorageAccess(context: Context): Boolean {
		if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R)
			return Environment.isExternalStorageManager()

		return ContextCompat.checkSelfPermission(
			context, Manifest.permission.WRITE_EXTERNAL_STORAGE
		) == PackageManager.PERMISSION_GRANTED
	}

	private fun findVolumeRoot(context: Context, volumeId: String): File? {
		if (volumeId.equals(PRIMARY_VOLUME_ID, ignoreCase = true))
			return Environment.getExternalStorageDirectory()

		if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
			val manager = context.getSystemService(StorageManager::class.java) ?: return null
			val volume = manager.storageVolumes.firstOrNull {
				it.uuid?.equals(volumeId, ignoreCase = true) == true
			} ?: return null
			if (volume.state != Environment.MEDIA_MOUNTED)
				return null
			return volume.directory
		}

		// No StorageVolume.getDirectory() before Android 11, use the app directories
		val marker = "/Android/data/"
		val mounted = context.getExternalFilesDirs(null).filterNotNull().mapNotNull {
			val index = it.path.indexOf(marker)
			if (index > 0) File(it.path.substring(0, index)) else null
		}.firstOrNull { it.name.equals(volumeId, ignoreCase = true) }

		return mounted ?: File("/storage", volumeId)
	}
}
