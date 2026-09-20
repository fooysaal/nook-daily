#!/bin/sh
# Build and install Nook into ~/.local (no root required).
set -eu

PREFIX="${PREFIX:-$HOME/.local}"
AUTOSTART="${AUTOSTART:-1}"

missing=""
for pkg in gtk+-3.0; do
    pkg-config --exists "$pkg" 2>/dev/null || missing="$missing $pkg"
done

if [ -n "$missing" ]; then
    echo "Missing build dependencies:$missing" >&2
    echo >&2
    echo "  Debian/Ubuntu: sudo apt install build-essential pkg-config libgtk-3-dev" >&2
    echo "  Fedora:        sudo dnf install gcc pkgconf-pkg-config gtk3-devel" >&2
    echo "  Arch:          sudo pacman -S base-devel gtk3" >&2
    exit 1
fi

make install PREFIX="$PREFIX"

if [ "$AUTOSTART" = "1" ]; then
    mkdir -p "$HOME/.config/autostart"
    cp "$PREFIX/share/applications/nook.desktop" "$HOME/.config/autostart/nook.desktop"
    echo "Autostart enabled (AUTOSTART=0 to skip)."
fi

echo "Installed to $PREFIX/bin/nook"

case ":$PATH:" in
    *":$PREFIX/bin:"*) ;;
    *) echo "Note: $PREFIX/bin is not on your PATH." ;;
esac

# GNOME hides tray icons unless an AppIndicator host is present.
if [ "${XDG_CURRENT_DESKTOP#*GNOME}" != "$XDG_CURRENT_DESKTOP" ]; then
    if ! gnome-extensions list 2>/dev/null | grep -qi appindicator; then
        echo >&2
        echo "Warning: no AppIndicator extension found. On vanilla GNOME the tray icon" >&2
        echo "will not appear. Install 'gnome-shell-extension-appindicator' and log out." >&2
    fi
fi
