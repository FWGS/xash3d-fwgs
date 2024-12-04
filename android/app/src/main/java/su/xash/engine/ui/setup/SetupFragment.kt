package su.xash.engine.ui.setup

import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import androidx.fragment.app.Fragment
import androidx.fragment.app.activityViewModels
import androidx.viewpager2.adapter.FragmentStateAdapter
import su.xash.engine.databinding.FragmentSetupBinding
import su.xash.engine.ui.setup.pages.LocationPageFragment
import su.xash.engine.ui.setup.pages.WelcomePageFragment

class SetupFragment : Fragment() {
    private var _binding: FragmentSetupBinding? = null
    private val binding get() = _binding!!
    private val setupViewModel: SetupViewModel by activityViewModels()

    private lateinit var setupPageAdapter: SetupPageAdapter

    override fun onCreateView(
        inflater: LayoutInflater, container: ViewGroup?, savedInstanceState: Bundle?
    ): View? {
        _binding = FragmentSetupBinding.inflate(inflater, container, false)
        return binding.root
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        setupPageAdapter = SetupPageAdapter(this)
        binding.viewPager.isUserInputEnabled = false
        binding.viewPager.adapter = setupPageAdapter

        setupViewModel.pageNumber.observe(viewLifecycleOwner) {
            binding.viewPager.setCurrentItem(it, true)
        }
    }

    override fun onDestroyView() {
        super.onDestroyView()
        _binding = null
    }
}

class SetupPageAdapter(fragment: Fragment) : FragmentStateAdapter(fragment) {
    val pages = listOf(WelcomePageFragment(), LocationPageFragment())
    override fun getItemCount(): Int = 2
    override fun createFragment(position: Int): Fragment = pages[position]
}
