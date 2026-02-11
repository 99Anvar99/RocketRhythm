<!-- ================================================== -->
<!-- Banner / Logo -->
<!-- ================================================== -->

<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="https://github.com/99Anvar99/RocketRhythm/blob/main/images/RocketRhythm.png?raw=true">
    <img alt="RocketRhythm Logo" src="https://github.com/99Anvar99/RocketRhythm/blob/main/images/RocketRhythm.png?raw=true" width="160">
  </picture>
</p>

<h1 align="center">🎵 RocketRhythm</h1>

<p align="center">
  A modern real-time music overlay for Rocket League (BakkesMod)
</p>

<p align="center">
  <a href="https://github.com/99Anvar99/RocketRhythm/releases">
    <img src="https://img.shields.io/github/v/release/99Anvar99/RocketRhythm?style=for-the-badge">
  </a>
  <a href="LICENSE">
    <img src="https://img.shields.io/github/license/99Anvar99/RocketRhythm?style=for-the-badge">
  </a>
  <a href="https://github.com/99Anvar99/RocketRhythm/stargazers">
    <img src="https://img.shields.io/github/stars/99Anvar99/RocketRhythm?style=for-the-badge">
  </a>
  <a href="https://github.com/99Anvar99/RocketRhythm/issues">
    <img src="https://img.shields.io/github/issues/99Anvar99/RocketRhythm?style=for-the-badge">
  </a>
</p>

<p align="center">
  <a href="https://bakkesplugins.com/plugins/view">
    <img src="https://img.shields.io/badge/BakkesMod-Plugin-blue?style=for-the-badge">
  </a>
  <img src="https://img.shields.io/badge/Platform-Windows-0078D6?style=for-the-badge">
</p>

---

# 🚀 Overview

**RocketRhythm** is a high-performance BakkesMod plugin that shows your currently playing music inside Rocket League through a clean, responsive, and polished overlay.

Built with **Dear ImGui** for smooth rendering and minimal overhead.

RocketRhythm reads your active media session via:

👉 **Windows Global System Media Transport Controls (GSMTC)**

This lets the plugin automatically detect media from most Windows-compatible players (Spotify, browsers, etc.). Spotify API integration is planned for future releases.

---

# 🖥 Preview

<p align="center">
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="https://github.com/99Anvar99/RocketRhythm/blob/main/images/plugin.png?raw=true">
  <img alt="RocketRhythm Preview" src="https://github.com/99Anvar99/RocketRhythm/blob/main/images/plugin.png?raw=true" width="500">
</picture>
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="https://github.com/99Anvar99/RocketRhythm/blob/main/images/plugin_settings.png?raw=true">
  <img alt="RocketRhythm Settings" src="https://github.com/99Anvar99/RocketRhythm/blob/main/images/plugin_settings.png?raw=true" width="500">
</picture>
</p>

---

# ✨ Features

## 🎧 Real-Time Media Detection
- Title / artist / album metadata
- Play / pause state
- Smooth progress tracking

## 🖼 Album Artwork
- Automatic album art detection (when available)
- Persistent disk caching
- GPU texture caching
- Clean placeholder fallback

## 📏 Smart UI Scaling
- DPI-aware scaling
- Optional auto scaling (resolution + DPI)
- Manual scaling via `rr_uiscale`
- Resizable overlay (corner dragging)

## 🎬 UI Polish
- Smooth progress interpolation + optional pulse animation
- Ping-pong marquee scrolling for long metadata
- **Time display modes:** centered (`current / total`) or corners (left/right)

## 🌐 Language Support
- International character support (CJK/Thai/Cyrillic)
- Font fallback merging for consistent rendering

## ⚡ Performance Focused
- Thread-safe media polling
- Optimized texture handling
- Minimal ImGui overhead during runtime

---

# 📥 Installation

## ⭐ Recommended (Releases)

### 🔽 Download Latest Release
https://github.com/99Anvar99/RocketRhythm/releases/latest

1. Download the `.dll`
2. Place it in:
   - `BakkesMod/plugins/`
3. Launch Rocket League
4. Enable RocketRhythm in the BakkesMod Plugin Manager

---

## 🛠 Manual Build

### Requirements
- Visual Studio 2022+
- BakkesMod SDK
- Windows 10+

### Build Steps
```bash
git clone https://github.com/99Anvar99/RocketRhythm
```

Open the solution in Visual Studio and build.

---

# ⚙️ CVars

```txt
rr_enabled = 1
rr_uiscale = 1.0
```

---

# ⚙️ Plugin Settings

| Setting | Description |
|---|---|
| Enable Plugin | Enables/disables RocketRhythm |
| Hide When Not Playing | Auto-hides overlay when no active media session |
| Enable Auto Scaling | Scales UI based on screen resolution + DPI |
| UI Scale Multiplier | Manual multiplier (also tied to `rr_uiscale`) |
| Background / Accent | Overlay color controls |
| Opacity | Overlay opacity |
| Window Rounding | Window corner rounding |
| Album Art Size | Album art size control |
| Show Album Art | Toggle album artwork |
| Show Progress Bar | Toggle progress bar |
| Show Album Info | Toggle album field |
| Enable Pulse Effect | Subtle pulse animation when playing |
| Marquee Scrolling | Ping-pong scrolling for long metadata |
| Time Display Mode | Centered `current / total` or corners (left/right) |

---

# 📂 Configuration

RocketRhythm saves config to:

- `BakkesMod/data/RocketRhythm/config.json`

If the config version is incompatible, RocketRhythm resets to defaults and regenerates the file automatically.

---

# 🧯 Troubleshooting

**No track info?**
- Use a player that publishes GSMTC (Spotify or a Chromium browser tab).
- Some apps only publish sessions while audio is playing.

**Plugin not showing?**
- Ensure `RocketRhythm.dll` is in `BakkesMod/plugins/`
- Enable it in Plugin Manager.

**Scaling looks off?**
- Disable Auto Scaling and test `rr_uiscale`.
- High Windows display scaling may require Auto Scaling ON with a lower multiplier.

**Album art missing?**
- Some players don’t provide artwork for every track.
- Switching tracks can force a refresh.

---

# ❤️ Credits

Developed by **Mister9982**

---

# 📜 License

Distributed under the MIT License. See `LICENSE` for details.

---

<p align="center">
  Built for Rocket League players who enjoy listening to music while competing 🎶
</p>
