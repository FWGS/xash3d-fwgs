package su.xash.engine.util

import android.content.Context
import android.content.Intent
import android.net.Uri
import android.os.Build
import androidx.appcompat.app.AlertDialog
import androidx.core.content.FileProvider
import su.xash.engine.BuildConfig
import su.xash.engine.R
import java.io.BufferedOutputStream
import java.io.File
import java.io.FileOutputStream
import java.util.zip.ZipEntry
import java.util.zip.ZipOutputStream

object CrashReports {
	private const val PREFS = "crash_reports"
	private const val KEY_LAST_VERSION = "last_version_code"
	private const val MAX_AGE_MS = 30L * 24L * 60L * 60L * 1000L // 30 days

	private const val D = "9c8d9e8c97bf9988988cd1989e86"

	const val STACKTRACE_NAME = "crash.log"
	const val SYSINFO_NAME = "sysinfo.txt"
	const val INTENT_NAME = "intent.txt"
	const val ENGINELOG_NAME = "engine.log"

	class Entry(val dir: File) {
		val name: String get() = dir.name
		val timestamp: Long get() = dir.lastModified()
		val stacktrace: File get() = File(dir, STACKTRACE_NAME)
		val sysinfo: File get() = File(dir, SYSINFO_NAME)
		val intent: File get() = File(dir, INTENT_NAME)
		val engineLog: File get() = File(dir, ENGINELOG_NAME)

		fun attachments(): List<File> = listOf(stacktrace, sysinfo, intent, engineLog).filter { it.exists() && it.length() > 0 }

		fun summary(): String = buildString {
			if (stacktrace.exists())
				append(stacktrace.readText())

			if (sysinfo.exists()) {
				append("\n--- System info ---\n").append(sysinfo.readText())
			}

			if (intent.exists()) {
				append("\n--- XashActivity intent ---\n").append(intent.readText())
			}
		}
	}

	fun pendingDir(ctx: Context): File = File(ctx.filesDir, "crashes")
	fun pendingStacktrace(ctx: Context): File = File(pendingDir(ctx), STACKTRACE_NAME)
	fun pendingSysinfo(ctx: Context): File = File(pendingDir(ctx), SYSINFO_NAME)
	fun pendingIntent(ctx: Context): File = File(pendingDir(ctx), INTENT_NAME)
	fun pendingEngineLog(ctx: Context): File = File(pendingDir(ctx), ENGINELOG_NAME)
	fun historyDir(ctx: Context): File = File(ctx.filesDir, "crashes/history")

	// wipe everything on app update; otherwise drop logs older than 30 days
	fun prune(ctx: Context) {
		val prefs = ctx.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
		val lastVersion = prefs.getInt(KEY_LAST_VERSION, -1)
		val currentVersion = BuildConfig.VERSION_CODE

		if (lastVersion != currentVersion) {
			historyDir(ctx).deleteRecursively()
			pendingStacktrace(ctx).delete()
			pendingSysinfo(ctx).delete()
			pendingIntent(ctx).delete()
			pendingEngineLog(ctx).delete()
			prefs.edit().putInt(KEY_LAST_VERSION, currentVersion).apply()
			return
		}

		val cutoff = System.currentTimeMillis() - MAX_AGE_MS
		historyDir(ctx).listFiles()?.forEach { entry ->
			if (entry.lastModified() < cutoff)
				entry.deleteRecursively()
		}
	}

