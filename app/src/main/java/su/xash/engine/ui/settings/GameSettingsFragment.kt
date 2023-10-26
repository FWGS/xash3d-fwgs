package su.xash.engine.ui.settings

import android.content.Intent
import android.os.Bundle
import android.view.LayoutInflater
import android.view.Menu
import android.view.MenuInflater
import android.view.MenuItem
import android.view.View
import android.view.ViewGroup
import androidx.core.view.MenuProvider
import androidx.core.view.get
import androidx.fragment.app.Fragment
import androidx.fragment.app.activityViewModels
import androidx.lifecycle.lifecycleScope
import androidx.lifecycle.viewModelScope
import androidx.navigation.fragment.findNavController
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import su.xash.engine.R
import su.xash.engine.databinding.FragmentGameSettingsBinding
import su.xash.engine.ui.library.LibraryViewModel

class GameSettingsFragment : Fragment() {
    private var _binding: FragmentGameSettingsBinding? = null
    private val binding get() = _binding!!

    private val libraryViewModel: LibraryViewModel by activityViewModels()

    override fun onCreateView(
        inflater: LayoutInflater, container: ViewGroup?, savedInstanceState: Bundle?
    ): View? {
        _binding = FragmentGameSettingsBinding.inflate(inflater, container, false)
        return binding.root
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        super.onViewCreated(view, savedInstanceState)

        val game = libraryViewModel.selectedItem.value!!

        binding.gameCard.apply {
            gameTitle.text = game.title

            if (game.icon != null) {
                gameIcon.setImageBitmap(game.icon)
            } else {
                gameIcon.visibility = View.GONE
            }

            if (game.cover != null) {
                gameCover.setImageBitmap(game.cover)
            } else {
                gameCover.visibility = View.GONE
            }

            buttonsContainer.visibility = View.GONE
        }

        childFragmentManager.beginTransaction()
            .add(binding.settingsFragment.id, GameSettingsPreferenceFragment(game))
            .commit();

        binding.bottomNavigation.menu.findItem(R.id.action_uninstall).setOnMenuItemClickListener {
            libraryViewModel.uninstallGame(game)
            findNavController().popBackStack()
        }
    }

    override fun onDestroyView() {
        super.onDestroyView()
        _binding = null
    }
}