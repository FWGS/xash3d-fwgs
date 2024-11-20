package su.xash.engine.model

import android.content.Context
import android.content.Intent
import android.content.pm.PackageInfo
import android.content.pm.PackageManager
import android.graphics.Bitmap
import android.net.Uri
import android.provider.MediaStore
import androidx.documentfile.provider.DocumentFile
import su.xash.engine.XashActivity


class Game(val ctx: Context, val basedir: DocumentFile, var installed: Boolean = true) {
    private var iconName = "game.ico"
    var title = "Unknown Game"
    var icon: Bitmap? = null
    var cover: Bitmap? = null

    private val pref = ctx.getSharedPreferences(basedir.name, Context.MODE_PRIVATE)

    init {
        basedir.findFile("gameinfo.txt")?.let {
            parseGameInfo(it)
        } ?: basedir.findFile("liblist.gam")?.let { parseGameInfo(it) }

        basedir.findFile(iconName)
            ?.let { icon = MediaStore.Images.Media.getBitmap(ctx.contentResolver, it.uri) }

        try {
            cover = BackgroundBitmap.createBackground(ctx, basedir)
        } catch (e: Exception) {
            e.printStackTrace()
        }
    }

    fun startEngine(ctx: Context) {
        ctx.startActivity(Intent(ctx, XashActivity::class.java).apply {
            flags = Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_CLEAR_TASK
            putExtra("gamedir", basedir.name)
            putExtra("argv", pref.getString("arguments", "-dev 2 -log"))
            putExtra("usevolume", pref.getBoolean("use_volume_buttons", false))
            //.putExtra("gamelibdir", getGameLibDir(context))
            //.putExtra("package", getPackageName()) }
        })
    }

    private fun parseGameInfo(file: DocumentFile) {
        ctx.contentResolver.openInputStream(file.uri).use { inputStream ->
            inputStream?.bufferedReader().use { reader ->
                reader?.forEachLine {
                    val tokens = it.split("\\s+".toRegex(), limit = 2)
                    if (tokens.size >= 2) {
                        val k = tokens[0]
                        val v = tokens[1].trim('"')

                        if (k == "title" || k == "game") title = v
                        if (k == "icon") iconName = v
                    }
                }
            }
        }
    }

    private fun getPackageName(): String? {
//        return if (mDbEntry != null) {
//            mDbEntry.getPackageName()
//        } else null
        return null
    }

    private fun getGameLibDir(ctx: Context): String? {
        val pkgName = getPackageName()
        if (pkgName != null) {
            val pkgInfo: PackageInfo = try {
                ctx.packageManager.getPackageInfo(pkgName, 0)
            } catch (e: PackageManager.NameNotFoundException) {
                e.printStackTrace()
                ctx.startActivity(
                    Intent(
                        Intent.ACTION_VIEW, Uri.parse("market://details?id=$pkgName")
                    ).setFlags(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_CLEAR_TASK)
                )
                return null
            }
            return pkgInfo.applicationInfo.nativeLibraryDir
        }
        return ctx.applicationInfo.nativeLibraryDir
    }

    companion object {
        fun getGames(ctx: Context, file: DocumentFile): List<Game> {
            val games = mutableListOf<Game>()

            if (checkIfGamedir(file)) {
                games.add(Game(ctx, file))
            } else {
                file.listFiles().forEach {
                    if (it.isDirectory) {
                        if (checkIfGamedir(it)) {
                            games.add(Game(ctx, it))
                        }
                    }
                }
            }

            return games
        }

        fun checkIfGamedir(file: DocumentFile): Boolean {
            file.findFile("liblist.gam")?.let { return true }
            file.findFile("gameinfo.txt")?.let { return true }
            return false
        }
    }
}

//    Intent intent = new Intent("su.xash.engine.MOD");
//                for (ResolveInfo info : context.getPackageManager()
//                        .queryIntentActivities(intent, PackageManager.GET_META_DATA)) {
//                        String packageName = info.activityInfo.applicationInfo.packageName;
//                        String gameDir = info.activityInfo.applicationInfo.metaData.getString(
//                        "su.xash.engine.gamedir");
//                        Log.d(TAG, "package = " + packageName + " gamedir = " + gameDir);
//                        }

//public void startEngine(Context context) {
//    context.startActivity(new Intent(context, XashActivity.class).setFlags(Intent.FLAG_ACTIVITY_CLEAR_TOP).putExtra("gamedir", getGameDir()).putExtra("argv", getArguments()).putExtra("usevolume", getVolumeState()).putExtra("gamelibdir", getGameLibDir(context)).putExtra("package", getPackageName()));
//}