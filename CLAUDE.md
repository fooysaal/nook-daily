# Claude CLI implementation brief — Nook

## What this is

A small local notes / tasks / clipboard companion for the Linux desktop, written in **C with
GTK3**. It lives as a tray indicator; clicking it opens a menu, and choosing a section opens a
panel with tabs.

There is no web stack. An earlier Tauri/TypeScript implementation existed and was **removed** —
do not reintroduce Node, Rust, a webview, or an HTML frontend.

## Source of truth

- `reference-design-notes.md` — original product concept.
- `reference-ui.png` — the original floating-mascot concept. **Superseded**; style reference only.
- `src/main.c`, `src/store.c`, `src/tray.c` — the implementation.

## Interaction model

1. A tray icon (Mote) registers in the system tray; no taskbar entry.
2. **Double-clicking** the icon opens the panel directly (`Activate`).
3. A single click shows a two-item menu, Open Nook / Quit Nook — see the constraint below.
4. The panel pins to the top-right of the work area.
5. Sections are a `GtkNotebook` tab bar across the top; a close button sits at the end of that
   tab row. Quit is in the tray menu only.
6. Switching tabs never closes the panel.
7. Escape, focus loss, the close button, and Quit all dismiss or exit.

Do not reintroduce dragging, edge-snapping, or a floating mascot window; that model was built,
tested, and deliberately removed.

## Product boundaries

Fully local. No account, cloud, telemetry, analytics, or sync.

- Notes — timestamped text capture, delete.
- Tasks — add/check/delete.
- Clips — automatic clipboard capture (toggle in the tab), read the clipboard or paste manually,
  copy back, keep, delete.
- Export / import — the `⋯` menu in the tab row writes the same GKeyFile to a chosen path
  (mode 0600; clips included) and imports one by **merging on item id**, never replacing.
  The local `auto_clips` setting is not imported. Panel focus-out is suppressed while the file
  chooser is open, or opening it would hide the panel.
- Capture — **removed on request.** The placeholder tab and `build_capture_page()` are gone;
  do not reinstate them. Screenshot/recording is not the direction.

## Implementation constraints

- The only dependency is **GTK3**. The tray is a StatusNotifierItem implemented directly on
  GDBus in `src/tray.c`, so libayatana-appindicator is *not* used. Do not add dependencies
  without a strong reason — a small surface is the point of this rewrite.
- Persistence is GKeyFile at `~/.local/share/nook/data.ini`. No JSON library, no database.
- Preserve keyboard access and focus visibility.
- `make check` must keep passing; extend `src/test_store.c` when persistence changes.
- Versioning is semver in one place: `VERSION` in the `Makefile`, reaching the code as
  `NOOK_VERSION` (`nook --version`, and the footer tooltip). Every user-visible change goes in
  `CHANGELOG.md` under *Unreleased* as it is made, and the version is bumped when that section
  becomes a release — a breaking change is a major bump, a feature a minor, a fix a patch.

## Hard-won platform constraints — do not re-litigate

- **The tray icon needs a valid icon theme, not just a PNG.** The GNOME AppIndicator extension
  calls `St.IconTheme.set_search_path([ourPath])`, which *replaces* the search path, so the
  directory must be a self-contained theme with `data/icons/hicolor/index.theme` listing every
  size. Without it the lookup fails silently and GNOME shows `image-loading-symbolic` (a "…"
  placeholder). GTK's own loader is lenient here and will not reproduce the failure — test with
  the real shell and check `journalctl --user` for "Impossible to lookup icon".
- **X11 / Xwayland.** `gdk_set_allowed_backends("x11,*")` runs before `gtk_init`, because Wayland
  forbids a client positioning its own window or raising itself above others.
- **A single tray click can never reach the app on GNOME.** The shell extension's
  `vfunc_button_press_event` toggles the menu on a single primary click, and only calls
  `Activate` on a *double* click; with an empty menu a single click does nothing at all. This is
  why the app implements `Activate` (double-click opens) and keeps a two-item menu so a single
  click is not dead. libayatana-appindicator cannot do this — it exposes no Activate method,
  which is why `src/tray.c` speaks the D-Bus spec directly.
- **`ItemIsMenu` must stay `false`**, or the host will never call `Activate`.
- **The panel cannot be anchored to the tray icon.** AppIndicator does not expose the icon's
  position, so it pins to the top-right of the work area — correct on GNOME, wrong on a
  bottom-panel desktop.
- **A long unbroken string will drag the window off screen** unless list labels use
  `PANGO_WRAP_WORD_CHAR` *and* `gtk_label_set_max_width_chars()`. Plain `set_line_wrap(TRUE)`
  breaks only at spaces, so a copied URL or token demands its full single-line width; with
  horizontal scrolling set to `POLICY_NEVER` that width propagates to the window. Measured at
  8144px wide from one 400-character clip before the fix. Any change to list rows must be
  re-tested with a long unbroken clip.
- **`position_window()` must read the real size**, not `PANEL_W`. GTK gives a non-resizable
  window `max(natural, default)`, so a wider natural size silently pushes the panel off the
  right edge and the window manager clamps it, losing the margin.
- **Notebook action widgets are internal children.** `gtk_widget_show_all()` on an ancestor does
  not reach them; they need an explicit `gtk_widget_show_all()` or they never appear.
- **Right-click is delegated to the host.** `ContextMenu` is accepted and ignored because GNOME
  renders the dbusmenu itself. A host that instead calls `ContextMenu` would show nothing, and
  since Quit lives only in the tray menu those users would have no way to exit. Revisit if a
  non-GNOME host is ever targeted.
