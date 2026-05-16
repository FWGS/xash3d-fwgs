package su.xash.engine.model

import android.app.PendingIntent
import android.content.Context
import android.content.Intent
import android.content.pm.PackageInstaller
import android.os.Build
import android.util.Log
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.ensureActive
import kotlinx.coroutines.withContext
import org.json.JSONException
import org.json.JSONObject
import su.xash.engine.BuildConfig
import java.io.File
import java.io.FileOutputStream
import java.io.IOException
import java.net.HttpURLConnection
import java.net.URL
import kotlin.coroutines.coroutineContext

class AppUpdater(private val context: Context) {

	data class UpdateInfo(val buildNum: Int, val tagName: String)
	data class CommitInfo(val sha: String, val subject: String)

	fun canInstall(): Boolean =
		Build.VERSION.SDK_INT < Build.VERSION_CODES.O ||
			context.packageManager.canRequestPackageInstalls()

	suspend fun checkForUpdate(): UpdateInfo? {
		if (!BuildConfig.ENABLE_AUTO_UPDATE) return null
		return withContext(Dispatchers.IO) {
			var connection: HttpURLConnection? = null
			try {
				connection = URL(RELEASE_API_URL).openConnection() as HttpURLConnection
				connection.connectTimeout = 5000
				connection.readTimeout = 5000
				connection.setRequestProperty("Accept", "application/vnd.github+json")
				connection.connect()

				if (connection.responseCode != HttpURLConnection.HTTP_OK) {
					Log.w(TAG, "Release API check failed: HTTP ${connection.responseCode}")
					return@withContext null
				}

				val release = JSONObject(connection.inputStream.bufferedReader().readText())
				val body = release.optString("body", "")
				val tagName = release.optString("tag_name").ifEmpty { TAG_CONTINUOUS }

				// buildnum is days-since-2015-04-01, same metric as VERSION_CODE / 10000
				val remote = BUILDNUM_REGEX.find(body)?.groupValues?.get(1)?.toIntOrNull()
				val localDays = BuildConfig.VERSION_CODE / 10000
				Log.i(TAG, "Remote buildnum: $remote (tag=$tagName), local: $localDays")

				if (remote != null && remote - localDays >= STALENESS_DAYS)
					UpdateInfo(remote, tagName)
				else
					null
			} catch (e: CancellationException) {
				throw e
			} catch (e: IOException) {
				Log.w(TAG, "Update check failed: ${e.message}")
				null
			} catch (e: JSONException) {
				Log.w(TAG, "Update check parse failed: ${e.message}")
				null
			} finally {
				connection?.disconnect()
			}
		}
	}

	suspend fun fetchChangelog(fromRef: String, toRef: String): List<CommitInfo>? {
		if (fromRef.isEmpty() || toRef.isEmpty())
			return null
		return withContext(Dispatchers.IO) {
			var connection: HttpURLConnection? = null
			try {
				connection = URL("$COMPARE_API_BASE/$fromRef...$toRef").openConnection() as HttpURLConnection
				connection.connectTimeout = 5000
				connection.readTimeout = 5000
				connection.setRequestProperty("Accept", "application/vnd.github+json")
				connection.connect()

				if (connection.responseCode != HttpURLConnection.HTTP_OK) {
					Log.w(TAG, "Compare API failed: HTTP ${connection.responseCode}")
					return@withContext null
				}

				val json = JSONObject(connection.inputStream.bufferedReader().readText())
				val commits = json.optJSONArray("commits") ?: return@withContext null
				val result = ArrayList<CommitInfo>(commits.length())
				for (i in 0 until commits.length()) {
					val c = commits.getJSONObject(i)
					val sha = c.optString("sha").ifEmpty { continue }
					val msg = c.optJSONObject("commit")?.optString("message") ?: continue
					val subject = msg.substringBefore('\n').trim()
					if (subject.isNotEmpty())
						result.add(CommitInfo(sha, subject))
				}
				// GitHub returns oldest first
				result.reverse()
				result
			} catch (e: CancellationException) {
				throw e
			} catch (e: IOException) {
				Log.w(TAG, "Changelog fetch failed: ${e.message}")
				null
			} catch (e: JSONException) {
				Log.w(TAG, "Changelog parse failed: ${e.message}")
				null
			} finally {
				connection?.disconnect()
			}
		}
	}

