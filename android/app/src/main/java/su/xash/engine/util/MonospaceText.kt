package su.xash.engine.util

import android.content.Context
import android.graphics.Typeface
import android.util.TypedValue
import android.view.View
import android.widget.ScrollView
import android.widget.TextView

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
