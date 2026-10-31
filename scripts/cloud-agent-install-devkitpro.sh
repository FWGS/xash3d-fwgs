#!/usr/bin/env bash
# Install the devkitPPC + libogc GameCube toolchain into /opt/devkitpro.
#
# devkitPro's package server (pkg.devkitpro.org) sits behind Cloudflare and
# rejects datacenter IPs with HTTP 403, and devkitPro explicitly discourages
# using dkp-pacman on CI. Instead we extract /opt/devkitpro straight out of the
# official `devkitpro/devkitppc` Docker image through the registry HTTP API, so
# no Docker daemon is required. The image ships devkitPPC, libogc (including
# libogc/lib/cube/libogc.a) and the GameCube/Wii portlibs the build needs.
set -euo pipefail

IMAGE="${DEVKITPPC_IMAGE:-devkitpro/devkitppc}"
TAG="${DEVKITPPC_IMAGE_TAG:-latest}"
DEST="${DEVKITPRO:-/opt/devkitpro}"
REGISTRY="https://registry-1.docker.io"

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
cd "$work"

get_token() {
	curl -fsSL "https://auth.docker.io/token?service=registry.docker.io&scope=repository:${IMAGE}:pull" \
		| python3 -c "import sys,json;print(json.load(sys.stdin)['token'])"
}

echo "Resolving ${IMAGE}:${TAG} manifest..."
token="$(get_token)"
index="$(curl -fsSL -H "Authorization: Bearer $token" \
	-H "Accept: application/vnd.oci.image.index.v1+json" \
	-H "Accept: application/vnd.docker.distribution.manifest.list.v2+json" \
	"${REGISTRY}/v2/${IMAGE}/manifests/${TAG}")"

digest="$(printf '%s' "$index" | python3 -c "
import sys, json
d = json.load(sys.stdin)
if 'manifests' in d:
    cand = [m['digest'] for m in d['manifests']
            if m.get('platform', {}).get('architecture') == 'amd64'
            and m.get('platform', {}).get('os') == 'linux']
    print(cand[0])
else:
    print('')
")"

if [ -n "$digest" ]; then
	manifest="$(curl -fsSL -H "Authorization: Bearer $token" \
		-H "Accept: application/vnd.oci.image.manifest.v1+json" \
		-H "Accept: application/vnd.docker.distribution.manifest.v2+json" \
		"${REGISTRY}/v2/${IMAGE}/manifests/${digest}")"
else
	manifest="$index"
fi

printf '%s' "$manifest" | python3 -c "
import sys, json
for layer in json.load(sys.stdin)['layers']:
    print(layer['digest'])
" > layers.txt

mkdir -p root
i=0
while read -r layer; do
	i=$((i + 1))
	echo "Fetching layer ${i} (${layer%%:*}...)"
	# Refresh the pull token per layer; large images can outlive one token.
	token="$(get_token)"
	curl -fsSL -H "Authorization: Bearer $token" \
		"${REGISTRY}/v2/${IMAGE}/blobs/${layer}" -o layer.tar.gz
	tar -xzf layer.tar.gz -C root 2>/dev/null || true
	rm -f layer.tar.gz
done < layers.txt

if [ ! -x "root/opt/devkitpro/devkitPPC/bin/powerpc-eabi-gcc" ]; then
	echo "error: devkitPPC not found in extracted image" >&2
	exit 1
fi

echo "Installing toolchain into ${DEST}..."
sudo rm -rf "$DEST"
sudo cp -a root/opt/devkitpro "$DEST"
echo "devkitPPC installed: $("$DEST/devkitPPC/bin/powerpc-eabi-gcc" --version | head -1)"