	suspend fun downloadAndInstall(onProgress: (Long, Long) -> Unit): Result<Unit> {
		return withContext(Dispatchers.IO) {
			val tempFile = File(context.cacheDir, "xash3d-fwgs-update.apk")
			var connection: HttpURLConnection? = null
			try {
				connection = URL(APK_URL).openConnection() as HttpURLConnection
				connection.connectTimeout = 10000
				connection.readTimeout = 30000
				connection.instanceFollowRedirects = true
				connection.connect()

				if (connection.responseCode != HttpURLConnection.HTTP_OK)
					return@withContext Result.failure(IOException("HTTP ${connection.responseCode}"))

				val total = connection.contentLengthLong
				var downloaded = 0L
				var lastEmit = 0L

				connection.inputStream.use { input ->
					FileOutputStream(tempFile).use { output ->
						val buffer = ByteArray(65536)
						while (true) {
							coroutineContext.ensureActive()
							val read = input.read(buffer)
							if (read < 0)
								break
							output.write(buffer, 0, read)
							downloaded += read
							val now = System.currentTimeMillis()
							if (now - lastEmit >= PROGRESS_INTERVAL_MS) {
								lastEmit = now
								withContext(Dispatchers.Main) { onProgress(downloaded, total) }
							}
						}
					}
				}
				withContext(Dispatchers.Main) { onProgress(downloaded, total) }

				Log.i(TAG, "Downloaded APK: ${tempFile.length()} bytes -> ${tempFile.absolutePath}")

				triggerInstall(tempFile)
				Result.success(Unit)
			} catch (e: CancellationException) {
				tempFile.delete()
				throw e
			} catch (e: IOException) {
				tempFile.delete()
				Result.failure(e)
			} finally {
				connection?.disconnect()
			}
		}
	}

	private fun triggerInstall(apk: File) {
		val installer = context.packageManager.packageInstaller
		val params = PackageInstaller.SessionParams(PackageInstaller.SessionParams.MODE_FULL_INSTALL)
		val sessionId = installer.createSession(params)
		installer.openSession(sessionId).use { session ->
			session.openWrite("base.apk", 0, apk.length()).use { out ->
				apk.inputStream().use { it.copyTo(out) }
				session.fsync(out)
			}
			val statusIntent = Intent(INSTALL_ACTION).setPackage(context.packageName)
			val piFlags = PendingIntent.FLAG_UPDATE_CURRENT or
				if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) PendingIntent.FLAG_MUTABLE else 0
			val pi = PendingIntent.getBroadcast(context, sessionId, statusIntent, piFlags)
			session.commit(pi.intentSender)
		}
	}

	companion object {
		private const val TAG = "AppUpdater"
		private const val STALENESS_DAYS = 3
		private const val PROGRESS_INTERVAL_MS = 100L
		private const val TAG_CONTINUOUS = "continuous"
		private const val INSTALL_ACTION = "su.xash.engine.INSTALL_RESULT"
		private const val APK_URL =
			"https://github.com/FWGS/xash3d-fwgs/releases/download/continuous/xash3d-fwgs-android.apk"
		private const val RELEASE_API_URL =
			"https://api.github.com/repos/FWGS/xash3d-fwgs/releases/tags/continuous"
		private const val COMPARE_API_BASE =
			"https://api.github.com/repos/FWGS/xash3d-fwgs/compare"
		private val BUILDNUM_REGEX = Regex("""buildnum\s+(\d+)""")
	}
}
