package su.xash.engine.model

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.pm.PackageInstaller
import android.os.Build
import android.util.Log
import android.widget.Toast
import su.xash.engine.R

class InstallStatusReceiver : BroadcastReceiver() {
	override fun onReceive(context: Context, intent: Intent) {
		val status = intent.getIntExtra(PackageInstaller.EXTRA_STATUS, -1)
		when (status) {
			PackageInstaller.STATUS_PENDING_USER_ACTION -> {
				val confirm: Intent? = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
					intent.getParcelableExtra(Intent.EXTRA_INTENT, Intent::class.java)
				} else {
					@Suppress("DEPRECATION")
					intent.getParcelableExtra(Intent.EXTRA_INTENT)
				}
				confirm?.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
				confirm?.let { context.startActivity(it) }
			}
			PackageInstaller.STATUS_SUCCESS -> Log.i(TAG, "Install succeeded")
			else -> {
				val msg = intent.getStringExtra(PackageInstaller.EXTRA_STATUS_MESSAGE).orEmpty()
				Log.w(TAG, "Install status $status: $msg")
				Toast.makeText(
					context,
					context.getString(R.string.engine_install_failed, msg),
					Toast.LENGTH_LONG,
				).show()
			}
		}
	}

	companion object {
		private const val TAG = "InstallStatus"
	}
}
