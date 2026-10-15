package su.xash.engine.util

import android.content.Context
import android.graphics.Typeface
import android.util.TypedValue
import android.view.View
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView
import androidx.appcompat.R as AppcompatR
import com.google.android.material.R as MaterialR

fun monospaceTextView(ctx: Context, content: String): View {
	val pad = (16 * ctx.resources.displayMetrics.density).toInt()
	val text = TextView(ctx).apply {
		text = content
		typeface = Typeface.MONOSPACE
		setTextSize(TypedValue.COMPLEX_UNIT_SP, 12f)
		setPadding(pad, pad, pad, pad)
	}
	return ScrollView(ctx).apply { addView(text) }
}

/**
 * Dialog body: a message plus an optional scrollable monospace block.
 *
 * The message must not be passed to AlertDialog.setMessage() together with
 * this view. When both the message panel and a custom view are present, the
 * dialog layout (AlertDialogLayout) gives up on measuring the buttons first
 * and shrinking the content, so a long changelog pushes the buttons off
 * screen. Keeping the whole body in a single custom view restores that.
 */
fun dialogContentView(ctx: Context, message: String, monospaceContent: String?): View {
	val value = TypedValue()
	ctx.theme.resolveAttribute(AppcompatR.attr.dialogPreferredPadding, value, true)
	val padH = if (value.resourceId != 0) ctx.resources.getDimensionPixelSize(value.resourceId)
		else (24 * ctx.resources.displayMetrics.density).toInt()
	val padV = (8 * ctx.resources.displayMetrics.density).toInt()
	ctx.theme.resolveAttribute(MaterialR.attr.materialAlertDialogBodyTextStyle, value, true)
	return LinearLayout(ctx).apply {
		orientation = LinearLayout.VERTICAL
		addView(TextView(ctx, null, 0, value.resourceId).apply {
			text = message
			setPadding(padH, padV, padH, padV)
		})
		if (!monospaceContent.isNullOrEmpty())
			addView(monospaceTextView(ctx, monospaceContent))
	}
}
