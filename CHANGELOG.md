# Changelog

All notable changes to PXB Preview are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.0.2] - 2026-09-04

### Added

- **UI themes**: dark / light / follow-system appearance via the new
  `主题 / Theme` menu. System mode follows Windows `AppsUseLightTheme` live
  (re-checked while running), and the choice is persisted in `config.ini`.
  The light palette is a tuned neutral scheme; the dark theme keeps the
  exact 0.0.1 look.
- **Layer visibility that actually filters the picture**: for
  `texture_table` exports that carry per-layer previews, hiding a layer now
  alpha-blends the remaining visible layers and displays the result —
  instead of only recording checkbox state.
- **`texture_table` format support** (exports without `preview_table`):
  decode the inline `metadata.thumbnail_url` data URI as the document
  thumbnail, scan the source region for per-frame / per-layer
  `preview_data_url` records, order frames by index and honor explicit
  per-frame `duration` field.
- **File-list metadata rows**: each sibling row shows the file size and
  last-modified time in a smaller font below the (two-line, ellipsis-clamped)
  title.
- **View-layout persistence**: pane toggles, splitter widths, minimap
  placement and the theme survive restarts via a portable-first
  `config.ini` (exe directory when writable, `%APPDATA%` fallback).

### Changed

- File-list thumbnails are no longer stretched: the 44×44 box letterboxes
  the art (aspect ratio preserved, centered, transparent margins).
- Windows Explorer thumbnails are decoded at 256 px instead of 88 px and
  letterboxed, so rectangular images no longer render squeezed and stay
  sharp at large icon sizes.
- The preview panel dropped its title bar and separator — the canvas now
  runs edge-to-edge.
- Default file-list width increased from 170 px to 200 px.
- Switching files inside one directory no longer re-decompresses and
  re-uploads every sibling thumbnail (metadata cache is kept when the
  directory is unchanged).

### Fixed

- The focused file's row kept showing the first file's thumbnail forever:
  the document thumbnail GPU texture (key `-999`) was never cleared on file
  switch and was reused by key without re-uploading.
- `path_parent()` mishandled backslash-only Windows paths (open dialog,
  drag & drop, argv), collapsing them to the current directory.

### Docs

- `docs/pxb-format-spec.md`: document the `texture_table` (no
  `preview_table`) variant, its data-URI landscape and the reader fallback
  strategy.
- `AGENTS.md`: font-atlas guidance (common CJK subset + `zh_strings()`
  merge) and the texture-key conventions.

## [0.0.1] - 2026-08-21

- Initial release: multi-frame PXB previewer (C++17, SDL3 + Dear ImGui),
  gzip-compressed `.pxb` parsing, sibling file navigation, Windows
  double-click association and Explorer thumbnail handler, zh/en UI.