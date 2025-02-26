package su.xash.engine.ui.settings

import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import androidx.fragment.app.Fragment
import su.xash.engine.databinding.FragmentAppSettingsBinding

class AppSettingsFragment : Fragment() {
	private var _binding: FragmentAppSettingsBinding? = null
	private val binding get() = _binding!!

	override fun onCreateView(
		inflater: LayoutInflater, container: ViewGroup?, savedInstanceState: Bundle?
	): View {
		_binding = FragmentAppSettingsBinding.inflate(inflater, container, false)
		return binding.root
	}

	override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
		super.onViewCreated(view, savedInstanceState)
		childFragmentManager.beginTransaction()
			.add(binding.settingsFragment.id, AppSettingsPreferenceFragment()).commit();
	}

	override fun onDestroyView() {
		super.onDestroyView()
		_binding = null
	}
}
