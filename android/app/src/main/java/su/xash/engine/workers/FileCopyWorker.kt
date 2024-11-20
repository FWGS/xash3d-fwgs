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
        const val Progress = "Progress"
    }

    private var fileCount = 0
    private var fileCopied = 0

    override suspend fun doWork(): Result {
        withContext(Dispatchers.IO) {
            val fileUri = inputData.getString(KEY_FILE_URI)
            setProgress(workDataOf(Input to fileUri))

            val uri = Uri.parse(fileUri)
            val source = DocumentFile.fromTreeUri(applicationContext, uri)

            fileCount = source?.countDirFiles() ?: return@withContext Result.failure()

            setProgress(workDataOf(Progress to 0f))

            val gamedir = source.name!!
            val externalFilesDir = DocumentFile.fromFile(applicationContext.getExternalFilesDir(null)!!)

            // create a directory to store staged files
            val target = externalFilesDir.createDirectory(".$gamedir")!!

            source.copyDirTo(applicationContext, this@FileCopyWorker, target)

            target.renameTo(gamedir)
        }
        return Result.success()
    }

    suspend fun fileCopied(count: Int) {
        if(count == 0)
            return

        fileCopied += count
        val percentage: Float = fileCopied.toFloat() / fileCount.toFloat();
        setProgress(workDataOf(Progress to percentage))
    }
}

fun DocumentFile.countDirFiles(): Int {
    var count: Int = 0

    listFiles().forEach {
        if (it.isDirectory)
            count += it.countDirFiles()
        else
            count++
    }

    return count
}

fun DocumentFile.copyFileTo(ctx: Context, file: DocumentFile) {
    val outFile = file.createFile("application", name!!)!!

    ctx.contentResolver.openOutputStream(outFile.uri).use { os ->
        ctx.contentResolver.openInputStream(uri).use {
            it?.copyTo(os!!)
        }
    }
}

suspend fun DocumentFile.copyDirTo(ctx: Context, worker: FileCopyWorker, dir: DocumentFile) {
    var count: Int = 0

    listFiles().forEach {
        if (it.isDirectory) {
            val outDir = dir.createDirectory(it.name!!)!!
            it.copyDirTo(ctx, worker, outDir)
        } else {
            it.copyFileTo(ctx, dir)
            count++
        }
    }

    worker.fileCopied(count)
}
