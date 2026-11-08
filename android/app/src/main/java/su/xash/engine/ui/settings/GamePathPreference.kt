package su.xash.engine.ui.settings

import android.content.Context
import android.util.AttributeSet
import android.view.View
import androidx.preference.Preference
import androidx.preference.PreferenceViewHolder
import su.xash.engine.R

class GamePathPreference(context: Context, attrs: AttributeSet?) : Preference(context, attrs) {
	var onResetClickListener: (() -> Unit)? = null

	var isResetEnabled = false
		set(value) {
			field = value
			notifyChanged()
		}

	override fun onBindViewHolder(holder: PreferenceViewHolder) {
		super.onBindViewHolder(holder)

		val resetButton: View? = holder.itemView.findViewById(R.id.game_path_reset)
		resetButton?.apply {
			visibility = if (isResetEnabled) View.VISIBLE else View.GONE
			setOnClickListener { onResetClickListener?.invoke() }
		}
	}
}