- **A clipboard manager re-announces a clip after the owning app quits.** Auto-capture skips a
  clip whose targets carry a password-manager hint, but when that app exits the desktop's
  clipboard manager takes ownership and re-offers the same text *without* the hint, which lands a
  secret in `data.ini` on the second `owner-change`. `src/main.c` therefore remembers the SHA-256
  of a skipped clip and drops it again. Re-test by copying with the hint target set and then
  killing the owner, not just while it is alive.
- **Auto-capture reads the clipboard asynchronously.** `owner-change` fires on the main loop
  while the new owner may not be ready to answer, so `gtk_clipboard_request_text()` is used, not
  `gtk_clipboard_wait_for_text()`, which would stall the UI.
- **The theme paints square corners over the panel's radius.** GTK does not clip a child to a
  rounded parent, so Adwaita's 1px border on the `notebook` node (and the opaque background on
  `notebook header`) drew straight across `.panel`'s 14px top corners, while the footer left the
  bottom two clean. `STYLE` zeroes both. Probing the extreme corner pixel proves nothing — it is
  transparent either way; sample the ~28px corner patch and compare the top pair against the
  bottom pair.
- **Row icons come from the icon theme, with a text fallback.** The copy button uses
  `edit-copy-symbolic` only when `gtk_icon_theme_has_icon()` says it exists, otherwise the word
  `Copy`; a themed icon that is missing renders as a placeholder, exactly like the tray `…`.
- **`%l` pads with a figure space, not a space.** The 12h clock format from
  `org.gnome.desktop.interface clock-format` uses `%l`, and glib pads a single-digit hour with
  U+2007, which `g_strstrip()` does not remove — the time rendered as `⁠ 1:36 PM`. `format_clock()`
  skips it explicitly.
- **Registering during shell startup can leave a dead `…` icon.** Autostarted in the same
  second GNOME Shell starts, the item registered and Activate worked, but the extension showed
  its `image-loading-symbolic` placeholder with no menu and never retried — only a suspend/wake
  redraw fixed it, and neither side logged anything. `src/tray.c` therefore emits `NewIcon` and
  `LayoutUpdated` once, 5s after registration. To debug a broken icon:
  `journalctl --user -b | grep 'nook:'` shows which of registered / menu fetched / icon read
  never happened; the extension's own debug output needs `G_MESSAGES_DEBUG=all` in the shell's
  environment.
- **Single instance comes from `GtkApplication`.** A second launch forwards `activate` to the
  running process, which opens the panel, and exits. Without it every desktop-icon click added
  another process and another tray icon.
- **Never create an icon cache in the user's hicolor directory.** `~/.local/share/icons/hicolor`
  is shared with other apps (Chrome web apps write there) that add icons without refreshing a
  cache, and GTK trusts a cache over the directory, so a cache Nook created could hide their
  icons. `make install`/`uninstall` only refresh one that already exists.
- **Vanilla GNOME has no AppIndicator host.** The icon will not appear on Fedora Workstation or
  Arch + GNOME without `gnome-shell-extension-appindicator`. `install.sh` warns about this.

## Acceptance checks

```bash
make          # build (needs gcc + GTK3 only)
make check    # persistence round-trip test
make run      # run from the source tree
```

The tray can be exercised without a mouse, which is how it was verified:

```bash
PID=$(pgrep -x nook); NAME="org.kde.StatusNotifierItem-$PID-1"
gdbus call -e -d "$NAME" -o /StatusNotifierItem -m org.kde.StatusNotifierItem.Activate 0 0
gdbus call -e -d "$NAME" -o /MenuBar -m com.canonical.dbusmenu.GetLayout 0 2 "['label']"
```

Manual QA:

- Tray icon shows the Mote artwork, not a placeholder.
- No window visible at startup.
- Launching Nook again while it runs opens the panel; `pgrep -c nook` stays 1.
- Double-clicking the icon opens the panel; a single click shows Open / Quit; Quit lives only
  in the tray menu, not in the panel.
- With no tray host, the panel opens by itself after 3s and a warning is logged, rather than the
  process sitting invisible with no way to quit.
- A failed save turns the footer red instead of only logging.
- Tabs switch sections without closing the panel.
- The close button and Quit in the tab row both work.
- Notes add/delete, Tasks toggle, Clips clipboard grab — each survives a restart.
- Copying anything else adds a clip by itself; a copy over 8 KB does not, and unticking
  **Auto-save** stops capture and survives a restart.
- The copy button flashes a checkmark and "Copied to clipboard" for 1.5s; **Starred** lists only
  kept clips and says so when none are kept.
- All four panel corners follow the 14px radius.
- Every list groups under Today / Yesterday / locale date, and times follow
  `org.gnome.desktop.interface clock-format`. Seed `data.ini` with timestamps a few days apart to
  check it; a schema-less system must fall back to 24h rather than fail.
- ⋯ → Export writes a 0600 file; importing it under a fresh `XDG_DATA_HOME` restores all three
  tabs, a second import reports nothing new, and the panel stays up while the chooser is open.
- Escape and clicking away both hide the panel; Quit exits and removes the icon.

## Development note

Launching from a snap-confined terminal (e.g. the VS Code snap) poisons `GTK_PATH`/`LOCPATH` and
the app dies with a glibc symbol error. Use a normal terminal.
