package su.xash.engine.ui.setup.pages

import android.content.Intent
import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import androidx.activity.result.contract.ActivityResultContracts
import androidx.fragment.app.Fragment
import androidx.fragment.app.activityViewModels
import androidx.navigation.fragment.findNavController
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import su.xash.engine.R
import su.xash.engine.databinding.PageLocationBinding
import su.xash.engine.ui.library.LibraryViewModel
import su.xash.engine.ui.setup.SetupViewModel

class LocationPageFragment : Fragment() {
    private var _binding: PageLocationBinding? = null
    private val binding get() = _binding!!
    private val setupViewModel: SetupViewModel by activityViewModels()
    private val libraryViewModel: LibraryViewModel by activityViewModels()

    override fun onCreateView(
        inflater: LayoutInflater, container: ViewGroup?, savedInstanceState: Bundle?
    ): View? {
        _binding = PageLocationBinding.inflate(inflater, container, false)
        return binding.root
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        binding.pageButton.setOnClickListener {
            getGamesDirectory.launch(null)
        }
    }

    override fun onDestroyView() {
        super.onDestroyView()
        _binding = null
    }

    private val getGamesDirectory =
        registerForActivityResult(ActivityResultContracts.OpenDocumentTree()) {
            it?.let {
                if (!setupViewModel.checkIfGameDir(it)) {
                    MaterialAlertDialogBuilder(requireContext()).apply {
                        setTitle(R.string.error)
                        setMessage(R.string.setup_location_empty)
                        setPositiveButton(R.string.ok) { dialog, _ -> dialog.dismiss() }
                        show()
                    }
                } else {
                    requireContext().contentResolver.takePersistableUriPermission(it, Intent.FLAG_GRANT_READ_URI_PERMISSION)
                    libraryViewModel.installGame(it)
                    findNavController().navigate(R.id.action_setupFragment_to_libraryFragment)
                }
            }
        }
}