	@JvmStatic
	fun writeSystemInfo(ctx: Context) {
		val text = buildString {
			append("App version: ").append(BuildConfig.VERSION_NAME).append(" (code ").append(BuildConfig.VERSION_CODE).append(")\n")
			append("Application ID: ").append(BuildConfig.APPLICATION_ID).append('\n')
			append("Android: ").append(Build.VERSION.RELEASE).append(" (SDK ").append(Build.VERSION.SDK_INT).append(")\n")
			append("Manufacturer: ").append(Build.MANUFACTURER).append('\n')
			append("Brand: ").append(Build.BRAND).append('\n')
			append("Model: ").append(Build.MODEL).append('\n')
			append("Device: ").append(Build.DEVICE).append('\n')
			append("Product: ").append(Build.PRODUCT).append('\n')
			append("Hardware: ").append(Build.HARDWARE).append('\n')
			append("Fingerprint: ").append(Build.FINGERPRINT).append('\n')
			append("Supported ABIs: ").append(Build.SUPPORTED_ABIS.joinToString(", ")).append('\n')
		}

		runCatching {
			pendingDir(ctx).mkdirs()
			pendingSysinfo(ctx).writeText(text)
		}
	}

	@JvmStatic
	fun writeIntentInfo(ctx: Context, text: String) {
		runCatching {
			pendingDir(ctx).mkdirs()
			pendingIntent(ctx).writeText(text)
		}
	}

	private fun zipUri(ctx: Context, entry: Entry): Uri {
		val zipDir = File(ctx.cacheDir, "crashes").apply { mkdirs() }
		val zip = File(zipDir, "${entry.name}.zip")
		ZipOutputStream(BufferedOutputStream(FileOutputStream(zip))).use { zos ->
			entry.attachments().forEach { f ->
				zos.putNextEntry(ZipEntry(f.name))
				f.inputStream().use { it.copyTo(zos) }
				zos.closeEntry()
			}
		}
		val authority = "${BuildConfig.APPLICATION_ID}.fileprovider"
		return FileProvider.getUriForFile(ctx, authority, zip)
	}

	fun sendByEmail(ctx: Context, entry: Entry) {
		val addr = D.chunked(2) { (it.toString().toInt(16) xor 0xFF).toChar() }.joinToString("")

		val mailtoProbe = Intent(Intent.ACTION_SENDTO, Uri.fromParts("mailto", addr, null))
		val mailApps = ctx.packageManager.queryIntentActivities(mailtoProbe, 0)
		if (mailApps.isEmpty()) {
			AlertDialog.Builder(ctx)
				.setTitle(R.string.crash_no_mail_app_title)
				.setMessage(R.string.crash_no_mail_app_message)
				.setPositiveButton(android.R.string.ok, null)
				.show()
			return
		}

		val uri = zipUri(ctx, entry)
		val baseSend = Intent(Intent.ACTION_SEND).apply {
			type = "application/zip"
			putExtra(Intent.EXTRA_EMAIL, arrayOf(addr))
			putExtra(Intent.EXTRA_SUBJECT, ctx.getString(R.string.crash_email_subject))
			putExtra(Intent.EXTRA_TEXT, ctx.getString(R.string.crash_email_body))
			putExtra(Intent.EXTRA_STREAM, uri)
			addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
		}

		val targeted = mailApps.map { ri ->
			Intent(baseSend).apply { setPackage(ri.activityInfo.packageName) }
		}

		if (targeted.size == 1) {
			ctx.startActivity(targeted[0])
			return
		}

		// Multiple mail apps — show a chooser limited to them, no other share targets
		val chooser = Intent.createChooser(targeted[0], ctx.getString(R.string.crash_send_to_developers))
		chooser.putExtra(Intent.EXTRA_INITIAL_INTENTS, targeted.drop(1).toTypedArray())
		ctx.startActivity(chooser)
	}

	fun share(ctx: Context, entry: Entry) {
		val uri = zipUri(ctx, entry)
		val intent = Intent(Intent.ACTION_SEND).apply {
			type = "application/zip"
			putExtra(Intent.EXTRA_SUBJECT, ctx.getString(R.string.crash_email_subject))
			putExtra(Intent.EXTRA_STREAM, uri)
			addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
		}
		ctx.startActivity(Intent.createChooser(intent, ctx.getString(R.string.crash_share)))
	}
}
