#!/usr/bin/env bash
set -e

BINDIR="$HOME/.local/bin"
APPDIR="$HOME/.local/share/applications"
ICONDIR="$HOME/.local/share/icons/hicolor/scalable/apps"
ICON256="$HOME/.local/share/icons/hicolor/256x256/apps"

mkdir -p "$BINDIR" "$APPDIR" "$ICONDIR" "$ICON256"

cmake -B build -G "Unix Makefiles" 2>&1
cmake --build build -j$(nproc) 2>&1

# Install binary (keep user-facing name for the command)
cp build/wbe-lnx "$BINDIR/desktop-wallpaper"

# Install .desktop entry
cat > "$APPDIR/desktop-wallpaper.desktop" << 'EOF'
[Desktop Entry]
Name=Desktop Wallpaper
Comment=Animated video wallpaper engine
Exec=desktop-wallpaper
Type=Application
Categories=Utility;
Terminal=false
Icon=desktop-wallpaper
StartupNotify=false
EOF

# Install icon
cp icon.svg "$ICONDIR/desktop-wallpaper.svg"

# Copy sample videos to ~/Videos/wbe-samples/
SAMPLES="$HOME/Videos/wbe-samples"
mkdir -p "$SAMPLES"
cp example/*.mp4 example/gen.sh "$SAMPLES/" 2>/dev/null || true

gtk-update-icon-cache ~/.local/share/icons/hicolor/ 2>/dev/null || true
update-desktop-database ~/.local/share/applications/ 2>/dev/null || true

echo "Installed!"
echo "  Binary:    $BINDIR/desktop-wallpaper"
echo "  Samples:   $SAMPLES/"
echo "  Run:       desktop-wallpaper"
echo "  Kill:      desktop-wallpaper --kill"
