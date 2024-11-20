package su.xash.engine.ui.setup

import android.app.Application
import android.net.Uri
import androidx.documentfile.provider.DocumentFile
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.LiveData
import androidx.lifecycle.MutableLiveData
import androidx.work.Data
import androidx.work.OneTimeWorkRequestBuilder
import androidx.work.WorkManager
import su.xash.engine.MainApplication
import su.xash.engine.model.Game
import su.xash.engine.workers.FileCopyWorker

class SetupViewModel(application: Application) : AndroidViewModel(application) {
    val pageNumber: LiveData<Int> get() = _pageNumber
    private val _pageNumber = MutableLiveData(0)

    fun checkIfGameDir(uri: Uri): Boolean {
        val ctx = getApplication<MainApplication>().applicationContext
        val file = DocumentFile.fromTreeUri(ctx, uri)!!
        return Game.checkIfGamedir(file)
    }

    fun setPageNumber(pos: Int) {
        _pageNumber.value = pos
    }
}