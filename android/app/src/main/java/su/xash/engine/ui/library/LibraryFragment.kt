package su.xash.engine.ui.library

import android.Manifest
import android.content.Intent
import android.content.pm.PackageManager
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.os.Environment
import android.provider.Settings
import android.view.LayoutInflater
import android.view.Menu
import android.view.MenuInflater
import android.view.MenuItem
import android.view.View
import android.view.ViewGroup
import androidx.activity.result.contract.ActivityResultContracts
import androidx.core.app.ActivityCompat
import androidx.core.content.ContextCompat
import androidx.core.view.MenuProvider
import androidx.fragment.app.Fragment
import androidx.fragment.app.activityViewModels
import androidx.lifecycle.Lifecycle
import androidx.navigation.fragment.findNavController
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import su.xash.engine.BuildConfig
import su.xash.engine.R
import su.xash.engine.adapters.GameAdapter
import su.xash.engine.databinding.FragmentLibraryBinding
import su.xash.engine.model.GameDataDownloader


class LibraryFragment : Fragment(), MenuProvider {
	private var _binding: FragmentLibraryBinding? = null
	private val binding get() = _binding!!

	private val libraryViewModel: LibraryViewModel by activityViewModels()
	private var isPermissionRequested = false
	private var hasShownFirstRunDialog = false

	private val startActivityForResult =
		registerForActivityResult(ActivityResultContracts.StartActivityForResult()) {
			isPermissionRequested = false
			if (checkStoragePermissions()) {
				libraryViewModel.reloadGames(requireContext())
			}
		}

	private val requiredPermissions = arrayOf(
		Manifest.permission.READ_EXTERNAL_STORAGE,
		Manifest.permission.WRITE_EXTERNAL_STORAGE
	)

	private val requestPermissionLauncher = registerForActivityResult(
		ActivityResultContracts.RequestMultiplePermissions()
	) { permissions ->
		val granted = permissions.entries.all { it.value }
		if (granted) {
			libraryViewModel.reloadGames(requireContext())
		}
	}

	private fun checkStoragePermissions(): Boolean {
		if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
			if (!Environment.isExternalStorageManager()) {
				if (isPermissionRequested) return false
				isPermissionRequested = true
				MaterialAlertDialogBuilder(requireContext()).apply {
					setTitle(R.string.file_access_required)
					setMessage(R.string.file_access_message)
					setPositiveButton(R.string.open_settings) { _, _ ->
						startActivityForResult.launch(
							Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION).setData(
								Uri.fromParts("package", BuildConfig.APPLICATION_ID, null)
							)
						)
					}
					setNeutralButton(android.R.string.cancel, null)
					setCancelable(false)
					show()

					return false
				}
			} else {
				return true
			}
		} else if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
			val permissionsNeeded = requiredPermissions.filter {
				ContextCompat.checkSelfPermission(
					requireContext(),
					it
				) != PackageManager.PERMISSION_GRANTED
			}.toTypedArray()

			if (!permissionsNeeded.isEmpty()) {
				if (isPermissionRequested) return false
				isPermissionRequested = true
				val showRationale = permissionsNeeded.any {
					ActivityCompat.shouldShowRequestPermissionRationale(requireActivity(), it)
				}

				MaterialAlertDialogBuilder(requireContext()).apply {
					setTitle(R.string.external_storage_required)
					setMessage(R.string.external_storage_message)
					setPositiveButton(R.string.open_settings) { _, _ ->
						if (showRationale) {
							requestPermissionLauncher.launch(permissionsNeeded)
						} else {
							val intent =
								Intent(Settings.ACTION_APPLICATION_DETAILS_SETTINGS).apply {
									data =
										Uri.fromParts("package", requireContext().packageName, null)
								}
							startActivity(intent)
						}
					}
					setNeutralButton(android.R.string.cancel, null)
					setCancelable(false)
					show()
				}

				return false
			} else {
				return true
			}
		} else {
			return true
		}
	}

	override fun onCreateView(
		inflater: LayoutInflater, container: ViewGroup?, savedInstanceState: Bundle?
	): View {
		_binding = FragmentLibraryBinding.inflate(inflater, container, false)

		val adapter = GameAdapter(libraryViewModel)
		binding.gamesList.adapter = adapter

		requireActivity().addMenuProvider(this, viewLifecycleOwner, Lifecycle.State.RESUMED)

		return binding.root
	}

	override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
		binding.swipeRefresh.setOnRefreshListener { libraryViewModel.reloadGames(requireContext()) }

		libraryViewModel.isReloading.observe(viewLifecycleOwner) {
			binding.swipeRefresh.isRefreshing = it
		}

		libraryViewModel.installedGames.observe(viewLifecycleOwner) {
			(binding.gamesList.adapter as GameAdapter).submitList(it)
			if (it.isEmpty()) {
				checkFirstRunGameData()
			}
		}

		if (checkStoragePermissions()) {
			libraryViewModel.reloadGames(requireContext())
		}
	}

	private fun checkFirstRunGameData() {
		val downloader = GameDataDownloader(requireContext())
		if (downloader.hasCheckedFirstRun()) return
		if (hasShownFirstRunDialog) return
		if (!checkStoragePermissions()) return

		val basedir = libraryViewModel.getBaseDir()
		if (downloader.hasAnyGameData(basedir)) {
			downloader.setFirstRunChecked()
			return
		}

		hasShownFirstRunDialog = true
		MaterialAlertDialogBuilder(requireContext())
			.setTitle(R.string.no_game_data_found)
			.setMessage(R.string.no_game_data_message)
			.setPositiveButton(R.string.download_now) { _, _ ->
				downloader.setFirstRunChecked()
				findNavController().navigate(R.id.action_libraryFragment_to_gameDataDownloaderFragment)
			}
			.setNegativeButton(R.string.no_thanks) { _, _ ->
				downloader.setFirstRunChecked()
			}
			.setCancelable(false)
			.show()
	}

	override fun onDestroyView() {
		super.onDestroyView()
		_binding = null
	}

	override fun onCreateMenu(menu: Menu, menuInflater: MenuInflater) {
		menuInflater.inflate(R.menu.menu_library, menu)
	}

	override fun onMenuItemSelected(menuItem: MenuItem): Boolean {
		when (menuItem.itemId) {
			R.id.action_settings -> {
				findNavController().navigate(R.id.action_libraryFragment_to_appSettingsFragment)
			}
			R.id.action_download -> {
				findNavController().navigate(R.id.action_libraryFragment_to_gameDataDownloaderFragment)
			}
		}

		return false
	}

	override fun onResume() {
		super.onResume()

		if (checkStoragePermissions()) {
			libraryViewModel.reloadGames(requireContext())
		}
	}
}
