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
  A modern real-time music overlay for Rocket League
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

**RocketRhythm** is a high-performance BakkesMod plugin that displays your currently playing music directly inside Rocket League through a clean, responsive, and visually polished overlay.

The plugin is fully built using **Dear ImGui**, ensuring smooth rendering and minimal performance overhead.

RocketRhythm integrates with:

👉 **Windows Global System Media Transport Controls (GSMTC)**

This allows the plugin to automatically detect and display media from most Windows-compatible media players. Advanced Spotify API integration is planned for future releases.

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
- Track title
- Artist name
- Album information
- Playback progress tracking
- Play and pause state detection

---

## 🌐 Language Support
- Supports most international character sets
- Custom font rendering for improved readability
- Clean and consistent text layout

---

## 🖼 Album Artwork
- Automatic album artwork detection
- Persistent disk caching system
- GPU texture caching using LRU optimization
- Automatic placeholder fallback when artwork is unavailable

---

## 📏 Smart UI Scaling
- DPI-aware scaling for high-resolution displays
- Automatic resolution-based scaling
- Manual UI scaling override
- Dynamic overlay auto-resizing
- Resizable overlay using corner dragging

---

## 🎬 Smooth Animations
- Smooth playback progress interpolation
- Optional pulse animation synchronized with playback
- Automatic scrolling for long track titles and metadata

---

## ⚡ Performance Focused
- Near-zero ImGui memory allocations during runtime
- Thread-safe media polling system
- Optimized GPU texture upload pipeline
- Memory-safe resource and texture management

---

# 📥 Installation

## ⭐ Recommended Installation (Releases)

👉 Download the latest version:

### 🔽 [Download Latest Release](https://github.com/99Anvar99/RocketRhythm/releases/latest)

1. Download the `.dll` file  
2. Place the file inside:

BakkesMod/plugins/

3. Launch Rocket League  
4. Enable the plugin via the BakkesMod plugin manager  

---

## 🛠 Manual Build

### Requirements
- Visual Studio 2022 or newer
- BakkesMod SDK
- Windows 10 or newer

### Build Steps

git clone https://github.com/99Anvar99/RocketRhythm

Open the solution in Visual Studio and build the plugin.

---

# ⚙️ Plugin Settings

| Setting | Description |
|----------|-------------|
| Enable Overlay | Toggles the music overlay visibility |
| Hide When Not Playing | Automatically hides overlay when no music is detected |
| UI Scale | Manually adjusts overlay size |
| Enable Pulse | Enables playback pulse animation |
| Show Album Art | Displays album artwork |
| Show Progress Bar | Displays playback progress bar |
| Show Album Info | Displays album metadata |

---

# 📂 Configuration

RocketRhythm automatically stores configuration data in:

BakkesMod/data/RocketRhythm/config.json

---

# ❤️ Credits

Developed by:

### Mister9982

---

# 📜 License

Distributed under the MIT License. See LICENSE for details.

---

<p align="center">
  Built for Rocket League players who enjoy listening to music while competing 🎶
</p>
