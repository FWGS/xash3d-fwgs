#!/usr/bin/env bash
# Configure the controller pipe and logging in the probe's private profile.
if [[ "${DOLPHIN_PAD_PIPE:-0}" == "1" ]]; then
	mkdir -p "$USER_DIR/Pipes"
	if [[ -e "$USER_DIR/Pipes/gcpad" || -L "$USER_DIR/Pipes/gcpad" ]]; then
		if [[ ! -p "$USER_DIR/Pipes/gcpad" || -L "$USER_DIR/Pipes/gcpad" ]]; then
			echo "error: refusing to replace non-FIFO controller path $USER_DIR/Pipes/gcpad" >&2
			return 1
		fi
		rm -f -- "$USER_DIR/Pipes/gcpad"
	fi
	mkfifo "$USER_DIR/Pipes/gcpad" || { echo "error: cannot create pad FIFO under $USER_DIR (set DOLPHIN_USER_DIR to a tmpfs path)" >&2; return 1; }
	cat > "$USER_DIR/Config/GCPadNew.ini" <<'EOF'
[GCPad1]
Device = Pipe/0/gcpad
Buttons/A = `Button A`
Buttons/B = `Button B`
Buttons/X = `Button X`
Buttons/Y = `Button Y`
Buttons/Z = `Button Z`
Buttons/Start = `Button START`
Main Stick/Up = `Axis MAIN Y -`
Main Stick/Down = `Axis MAIN Y +`
Main Stick/Left = `Axis MAIN X -`
Main Stick/Right = `Axis MAIN X +`
C-Stick/Up = `Axis C Y -`
C-Stick/Down = `Axis C Y +`
C-Stick/Left = `Axis C X -`
C-Stick/Right = `Axis C X +`
Triggers/L = `Button L`
Triggers/R = `Button R`
D-Pad/Up = `Button D_UP`
D-Pad/Down = `Button D_DOWN`
D-Pad/Left = `Button D_LEFT`
D-Pad/Right = `Button D_RIGHT`
EOF
	printf '[Input]\nBackgroundInput = True\n' >> "$USER_DIR/Config/Dolphin.ini"
	echo "==> GameCube port 1 on Dolphin pipe $USER_DIR/Pipes/gcpad"
fi
cat > "$USER_DIR/Config/Logger.ini" <<'EOF'
[Logs]
BOOT = True
CORE = True
DVD = False
OSREPORT = True
OSREPORT_HLE = True
PowerPC = False
[Options]
Verbosity = 4
WriteToConsole = True
WriteToFile = True
WriteToWindow = False
EOF
# DOLPHIN_LOG_PPC=1: log PowerPC exceptions and memory-interface errors, so
# a guest crash that makes batch Dolphin stop (exit 0, no OSReport) says why.
if [[ "${DOLPHIN_DUMP_VIDEO:-0}" == "1" ]]; then
	sed -i 's/^\[Logs\]/[Logs]\nFRAMEDUMP = True/' "$USER_DIR/Config/Logger.ini"
fi
if [[ "${DOLPHIN_LOG_PPC:-0}" == "1" ]]; then
	sed -i 's/^PowerPC = False/PowerPC = True\nMI = True\nMEMMAP = True/' "$USER_DIR/Config/Logger.ini"
fi
