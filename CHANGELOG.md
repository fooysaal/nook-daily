# Changelog

All notable changes to Nook are recorded here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and versions follow
[Semantic Versioning](https://semver.org/spec/v2.0.0.html) — while the major is `0`, a minor bump
is a feature release and may change behaviour.

The running version is `VERSION` in the `Makefile`, reported by `nook --version` and in the
footer's tooltip.

## [0.2.0] — 2026-09-20

Clips stops being a manual scratchpad: it watches the clipboard for you, and every list is
grouped by day.

### Added

- **Automatic clipboard capture.** Copies are saved as you make them, via GTK's `owner-change`
  signal — no polling. An **Auto-save** checkbox turns it off and the choice persists.
- **Copy button on every clip**, which returns the clip's full text and confirms with a checkmark
  and "Copied to clipboard" for 1.5 seconds.
- **★ keep** on every clip. Kept clips are exempt from eviction, and a **Starred** checkbox lists
  only those.
- **Day grouping** in Notes, Tasks and Clips — `Today`, `Yesterday`, then the locale's date.
- Times follow the desktop's 12h/24h setting (`org.gnome.desktop.interface clock-format`).
- `nook --version`.

### Changed

- Clips are bounded so an unattended watcher cannot fill the disk: auto-capture ignores anything
  over 8 KB (**Grab clipboard** still takes it), and the list holds 50, evicting the oldest
  *unkept* clip.
- Clip rows show a 3-line / 160-character preview with a `· 412 chars` or `· 1240 lines` note,
  instead of rendering a whole file dump into the panel.
- The panel is one colour throughout, and all four corners follow the 14px radius.
- Copy buttons use the icon theme (`edit-copy-symbolic`), falling back to the word `Copy` where
  the icon is missing.

### Removed

- The **Capture** placeholder tab. Screenshot and screen recording are not the direction.

### Fixed

- Clips carrying a password-manager hint are never captured — including when the owning
  application quits and the desktop's clipboard manager re-announces the same text without the
  hint.
- A clip copied out of Nook survives quitting Nook (`gtk_clipboard_set_can_store`).
- Copying inside the panel no longer stores a duplicate clip or destroys the selection being read.
- The clip just added is never its own eviction victim, which silently disabled auto-capture once
  50 kept clips existed.
- Items created in the same second keep their order across a restart.
- A failed clipboard TARGETS conversion fails closed rather than capturing unchecked.

## [0.1.0] — 2026-09-20

First native release: a complete rewrite of an earlier Tauri/TypeScript prototype into C and
GTK3, with no Node, Rust or webview left.

### Added

- Tray indicator speaking the StatusNotifierItem D-Bus spec directly on GDBus, so double-clicking
  the icon opens the panel (`Activate`) and a single click shows Open / Quit.
- Panel pinned to the top-right of the work area: Notes, Tasks and Clips as notebook tabs, closed
  by Escape, focus loss or the ✕ in the tab row.
- Notes (timestamped capture), Tasks (add/check/delete) and Clips (grab the clipboard or paste).
- GKeyFile persistence at `~/.local/share/nook/data.ini`, with a footer that turns red when a save
  fails instead of losing data quietly.
- `make check` round-trip test for the store, and `install.sh`.
