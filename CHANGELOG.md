# Changelog

All notable changes to Nook are recorded here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and versions follow
[Semantic Versioning](https://semver.org/spec/v2.0.0.html) — a breaking change bumps the major,
a feature the minor, a fix the patch.

The running version is `VERSION` in the `Makefile`, reported by `nook --version` and in the
footer's tooltip.

## [Unreleased]

## [1.0.0] — 2026-10-03

Your data can move between machines, Nook runs as a single instance, and the tray icon comes up
reliably when you log in.

### Added

- **Export and import.** The panel's `⋯` menu saves everything — notes, tasks and clips — to an
  `.ini` file you choose, and imports one on another machine. Import merges: items already
  present are skipped, so importing the same file twice adds nothing, and deletions are not
  carried over. Exports are readable only by you, since clips can hold secrets.
- **Tray steps are logged to the journal.** Registering with the tray host, the host fetching
  the menu and reading the icon each log one `nook:` line, so
  `journalctl --user -b | grep 'nook:'` shows where a broken tray icon stopped.

### Fixed

- **Installing no longer creates an icon cache in your shared icon folder.** `make install` used
  to generate `~/.local/share/icons/hicolor/icon-theme.cache`, and GTK trusts that cache over
  the folder — so icons other apps drop there later (browser web apps, for one) could go missing.
  An existing cache is still refreshed; none is created.
- **Reinstalling respects autostart being switched off.** `install.sh` no longer overwrites an
  autostart entry your desktop settings have disabled.
- **Launching Nook again opens the running panel.** Each click on the app icon used to start
  another process with its own tray icon; Nook is now a single instance.
- **Tray icon and menu survive starting with the session.** Started in the same second as
  GNOME Shell, the icon could stay a `…` with no Open/Quit menu until a suspend and wake. Nook
  now re-announces its icon and menu a few seconds after registering.
- **Nook starts even when `~/.local/bin` is off your `PATH`.** The installed desktop entry and
  the autostart copy now carry the full path to the binary instead of a bare `Exec=nook`, which
  the app menu and the login session could not resolve — the install looked fine and nothing
  ever launched.

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
