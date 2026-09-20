# Nook — desktop companion (design concept)

A small floating icon on Ubuntu that carries daily notes, tasks, and reusable clipboard content — fully local, always one click away.

## Core interaction

1. **Drag** — the icon can be placed anywhere on screen and stays there.
2. **Click** — opens a vertical dock of icons next to it (flips side depending on screen space).
3. **Tap a dock icon** — opens that section as a panel beside the dock. Tapping a different icon swaps the panel; tapping the active one again collapses it. The dock stays open until you dismiss it.

## Sections

| Section | What it does |
|---|---|
| **Notes** | Quick text capture, timestamped, deletable |
| **Tasks** | Checklist — add, check off, delete |
| **Clips** | Reuse copied text — grab current clipboard or paste manually |
| **Capture** | Screenshot / screen recording — placeholder for now, not in v1 |

## Constraints

- Platform: Ubuntu
- Storage: fully local, no sync (for now)

## Implementation note

A browser can't truly float an icon over every window — that needs a native shell. Realistic options:

- **Tauri** — small footprint, Rust backend, this prototype's UI could carry over almost as-is as the webview
- **Electron** — heavier, but faster to get a first build running
- **GTK + layer-shell** — most native GNOME/Ubuntu feel, more setup work

## Prototype

Interactive click-through: [Nook — Desktop Companion Prototype](https://claude.ai/artifact/PLcwvQMz8CPDNaCHbijGmd)
