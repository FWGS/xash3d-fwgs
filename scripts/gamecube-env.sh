#!/usr/bin/env bash
# Shared non-secret automation defaults for the GameCube port.

gamecube_export_dolphin_env() {
	local flatpak_id="${DOLPHIN_FLATPAK_ID:-org.DolphinEmu.dolphin-emu}"

	# If DOLPHIN_EXECUTABLE is already set by the environment, respect it.
	# Always export the flatpak ID as it may be needed by callers for cleanup
	# or specific flatpak operations even if a native binary is preferred.
	export DOLPHIN_FLATPAK_ID="$flatpak_id"

	if [[ -n "${DOLPHIN_EXECUTABLE:-}" ]]; then
		export DOLPHIN_EXECUTABLE
		return 0
	fi

	if command -v dolphin-emu >/dev/null 2>&1; then
		DOLPHIN_EXECUTABLE="$(command -v dolphin-emu)"
	elif command -v dolphin >/dev/null 2>&1; then
		DOLPHIN_EXECUTABLE="$(command -v dolphin)"
	elif command -v flatpak >/dev/null 2>&1 && \
		flatpak info "$flatpak_id" >/dev/null 2>&1; then
		DOLPHIN_EXECUTABLE="flatpak:$flatpak_id"
	fi

	if [[ -n "${DOLPHIN_EXECUTABLE:-}" ]]; then
		export DOLPHIN_EXECUTABLE
	fi
}

gamecube_export_dolphin_env
