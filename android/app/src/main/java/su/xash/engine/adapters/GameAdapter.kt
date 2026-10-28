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

	fun gameAt(position: Int): Game = getItem(position)

	// Different game data locations may contain games with the same directory
	// names, so compare full paths to avoid reusing cards with stale click handlers.
	private class DiffCallback : DiffUtil.ItemCallback<Game>() {
		override fun areItemsTheSame(oldItem: Game, newItem: Game): Boolean {
			return oldItem.basedir == newItem.basedir
		}

		override fun areContentsTheSame(oldItem: Game, newItem: Game): Boolean {
			return oldItem.basedir == newItem.basedir &&
				oldItem.title == newItem.title &&
				oldItem.iconSignature == newItem.iconSignature &&
				oldItem.coverSignature == newItem.coverSignature &&
				(oldItem.icon == null) == (newItem.icon == null) &&
				(oldItem.cover == null) == (newItem.cover == null)
		}

	}

	inner class GameViewHolder(val binding: CardGameBinding) :
		RecyclerView.ViewHolder(binding.root) {
		fun bind(game: Game) {
			binding.apply {
				gameTitle.text = game.title

				if (game.icon != null) {
					gameIcon.visibility = View.VISIBLE
					gameIcon.setImageBitmap(game.icon)
				} else {
					gameIcon.visibility = View.GONE
				}

				if (game.cover != null) {
					gameCover.visibility = View.VISIBLE
					gameCover.setImageBitmap(game.cover)
				} else {
					gameCover.visibility = View.GONE
				}

				settingsButton.setOnClickListener {
					val selected = currentGame() ?: return@setOnClickListener
					libraryViewModel.setSelectedGame(selected)

					it.findNavController()
						.navigate(R.id.action_libraryFragment_to_gameSettingsFragment)
				}

				root.setOnClickListener { launchEngine() }
				launchButton.setOnClickListener { launchEngine() }
			}
		}

		private fun launchEngine() {
			val game = currentGame()

			if (game != null) {
				libraryViewModel.startEngine(itemView.context, game)
			}
		}

		private fun currentGame(): Game? {
			val position = bindingAdapterPosition

			if (position == RecyclerView.NO_POSITION) {
				return null
			}

			return gameAt(position)
		}
	}
}
