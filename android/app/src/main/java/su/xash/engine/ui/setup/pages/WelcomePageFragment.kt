package su.xash.engine.ui.setup.pages

import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import androidx.fragment.app.Fragment
import androidx.fragment.app.activityViewModels
import androidx.viewbinding.ViewBinding
import su.xash.engine.databinding.PageLocationBinding
import su.xash.engine.databinding.PageWelcomeBinding
import su.xash.engine.ui.setup.SetupFragment
import su.xash.engine.ui.setup.SetupViewModel

class WelcomePageFragment : Fragment() {
    private var _binding: PageWelcomeBinding? = null
    private val binding get() = _binding!!
    private val setupViewModel: SetupViewModel by activityViewModels()

    override fun onCreateView(
        inflater: LayoutInflater, container: ViewGroup?, savedInstanceState: Bundle?
    ): View? {
        _binding = PageWelcomeBinding.inflate(inflater, container, false)
        return binding.root
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        binding.pageButton.setOnClickListener {
            setupViewModel.setPageNumber(1)
        }
        setupViewModel.setPageNumber(0)
    }

    override fun onDestroyView() {
        super.onDestroyView()
        _binding = null
    }
}