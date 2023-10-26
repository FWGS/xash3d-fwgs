package su.xash.engine.workers

import android.content.Context
import android.net.Uri
import android.provider.DocumentsContract
import android.util.Log
import androidx.documentfile.provider.DocumentFile
import androidx.work.CoroutineWorker
import androidx.work.Worker
import androidx.work.WorkerParameters
import androidx.work.workDataOf
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import su.xash.engine.model.Game
import java.io.FileInputStream

const val KEY_FILE_URI = "KEY_FILE_URI"

class FileCopyWorker(ctx: Context, params: WorkerParameters) : CoroutineWorker(ctx, params) {
    companion object {
        const val Input = "Input"
    }

    override suspend fun doWork(): Result {
        withContext(Dispatchers.IO) {
            val fileUri = inputData.getString(KEY_FILE_URI)
            setProgress(workDataOf(Input to fileUri))

            val target = DocumentFile.fromFile(applicationContext.getExternalFilesDir(null)!!)
            val source = DocumentFile.fromTreeUri(applicationContext, Uri.parse(fileUri))

            source?.copyDirTo(applicationContext, target) ?: return@withContext Result.failure()
        }
        return Result.success()
    }
}

fun DocumentFile.copyFileTo(ctx: Context, file: DocumentFile) {
    val outFile = file.createFile("application", name!!)!!

    ctx.contentResolver.openOutputStream(outFile.uri).use { os ->
        ctx.contentResolver.openInputStream(uri).use {
            it?.copyTo(os!!)
        }
    }
}

fun DocumentFile.copyDirTo(ctx: Context, dir: DocumentFile) {
    val outDir = dir.createDirectory(name!!)!!

    listFiles().forEach {
        if (it.isDirectory) {
            it.copyDirTo(ctx, outDir)
        } else {
            it.copyFileTo(ctx, outDir)
        }
    }
}