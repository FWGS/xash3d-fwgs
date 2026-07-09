#!/usr/bin/env bash
# Install a user-level systemd service for unattended GameCube port automation.
set -euo pipefail

ROOT="$(git rev-parse --show-toplevel)"
SERVICE_NAME="${GC_PORT_SERVICE_NAME:-xash3d-gc-port-automation.service}"
UNIT_DIR="${XDG_CONFIG_HOME:-$HOME/.config}/systemd/user"
UNIT_PATH="$UNIT_DIR/$SERVICE_NAME"

mkdir -p "$UNIT_DIR"

cat >"$UNIT_PATH" <<EOF
[Unit]
Description=Xash3D GameCube port automation loop
After=network.target

[Service]
Type=simple
WorkingDirectory=$ROOT
EnvironmentFile=-$ROOT/.env
Environment=GC_PORT_CONTINUOUS=1
ExecStart=$ROOT/scripts/gc-port-loop.sh --max-cycles 0 --sleep 20 --continuous
Restart=on-failure
RestartSec=30

[Install]
WantedBy=default.target
EOF

chmod +x "$ROOT/scripts/gc-port-loop.sh"

systemctl --user daemon-reload
echo "Installed $UNIT_PATH"
echo
echo "Start now:"
echo "  systemctl --user start ${SERVICE_NAME%.service}"
echo
echo "Enable on login:"
echo "  systemctl --user enable ${SERVICE_NAME%.service}"
echo
echo "Follow logs:"
echo "  journalctl --user -u ${SERVICE_NAME%.service} -f"
