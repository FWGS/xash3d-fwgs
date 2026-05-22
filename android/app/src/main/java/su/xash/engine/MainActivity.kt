package su.xash.engine

import android.os.Bundle
import androidx.appcompat.app.AppCompatActivity
import androidx.navigation.NavController
import androidx.navigation.fragment.NavHostFragment
import androidx.navigation.ui.AppBarConfiguration
import androidx.navigation.ui.navigateUp
import androidx.navigation.ui.setupActionBarWithNavController
import su.xash.engine.databinding.ActivityMainBinding
import android.content.SharedPreferences
import android.preference.PreferenceManager

class MainActivity : AppCompatActivity() {
    private lateinit var binding: ActivityMainBinding
    private lateinit var appBarConfiguration: AppBarConfiguration
    private lateinit var navController: NavController
    private lateinit var preferences: SharedPreferences

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        preferences = PreferenceManager.getDefaultSharedPreferences(this)

        binding = ActivityMainBinding.inflate(layoutInflater)
        setContentView(binding.root)

        setSupportActionBar(binding.toolbar)

        val navHostFragment =
                supportFragmentManager.findFragmentById(R.id.fragmentContainerView) as NavHostFragment
        navController = navHostFragment.navController
        appBarConfiguration = AppBarConfiguration(navController.graph)
        setupActionBarWithNavController(navController, appBarConfiguration)
    }

    fun getStoragePath(): String {
        val useInternalStorage = preferences.getBoolean("storage_toggle", false)
        
        return if (useInternalStorage) {
            getExternalFilesDir(null)?.absolutePath ?: "/storage/emulated/0/Android/data/su.xash.engine.test/files"
        } else {
            "/storage/emulated/0/xash"
        }
    }

    fun getStorageSummary(): String {
        val useInternalStorage = preferences.getBoolean("storage_toggle", false)
        
        return if (useInternalStorage) {
            "Internal Storage\n${getExternalFilesDir(null)?.absolutePath ?: "Android/data"}"
        } else {
            "External Storage\n/storage/emulated/0/xash"
        }
    }

    fun isUsingInternalStorage(): Boolean {
        return preferences.getBoolean("storage_toggle", false)
    }

    override fun onSupportNavigateUp(): Boolean {
        return navController.navigateUp(appBarConfiguration) || super.onSupportNavigateUp()
    }
}
