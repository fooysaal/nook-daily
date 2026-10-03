<div align="center">

<img src="data/icons/hicolor/128x128/apps/nook.png" width="96" alt="Nook">

# Nook

**A tiny local notes / tasks / clipboard companion for the Linux desktop.**
Lives in the system tray, opens a panel in the corner, stores everything in a plain text file on your machine.

[![License: MIT](https://img.shields.io/badge/license-MIT-8c7cf0.svg)](LICENSE)
[![Dependency: GTK3 only](https://img.shields.io/badge/deps-GTK3%20only-8c7cf0.svg)](#requirements)
[![Written in C](https://img.shields.io/badge/written%20in-C-8c7cf0.svg)](src/)

</div>

Plain C and GTK3 — no Electron, no Node, no Rust, no browser engine. The whole binary is about
56 KB and starts instantly.

---

## Quick start

```sh
sudo apt install -y build-essential pkg-config libgtk-3-dev
git clone https://github.com/fooysaal/nook-daily.git && cd nook-daily && ./install.sh
```

That's it — the Mote icon appears in your tray. **Double-click it to open the panel.**

<sub>Fedora: `sudo dnf install gcc pkgconf-pkg-config gtk3-devel` · Arch: `sudo pacman -S base-devel gtk3`</sub>

`install.sh` builds Nook and installs it into `~/.local` — no root needed:

| What | Where |
|---|---|
| Binary | `~/.local/bin/nook` |
| Icons | `~/.local/share/icons/hicolor/*/apps/nook.png` |
| Desktop entry | `~/.local/share/applications/nook.desktop` |
| Autostart | `~/.config/autostart/nook.desktop` |

Variants:

```sh
AUTOSTART=0 ./install.sh          # install, but don't start on login
PREFIX=/usr/local sudo ./install.sh   # system-wide
make uninstall                    # remove everything above
```

If `~/.local/bin` isn't on your `PATH`, the installer says so — that only affects typing `nook`
in a shell. The app menu entry and autostart use the full path, so Nook still starts. Log out and
back in if the tray icon doesn't show up immediately.

## Requirements

| Distro | Build packages |
|---|---|
| Debian / Ubuntu | `build-essential pkg-config libgtk-3-dev` |
| Fedora | `gcc pkgconf-pkg-config gtk3-devel` |
| Arch | `base-devel gtk3` |

At runtime it needs only `libgtk-3-0`, which is already on essentially every Linux desktop.

## Using Nook

### The tray icon

| You do | Nook does | Why |
|---|---|---|
| **Double-click** the icon | Opens the panel (or closes it if open) | This is the reliable path — GNOME's AppIndicator extension only calls `Activate` on a *double* click |
| **Single-click** the icon | Shows a two-item menu: **Open Nook** / **Quit Nook** | A single click toggles the menu instead of reaching the app, so the menu exists to make that click do something |
| **Right-click** | Same menu, drawn by your desktop shell | |

**Quit lives only in the tray menu** — the panel itself never exits the app, it only hides.

### The panel

Opens pinned to the **top-right of your work area**, always on top, no window decorations, no
taskbar entry.

```
┌──────────────────────────────────┐
│ Notes   Tasks   Clips        ⋯ ✕ │  ← tab row; ⋯ export/import, ✕ hides the panel
├──────────────────────────────────┤
│ [ Quick note…            ] [Add] │  ← type, press Enter or click Add
│                                  │
│  TODAY                           │  ← items group under the day they were taken
│  remember the milk          ✕    │  ← newest first; ✕ deletes this item
│  14:22                           │  ← capture time, in your 12h/24h setting
│  YESTERDAY                       │
│  pick up the dry cleaning   ✕    │
│  09:04                           │
├──────────────────────────────────┤
│        stored on this device     │  ← turns red if a save fails
└──────────────────────────────────┘
```

The footer is a live status line, not decoration: if Nook can't write to disk it turns red and
says *"could not save — changes will be lost"* instead of failing quietly.

### The tabs

| Tab | What it does |
|---|---|
| **Notes** | Type and press **Enter** (or click **Add**). Each note is timestamped and stays until you delete it with ✕. |
| **Tasks** | Same, plus a checkbox. Checking an item strikes it through and dims it; the state survives a restart. |
| **Clips** | Copies are saved as you make them while **Auto-save** is ticked; **Grab clipboard** and the entry still save on demand. Each clip has a copy button — which says **Copied to clipboard** for a moment when you press it — and ★ to keep it from being evicted. Tick **Starred** to list only the kept ones. Long URLs and tokens wrap instead of stretching the window. |

Every list groups by day — **Today**, **Yesterday**, then the date in your locale's format — and
times follow the desktop's 12h/24h setting.

Switching tabs never closes the panel, and Nook reopens on the tab you used last.

### Dismissing

| Action | Result |
|---|---|
| `Esc` | Hide panel |
| Click anywhere outside | Hide panel |
| ✕ in the tab row | Hide panel |
| Double-click the tray icon again | Hide panel |
| **Quit Nook** (tray menu) | Exit and remove the icon |

### Keyboard

Everything is reachable without a mouse. `Tab` / `Shift+Tab` moves through the entry, the list
rows and the buttons with visible focus; `Enter` commits the entry; `Space` toggles a task
checkbox; `Esc` hides the panel. Item text is selectable, and the ✕ buttons are labelled for
screen readers.

## Your data

One plain text file:

```
~/.local/share/nook/data.ini
```

```ini
[notes.4f3c…]
text=remember the milk
ts=1758300000
```

It's a GKeyFile — readable, greppable, editable in any text editor, trivial to back up or sync
with whatever tool you already use. No database, no JSON library, no cloud.

### Moving to another machine

**⋯ → Export data…** saves notes, tasks and clips to a `nook-backup-<date>.ini` file readable
only by you (clips can hold passwords or tokens — treat the file accordingly). On the other
machine, **⋯ → Import data…** merges it in: anything already there is kept, items already
imported are skipped, and deletions are not carried over.

## Troubleshooting

**No icon on GNOME.** Vanilla GNOME (Fedora Workstation, Arch + GNOME) ships no tray host.
Install `gnome-shell-extension-appindicator` and log out. Ubuntu enables it already; KDE, XFCE,
Cinnamon and MATE support it natively. `install.sh` warns when it detects the missing extension.

**Nook opened a panel by itself at startup.** That's deliberate. If no tray host answers within
3 seconds, Nook opens the panel and logs a warning rather than sitting invisible with no way to
reach or quit it.

**The panel isn't next to my tray icon.** It pins to the top-right of the work area, which is
right on GNOME and wrong on a bottom panel. AppIndicator never tells the application where its
icon is drawn, so there's nothing to anchor to.

**Wayland.** Nook forces the X11 backend (via Xwayland) before `gtk_init`, because Wayland
forbids a client from positioning its own window or raising itself above others. It falls back
to other backends if X11 is unavailable, but positioning won't work there.

**Build dies with a glibc symbol error.** You're in a snap-confined terminal (the VS Code snap,
typically), which poisons `GTK_PATH` and `LOCPATH`. Build from a normal terminal.

**Check what the tray is doing:**

```sh
journalctl --user -f | grep nook
```

## Build from source

```sh
make          # build ./nook
make run      # build and run from the source tree
make check    # persistence round-trip test
make clean    # remove objects and binaries
./nook --version
```

The version lives in one place, `VERSION` in the `Makefile`; it reaches the binary as
`NOOK_VERSION`. Every user-visible change is recorded in [CHANGELOG.md](CHANGELOG.md).

Exercise the tray without touching the mouse:

```sh
PID=$(pgrep -x nook); NAME="org.kde.StatusNotifierItem-$PID-1"
gdbus call -e -d "$NAME" -o /StatusNotifierItem -m org.kde.StatusNotifierItem.Activate 0 0
gdbus call -e -d "$NAME" -o /MenuBar -m com.canonical.dbusmenu.GetLayout 0 2 "['label']"
```

## Layout

```
src/main.c                    GTK UI, window lifecycle, panel positioning
src/tray.c/.h                 StatusNotifierItem + dbusmenu, spoken directly over GDBus
src/store.c/.h                items and GKeyFile persistence
src/test_store.c              round-trip test (make check)
CHANGELOG.md                  what changed in each version
data/nook.desktop             desktop entry
data/icons/hicolor/           a complete icon theme, index.theme included
```

The icons are a self-contained theme rather than a loose PNG on purpose: the GNOME AppIndicator
extension *replaces* the icon search path with the one the app hands it, so without a valid
`index.theme` listing every size the lookup fails silently and the shell shows a "…" placeholder.

The tray talks the StatusNotifierItem D-Bus spec directly instead of using
libayatana-appindicator, because that library exposes no `Activate` method — and without
`Activate`, double-clicking the icon could never open the panel.

## Not included

Fully local, deliberately small:

- No account, no cloud, no sync, no telemetry, no analytics.
- No Electron, Node, Rust or webview — an earlier Tauri prototype was removed on purpose.
- No dragging or floating mascot window — built, tested, and deliberately dropped.
- No screenshot or screen recording. The placeholder tab for it was removed.

## Contributing

Small surface is the point. Three rules: `make check` must keep passing, GTK3 stays the only
dependency, and anything a user would notice gets a line in [CHANGELOG.md](CHANGELOG.md) under
*Unreleased* — bump `VERSION` in the `Makefile` when that becomes a release. Extend
`src/test_store.c` when persistence changes.

## License

[MIT](LICENSE)
