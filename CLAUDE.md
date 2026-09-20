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
- Clips — read the system clipboard or paste manually, delete.
- Capture — placeholder only. Do not implement unless scope explicitly expands.

## Implementation constraints

- The only dependency is **GTK3**. The tray is a StatusNotifierItem implemented directly on
  GDBus in `src/tray.c`, so libayatana-appindicator is *not* used. Do not add dependencies
  without a strong reason — a small surface is the point of this rewrite.
- Persistence is GKeyFile at `~/.local/share/nook/data.ini`. No JSON library, no database.
- Preserve keyboard access and focus visibility.
- `make check` must keep passing; extend `src/test_store.c` when persistence changes.

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
- Double-clicking the icon opens the panel; a single click shows Open / Quit; Quit lives only
  in the tray menu, not in the panel.
- With no tray host, the panel opens by itself after 3s and a warning is logged, rather than the
  process sitting invisible with no way to quit.
- A failed save turns the footer red instead of only logging.
- Tabs switch sections without closing the panel.
- The close button and Quit in the tab row both work.
- Notes add/delete, Tasks toggle, Clips clipboard grab — each survives a restart.
- Escape and clicking away both hide the panel; Quit exits and removes the icon.

## Development note

Launching from a snap-confined terminal (e.g. the VS Code snap) poisons `GTK_PATH`/`LOCPATH` and
the app dies with a glibc symbol error. Use a normal terminal.
