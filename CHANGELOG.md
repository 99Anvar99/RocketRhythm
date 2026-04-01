# Changelog

All notable changes to this project are documented in this file.

## [1.5.0] - 2026-03-31

### Improved
- Improved media metadata refresh behavior after track changes for more reliable album artwork updates.

### Fixed
- Fixed album artwork not updating when the song changed.
- Fixed stale or missing cover art persisting until another media event forced a refresh.

## [1.4.0] - 2026-02-12

### Added
- Added safer font initialization behavior to better handle missing fonts on first run with a download-first workflow.

### Fixed
- Fixed an ImGui crash/assert (`font_cfg.FontData == 0`) caused by reusing the same `ImFontConfig` across multiple font loads.
- Fixed subsequent `LoadFont` calls breaking after the loader modified a reused font config.

### Improved
- Improved font-loading stability and consistency when building multiple font sizes (overlay and settings) from the same TTF.

## [1.3.0] - 2026-02-11

### Added
- Added a new `Time Display` setting for the progress bar with two modes: centered `current / total` or corners (`current` left, `total` right).
- Added a new ping-pong marquee mode for long title, artist, and album text with configurable pause and speed.
- Added new config keys: `time_display_mode`, `enable_marquee`, `marquee_speed_px`, and `marquee_wait_sec`.

### Improved
- Improved progress bar rendering with safer rounding behavior for both background and fill.
- Bumped the config version and added validation and clamping for new fields.

## [1.2.3] - 2026-02-10

### Fixed
- Fixed ImGui font atlas merge order so fallback glyphs merge into the correct base font.
- Fixed missing or garbled CJK, Thai, and Cyrillic characters caused by merging 24px fallbacks into the 16px font.

### Improved
- Built two separate font families (24px overlay and 16px settings), each with its own merged fallbacks.
- Improved font sizing consistency and reduced random-looking glyph fallback behavior.

## [1.2.2] - 2026-02-10

### Added
- Added automatic config creation on first run.
- Added automatic config reset when an incompatible config version is detected.

### Improved
- Updated default settings to match the new preset.
- Improved config loading and validation for better fault tolerance.

### Fixed
- Fixed miscellaneous stability issues and minor bugs.

## [1.1.0] - 2026-02-08

### Improved
- Internal improvements and minor refinements.

### Fixed
- Various bug fixes and stability improvements.

## [1.0.0] - 2026-02-05

### Added
- Added real-time media detection and track metadata display for title, artist, and album.
- Added playback state and progress tracking.
- Added international character support with custom font rendering.
- Added automatic album artwork detection with disk and GPU texture caching plus placeholder fallback.
- Added DPI-aware UI scaling with automatic and manual scaling options, plus a resizable overlay.
- Added smooth progress interpolation, optional pulse animation, and scrolling text for long metadata.
- Added performance-focused runtime behavior with optimized texture handling and thread-safe polling.
