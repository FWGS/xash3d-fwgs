package su.xash.engine.util

import android.content.Context
import android.text.format.Formatter
import android.view.LayoutInflater
import android.widget.TextView
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import com.google.android.material.progressindicator.LinearProgressIndicator
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.launch
import su.xash.engine.R

fun showDownloadProgressDialog(
	ctx: Context,
	titleRes: Int,
	cancelable: Boolean,
	scope: CoroutineScope,
	download: suspend ((Long, Long) -> Unit) -> Result<Unit>,
	onSuccess: (() -> Unit)? = null,
) {
	val view = LayoutInflater.from(ctx).inflate(R.layout.dialog_download_progress, null)
	val progressBar = view.findViewById<LinearProgressIndicator>(R.id.downloadProgress)
	val statusText = view.findViewById<TextView>(R.id.downloadStatus)

	val dialog = MaterialAlertDialogBuilder(ctx)
		.setTitle(titleRes)
		.setView(view)
		.setCancelable(cancelable)
		.apply {
			if (cancelable)
				setNegativeButton(android.R.string.cancel) { d, _ -> d.dismiss() }
		}
		.create()

	dialog.show()

	val job = scope.launch {
		val result = download { downloaded, total ->
			val downloadedStr = Formatter.formatShortFileSize(ctx, downloaded)
			if (total > 0) {
				progressBar.isIndeterminate = false
				progressBar.progress = (downloaded * 100 / total).toInt()
				val totalStr = Formatter.formatShortFileSize(ctx, total)
				statusText.text = "$downloadedStr / $totalStr"
			} else {
				statusText.text = ctx.getString(R.string.download_progress_unknown, downloadedStr)
			}
		}

		if (!dialog.isShowing)
			return@launch

		dialog.dismiss()

		if (result.isSuccess) {
			onSuccess?.invoke()
		} else {
			MaterialAlertDialogBuilder(ctx)
				.setTitle(R.string.download_failed)
				.setMessage(result.exceptionOrNull()?.message
					?: ctx.getString(R.string.download_error))
				.setPositiveButton(android.R.string.ok, null)
				.show()
		}
	}

	if (cancelable)
		dialog.setOnDismissListener { job.cancel() }
}
