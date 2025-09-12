package su.xash.engine.ui

import android.graphics.Color
import android.os.Bundle
import androidx.activity.SystemBarStyle
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.appcompat.app.AppCompatActivity
import dagger.hilt.android.AndroidEntryPoint
import su.xash.engine.ui.theme.AppTheme

@AndroidEntryPoint
class MainActivity : AppCompatActivity() {
	override fun onCreate(savedInstanceState: Bundle?) {
		super.onCreate(savedInstanceState)

		enableEdgeToEdge(
			statusBarStyle = SystemBarStyle.dark(Color.TRANSPARENT),
			navigationBarStyle = SystemBarStyle.dark(Color.argb(0x80, 0x1b, 0x1b, 0x1b))
		)

		setContent {
			AppTheme {
				LauncherApp()
			}
		}
	}
}