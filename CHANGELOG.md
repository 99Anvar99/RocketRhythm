# Changelog

All notable changes to this project are documented in this file.

## [2.0.1] - 2026-05-13

### Improved
- Cleaned up the settings UI with a simpler compact tab layout to avoid broken column state and visual divider artifacts.
- Improved Spotify playback progress handling so timestamps advance smoothly between API polls.
- Improved general media progress smoothing so stale media positions no longer cause small backward jumps.

### Fixed
- Fixed Spotify polling causing the displayed timestamp to jump backwards when the API returned slightly stale progress data.
- Fixed settings UI layout issues caused by the custom sidebar shell and nested ImGui columns.
- Fixed the BakkesPlugins release package workflow to continue generating an upload-ready zip after Release builds.

## [2.0.0] - 2026-05-12

### Added
- Added optional Spotify Web API integration with account authorization.
- Added Spotify playback controls for play, pause, previous, and next.
- Added Spotify liked-track toggling from the settings UI.
- Added Spotify metadata override support so the overlay can use Spotify track data when enabled.
- Added Spotify album artwork downloading and local caching when artwork is not already available.
- Added support for displaying all Spotify artist names instead of only the first artist.

### Improved
- Redesigned the settings UI with a compact left-sidebar layout and darker content panel.
- Improved media handling so Spotify artwork downloads in the background without blocking rendering.
- Improved album art behavior by reusing cached Spotify artwork and falling back to Windows media artwork when available.
- Improved Spotify authorization to use the configured local redirect URI: `http://127.0.0.1:9982/callback`.
- Improved settings layout density with tighter controls and fewer tall sections.

### Fixed
- Fixed an ImGui assertion caused by unsafe settings child-window/card layout.
- Fixed Spotify API requests with query strings by preserving URL extra info in WinHTTP requests.
- Fixed Spotify PKCE authorization by sending the required code challenge and handling the local callback.
- Fixed the About settings page divider and removed the duplicate Support section.

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
