package su.xash.engine.adapters

import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import androidx.navigation.findNavController
import androidx.recyclerview.widget.DiffUtil
import androidx.recyclerview.widget.ListAdapter
import androidx.recyclerview.widget.RecyclerView
import su.xash.engine.R
import su.xash.engine.databinding.CardGameBinding
import su.xash.engine.model.Game
import su.xash.engine.ui.library.LibraryViewModel


class GameAdapter(private val libraryViewModel: LibraryViewModel) :
	ListAdapter<Game, GameAdapter.GameViewHolder>(DiffCallback()) {

	override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): GameAdapter.GameViewHolder {
		val binding = CardGameBinding.inflate(LayoutInflater.from(parent.context), parent, false)
		return GameViewHolder(binding)
	}

	override fun onBindViewHolder(holder: GameAdapter.GameViewHolder, position: Int) {
		return holder.bind(getItem(position))
	}

	private class DiffCallback : DiffUtil.ItemCallback<Game>() {
		override fun areItemsTheSame(oldItem: Game, newItem: Game): Boolean {
			return oldItem.basedir.name == newItem.basedir.name
		}

		override fun areContentsTheSame(oldItem: Game, newItem: Game): Boolean {
			return oldItem.basedir.name == newItem.basedir.name
		}

	}

	inner class GameViewHolder(val binding: CardGameBinding) :
		RecyclerView.ViewHolder(binding.root) {
		fun bind(game: Game) {
			binding.apply {
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

				settingsButton.setOnClickListener {
					libraryViewModel.setSelectedGame(game)
					it.findNavController()
						.navigate(R.id.action_libraryFragment_to_gameSettingsFragment)
				}

				root.setOnClickListener { libraryViewModel.startEngine(it.context, game) }
				launchButton.setOnClickListener {
					libraryViewModel.startEngine(it.context, game)
				}
			}
		}
	}
}
