package su.xash.engine

import android.app.Application
import android.content.Context
import android.os.StrictMode
import org.acra.data.StringFormat
import org.acra.ktx.initAcra

class MainApplication : Application() {
	override fun attachBaseContext(base: Context?) {
		super.attachBaseContext(base)

		if (!BuildConfig.DEBUG) {
            initAcra {
                buildConfigClass = BuildConfig::class.java
                reportFormat = StringFormat.JSON

//                httpSender {
//                    uri = "http://bodis.pp.ua:5000/report"
//                }
            }
		} else {
			// enable strict mode to detect memory leaks etc.
			StrictMode.enableDefaults();
		}
	}
}
