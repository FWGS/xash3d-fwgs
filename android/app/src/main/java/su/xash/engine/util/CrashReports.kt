package su.xash.engine.util

import android.content.Context
import android.content.Intent
import android.graphics.Typeface
import android.net.Uri
import android.util.TypedValue
import android.view.View
import android.widget.ScrollView
import android.widget.TextView
import androidx.core.content.FileProvider
import su.xash.engine.BuildConfig
import su.xash.engine.R
import java.io.File

object CrashReports {
	private const val PREFS = "crash_reports"
	private const val KEY_LAST_VERSION = "last_version_code"
	private const val MAX_AGE_MS = 30L * 24L * 60L * 60L * 1000L // 30 days

	private const val D = "9c8d9e8c97bf9988988cd1989e86"

	fun pendingFile(ctx: Context): File = File(ctx.filesDir, "crashes/crash.log")
	fun historyDir(ctx: Context): File = File(ctx.filesDir, "crashes/history")

	// wipe everything on app update; otherwise drop logs older than 30 days
	fun prune(ctx: Context) {
		val prefs = ctx.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
		val lastVersion = prefs.getInt(KEY_LAST_VERSION, -1)
		val currentVersion = BuildConfig.VERSION_CODE

		if (lastVersion != currentVersion) {
			historyDir(ctx).listFiles()?.forEach { it.delete() }
			pendingFile(ctx).delete()
			prefs.edit().putInt(KEY_LAST_VERSION, currentVersion).apply()
			return
		}

		val cutoff = System.currentTimeMillis() - MAX_AGE_MS
		historyDir(ctx).listFiles()?.forEach { f ->
			if (f.lastModified() < cutoff) f.delete()
		}
	}

	fun sendByEmail(ctx: Context, content: String) {
		val addr = D.chunked(2) { (it.toString().toInt(16) xor 0xFF).toChar() }.joinToString("")
		val intent = Intent(Intent.ACTION_SENDTO).apply {
			data = Uri.fromParts("mailto", addr, null)
			putExtra(Intent.EXTRA_SUBJECT, ctx.getString(R.string.crash_email_subject))
			putExtra(Intent.EXTRA_TEXT, content)
		}
		if (intent.resolveActivity(ctx.packageManager) != null) {
			ctx.startActivity(intent)
		}
	}

	fun buildContentView(ctx: Context, content: String): View {
		val pad = (16 * ctx.resources.displayMetrics.density).toInt()
		val text = TextView(ctx).apply {
			text = content
			typeface = Typeface.MONOSPACE
			setTextIsSelectable(true)
			setTextSize(TypedValue.COMPLEX_UNIT_SP, 12f)
			setPadding(pad, pad, pad, pad)
		}
		return ScrollView(ctx).apply { addView(text) }
	}

	fun share(ctx: Context, file: File) {
		val authority = "${BuildConfig.APPLICATION_ID}.fileprovider"
		val uri = FileProvider.getUriForFile(ctx, authority, file)
		val intent = Intent(Intent.ACTION_SEND).apply {
			type = "text/plain"
			putExtra(Intent.EXTRA_SUBJECT, ctx.getString(R.string.crash_email_subject))
			putExtra(Intent.EXTRA_STREAM, uri)
			addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
		}
		ctx.startActivity(Intent.createChooser(intent, ctx.getString(R.string.crash_share)))
	}
}
