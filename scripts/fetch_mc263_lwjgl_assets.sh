#!/usr/bin/env bash
set -Eeuo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ASSET_DIR="$ROOT/ZalithLauncher/src/main/assets/components/lwjgl3"
TMP_DIR="$(mktemp -d)"
trap 'rm -rf "$TMP_DIR"' EXIT

SDL_URL="https://raw.githubusercontent.com/Star1xr/ZalithLauncher2Plus/b9631a18a3c11b4d4b2c27945506eb308a089435/ZalithLauncher/src/main/assets/components/lwjgl3/lwjgl-sdl.jar"
GLFW_URL="https://raw.githubusercontent.com/Star1xr/ZalithLauncher2Plus/bb29ba278199b75558d670fa999db77713ec8836/ZalithLauncher/src/main/assets/components/lwjgl3/lwjgl-glfw-classes.jar"

command -v curl >/dev/null 2>&1 || {
    echo "Error: curl is required to fetch the Minecraft 26.3 LWJGL compatibility assets." >&2
    exit 1
}

mkdir -p "$ASSET_DIR"
echo "Downloading patched LWJGL SDL binding..."
curl --fail --location --retry 3 --connect-timeout 20 "$SDL_URL" -o "$TMP_DIR/lwjgl-sdl.jar"
echo "Downloading patched LWJGL GLFW compatibility classes..."
curl --fail --location --retry 3 --connect-timeout 20 "$GLFW_URL" -o "$TMP_DIR/lwjgl-glfw-classes.jar"

# Guard against HTML error pages or incomplete downloads.
SDL_SIZE="$(wc -c < "$TMP_DIR/lwjgl-sdl.jar" | tr -d '[:space:]')"
GLFW_SIZE="$(wc -c < "$TMP_DIR/lwjgl-glfw-classes.jar" | tr -d '[:space:]')"
if [[ "$SDL_SIZE" != "994249" ]]; then
    echo "Error: unexpected lwjgl-sdl.jar size: $SDL_SIZE (expected 994249)." >&2
    exit 1
fi
if [[ "$GLFW_SIZE" != "10830155" ]]; then
    echo "Error: unexpected lwjgl-glfw-classes.jar size: $GLFW_SIZE (expected 10830155)." >&2
    exit 1
fi

mv "$TMP_DIR/lwjgl-sdl.jar" "$ASSET_DIR/lwjgl-sdl.jar"
mv "$TMP_DIR/lwjgl-glfw-classes.jar" "$ASSET_DIR/lwjgl-glfw-classes.jar"

# Force component re-extraction in installations that already have the older jars.
