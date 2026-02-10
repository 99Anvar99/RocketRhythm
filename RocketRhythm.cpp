#include "pch.h"
#include "RocketRhythm.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <ShlObj.h>
#include <Windows.h>
#include <cmath>
#include "notification.h"
#include "IMGUI/imgui_internal.h"
#include "version.h"
#include <Windows.h>
#include <shellapi.h>
#include <nlohmann/json.hpp>

std::shared_ptr<CVarManagerWrapper> _globalCvarManager;

const auto plugin_version = std::string(stringify(VERSION_MAJOR)) + "." + stringify(VERSION_MINOR) + "." + stringify(VERSION_PATCH) + "." + stringify(VERSION_BUILD);
const std::string CONFIG_FILE_NAME = "config.json";
const std::string CONFIG_DIR = "RocketRhythm";
const std::string PLUGIN_NAME_STR = "RocketRhythm";

BAKKESMOD_PLUGIN(RocketRhythm, "RocketRhythm", plugin_version.c_str(), PLUGINTYPE_THREADED)

static std::string format_time(int seconds)
{
    if (seconds <= 0) return "0:00";
    int minutes = seconds / 60;
    int secs = seconds % 60;
    char buffer[16];
    snprintf(buffer, sizeof(buffer), "%d:%02d", minutes, secs);
    return std::string(buffer);
}

static bool IsValidImageFile(const std::string& path)
{
    try
    {
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) return false;
        file.seekg(0, std::ios::end);
        return file.tellg() > 100;
    }
    catch (...)
    {
        return false;
    }
}

static nlohmann::json ImVec4ToJson(const ImVec4& c)
{
    return nlohmann::json::array({ c.x, c.y, c.z, c.w });
}

static bool JsonToImVec4(const nlohmann::json& j, ImVec4& out)
{
    if (!j.is_array() || j.size() != 4) return false;
    for (int i = 0; i < 4; ++i)
        if (!j[i].is_number()) return false;

    out.x = j[0].get<float>();
    out.y = j[1].get<float>();
    out.z = j[2].get<float>();
    out.w = j[3].get<float>();
    return true;
}

template <typename T>
static void AssignIfNumberOrBool(const nlohmann::json& obj, const char* key, T& target)
{
    auto it = obj.find(key);
    if (it == obj.end()) return;

    if constexpr (std::is_same_v<T, bool>)
    {
        if (it->is_boolean())
            target = it->get<bool>();
    }
    else
    {
        if (it->is_number())
        {
            // read as double to avoid template mismatch / overflow surprises
            const double v = it->get<double>();
            target = static_cast<T>(v);
        }
    }
}

static RocketRhythm::WindowStyle DefaultWindowStyle() { return RocketRhythm::WindowStyle{}; }

void to_json(nlohmann::json& j, const RocketRhythm::WindowStyle& s)
{
    j = nlohmann::json{
        {"background_color", ImVec4ToJson(s.backgroundColor)},
        {"accent_color",     ImVec4ToJson(s.accentColor)},
        {"accent_color2",    ImVec4ToJson(s.accentColor2)},
        {"text_color",       ImVec4ToJson(s.textColor)},
        {"text_color_dim",   ImVec4ToJson(s.textColorDim)},
        {"text_color_faint", ImVec4ToJson(s.textColorFaint)},

        {"window_rounding",       s.windowRounding},
        {"album_art_rounding",    s.albumArtRounding},
        {"progress_bar_height",   s.progressBarHeight},
        {"progress_bar_rounding", s.progressBarRounding},
        {"album_art_size",        s.albumArtSize},
        {"enable_pulse",          s.enablePulse},

        {"show_album_art",     s.showAlbumArt},
        {"show_progress_bar",  s.showProgressBar},
        {"show_album_info",    s.showAlbumInfo},
        {"window_opacity",     s.windowOpacity},

        {"ui_scale",            s.uiScale},
        {"enable_auto_scaling", s.enableAutoScaling},
        {"min_scale",           s.minScale},
        {"max_scale",           s.maxScale},
    };
}

void from_json(const nlohmann::json& j, RocketRhythm::WindowStyle& s)
{
    const RocketRhythm::WindowStyle def = DefaultWindowStyle();
    s = def;

    if (!j.is_object()) return;

    auto loadColor = [&](const char* key, ImVec4& target, const ImVec4& fallback)
    {
        auto it = j.find(key);
        if (it == j.end()) { target = fallback; return; }
        ImVec4 tmp;
        if (JsonToImVec4(*it, tmp)) target = tmp;
        else target = fallback;
    };

    loadColor("background_color", s.backgroundColor, def.backgroundColor);
    loadColor("accent_color",     s.accentColor,     def.accentColor);
    loadColor("accent_color2",    s.accentColor2,    def.accentColor2);
    loadColor("text_color",       s.textColor,       def.textColor);
    loadColor("text_color_dim",   s.textColorDim,    def.textColorDim);
    loadColor("text_color_faint", s.textColorFaint,  def.textColorFaint);

    AssignIfNumberOrBool(j, "window_rounding",       s.windowRounding);
    AssignIfNumberOrBool(j, "album_art_rounding",    s.albumArtRounding);
    AssignIfNumberOrBool(j, "progress_bar_height",   s.progressBarHeight);
    AssignIfNumberOrBool(j, "progress_bar_rounding", s.progressBarRounding);
    AssignIfNumberOrBool(j, "album_art_size",        s.albumArtSize);

    AssignIfNumberOrBool(j, "window_opacity",        s.windowOpacity);
    AssignIfNumberOrBool(j, "ui_scale",              s.uiScale);
    AssignIfNumberOrBool(j, "min_scale",             s.minScale);
    AssignIfNumberOrBool(j, "max_scale",             s.maxScale);

    AssignIfNumberOrBool(j, "enable_pulse",          s.enablePulse);
    AssignIfNumberOrBool(j, "show_album_art",        s.showAlbumArt);
    AssignIfNumberOrBool(j, "show_progress_bar",     s.showProgressBar);
    AssignIfNumberOrBool(j, "show_album_info",       s.showAlbumInfo);
    AssignIfNumberOrBool(j, "enable_auto_scaling",   s.enableAutoScaling);

    auto clampf = [](float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; };

    s.windowOpacity = clampf(s.windowOpacity, 0.0f, 1.0f);
    s.uiScale = clampf(s.uiScale, 0.5f, 2.0f);
    s.minScale = clampf(s.minScale, 0.1f, 10.0f);
    s.maxScale = clampf(s.maxScale, 0.1f, 10.0f);
    if (s.minScale > s.maxScale) std::swap(s.minScale, s.maxScale);

    s.windowRounding = clampf(s.windowRounding, 0.0f, 50.0f);
    s.albumArtRounding = clampf(s.albumArtRounding, 0.0f, 50.0f);
    s.progressBarHeight = clampf(s.progressBarHeight, 0.0f, 50.0f);
    s.progressBarRounding = clampf(s.progressBarRounding, 0.0f, 50.0f);
    s.albumArtSize = clampf(s.albumArtSize, 16.0f, 512.0f);
}

// ---------------------------
RocketRhythm::RocketRhythm()
    : lastProgressUpdate(std::chrono::steady_clock::now()) {}

RocketRhythm::~RocketRhythm() = default;

// ---------------------------
void RocketRhythm::onLoad()
{
    _globalCvarManager = cvarManager;
    media = CreateMediaController(gameWrapper->GetDataFolder().string());

    enabled = std::make_shared<bool>(true);
    uiScaleCVar = std::make_shared<float>(1.0f);

    cvarManager->registerCvar("rr_enabled", "1", "Enable RocketRhythm").bindTo(enabled);
    cvarManager->registerCvar("rr_uiscale", "1.0", "UI Scale factor", true, true, 0.5f, true, 2.0f).bindTo(uiScaleCVar);

    LoadConfig();

    if (!fontsInitialized)
    {
        InitializeFont();
    }

    gameWrapper->RegisterDrawable([this](const CanvasWrapper& canvas) { RenderCanvas(canvas); });

    LOG("{} v{} loaded!", PLUGIN_NAME_STR, plugin_version);
}

void RocketRhythm::onUnload()
{
    SaveConfig();
    albumArtTexture.reset();
    cvarManager->removeCvar("rr_enabled");
    cvarManager->removeCvar("rr_uiscale");
    LOG("{} unloaded!", PLUGIN_NAME_STR);
}

// ---------------------------
void RocketRhythm::InitializeFont()
{
    if (!ImGui::GetCurrentContext())
        return;

    ImGuiIO& io = ImGui::GetIO();
    if (!io.Fonts)
        return;

    // Build merged glyph ranges once
    ImFontGlyphRangesBuilder builder;
    const ImWchar* ranges[] = {
        io.Fonts->GetGlyphRangesDefault(),
        io.Fonts->GetGlyphRangesCyrillic(),
        io.Fonts->GetGlyphRangesJapanese(),
        io.Fonts->GetGlyphRangesChineseSimplifiedCommon(),
        io.Fonts->GetGlyphRangesKorean(),
        io.Fonts->GetGlyphRangesThai()
    };
    for (auto r : ranges) builder.AddRanges(r);
    builder.BuildRanges(&mergedGlyphRanges);

    PWSTR pathToFonts = nullptr;
    if (!SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Fonts, 0, nullptr, &pathToFonts)))
        return;

    std::filesystem::path fontsPath = pathToFonts;
    CoTaskMemFree(pathToFonts);

    auto segoePath   = (fontsPath / "segoeui.ttf").string();
    auto chinesePath = (fontsPath / "msyh.ttc").string();    // YaHei
    auto japanesePath= (fontsPath / "meiryo.ttc").string();
    auto koreanPath  = (fontsPath / "malgun.ttf").string();

    if (!std::filesystem::exists(segoePath))
        return;

    ImFontConfig base{};
    base.PixelSnapH = true;
    base.OversampleH = 2;
    base.OversampleV = 2;

    ImFontConfig merge{};
    merge.MergeMode = true;
    merge.PixelSnapH = true;

    auto addFallbacks = [&](float sizePx)
    {
        if (std::filesystem::exists(chinesePath))
            io.Fonts->AddFontFromFileTTF(chinesePath.c_str(), sizePx, &merge, mergedGlyphRanges.Data);
        if (std::filesystem::exists(japanesePath))
            io.Fonts->AddFontFromFileTTF(japanesePath.c_str(), sizePx, &merge, mergedGlyphRanges.Data);
        if (std::filesystem::exists(koreanPath))
            io.Fonts->AddFontFromFileTTF(koreanPath.c_str(), sizePx, &merge, mergedGlyphRanges.Data);
    };

    // ---- 24px family (add base, then merge fallbacks into *that* base)
    customFontSegoeUI = io.Fonts->AddFontFromFileTTF(
        segoePath.c_str(), 24.0f, &base, mergedGlyphRanges.Data
    );
    if (customFontSegoeUI)
        addFallbacks(24.0f);

    // ---- 16px family (add base, then merge fallbacks into *that* base)
    customSettingsFontUI = io.Fonts->AddFontFromFileTTF(
        segoePath.c_str(), 16.0f, &base, mergedGlyphRanges.Data
    );
    if (customSettingsFontUI)
        addFallbacks(16.0f);

    io.Fonts->Build();

    fontsInitialized = customFontSegoeUI != nullptr && customSettingsFontUI != nullptr;
}

// ---------------------------
void RocketRhythm::LoadAlbumArt(const std::string& path)
{
    if (path.empty() || !IsValidImageFile(path))
    {
        albumArtLoaded = false;
        albumArtTexture.reset();
        currentAlbumArtPath.clear();
        return;
    }

    if (albumArtLoaded && albumArtTexture && currentAlbumArtPath == path)
    {
        return;
    }

    try
    {
        albumArtTexture.reset();
        albumArtTexture = std::make_shared<ImageWrapper>(path, false, true);

        if (albumArtTexture)
        {
            albumArtLoaded = true;
            currentAlbumArtPath = path;
        }
        else
        {
            albumArtLoaded = false;
            currentAlbumArtPath.clear();
        }
    }
    catch (const std::exception& e)
    {
        albumArtLoaded = false;
        albumArtTexture.reset();
        currentAlbumArtPath.clear();
        LOG("Album art load error: {}", e.what());
    }
}

// ---------------------------
int RocketRhythm::GetCurrentDisplayPosition()
{
    const auto now = std::chrono::steady_clock::now();

    // Track identity
    const std::string songKey = mediaState.title + "|" + mediaState.artist + "|" + mediaState.album;

    // Re-anchor when track changes or when the reported position changes (i.e., we received a real update)
    if (songKey != lastInterpSongKey || mediaState.positionSec != lastPositionSec)
    {
        lastInterpSongKey = songKey;
        lastPositionSec = mediaState.positionSec;
        interpolatedPositionSec = mediaState.positionSec;
        lastProgressUpdate = now; // anchor time for interpolation
    }

    if (!mediaState.isPlaying || mediaState.durationSec <= 0)
    {
        return std::clamp(mediaState.positionSec, 0, mediaState.durationSec > 0 ? mediaState.durationSec : 0);
    }

    const auto elapsedSec = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastProgressUpdate).count() / 1000;

    const int predicted = interpolatedPositionSec + static_cast<int>(elapsedSec);
    return std::clamp(predicted, 0, mediaState.durationSec);
}

// ---------------------------

void RocketRhythm::UpdateTextScroll(float deltaTime)
{
    const std::string newSongHash = mediaState.title + "|" + mediaState.artist + "|" + mediaState.album;

    if (newSongHash != currentSongHash)
    {
        scrollStates.clear();
        currentSongHash = newSongHash;
    }

    for (auto& st : std::views::values(scrollStates))
    {
        if (!st.needsScrolling) continue;

        st.offset += scrollSpeed * deltaTime;
        if (st.offset > st.textWidth + 100.0f)
        {
            st.offset = -60.0f;
        }
    }
}

// ---------------------------

void RocketRhythm::DrawScrollableText(const std::string& text, const ImVec4& color)
{
    if (text.empty())
    {
        ImGui::Text("");
        return;
    }
    
    // Get current available width
    float availableTextWidth = ImGui::GetContentRegionAvail().x;
    
    // Calculate text width
    float textWidth = ImGui::CalcTextSize(text.c_str()).x;
    
    // Use text as key for scroll state
    ScrollState& state = scrollStates[text];
    state.textWidth = textWidth;
    
    // Check if scrolling is needed
    state.needsScrolling = (state.textWidth > availableTextWidth);
    
    if (!state.needsScrolling)
    {
        ImGui::TextColored(color, "%s", text.c_str());
        state.offset = 0.0f;
        return;
    }
    
    // Get cursor position
    ImVec2 cursorPos = ImGui::GetCursorScreenPos();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    
    // Set up clipping rectangle
    float clipHeight = ImGui::GetTextLineHeight();
    ImVec2 clipMin = cursorPos;
    ImVec2 clipMax = ImVec2(cursorPos.x + availableTextWidth, cursorPos.y + clipHeight);
    
    // Save current clip rect and set new one
    draw_list->PushClipRect(clipMin, clipMax, true);
    
    // Draw scrolling text
    ImVec2 textPos = ImVec2(cursorPos.x - state.offset, cursorPos.y);
    draw_list->AddText(textPos, ImGui::GetColorU32(color), text.c_str());
    
    // Draw duplicate text for seamless looping
    if (state.offset > 0)
    {
        ImVec2 loopTextPos = ImVec2(cursorPos.x + state.textWidth + 40.0f - state.offset, cursorPos.y);
        draw_list->AddText(loopTextPos, ImGui::GetColorU32(color), text.c_str());
    }
    
    // Restore clip rect
    draw_list->PopClipRect();
    
    // Move cursor down for next line
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + clipHeight);
}

// ---------------------------

float RocketRhythm::GetDPIScaleFactor()
{
    HDC screen = GetDC(nullptr);
    float dpiScaleX = static_cast<float>(GetDeviceCaps(screen, LOGPIXELSX)) / 96.0f;
    ReleaseDC(nullptr, screen);
    return dpiScaleX;
}

float RocketRhythm::CalculateAutoScaleFactor()
{
    ImVec2 displaySize = ImGui::GetIO().DisplaySize;

    constexpr float baseWidth = 1920.0f;
    constexpr float baseHeight = 1080.0f;

    const float heightRatio = displaySize.y / baseHeight;
    const float widthRatio = displaySize.x / baseWidth;

    const float resolutionScale = std::min(heightRatio, widthRatio);
    const float dpiScale = GetDPIScaleFactor();

    float autoScale = resolutionScale * dpiScale;
    autoScale = std::clamp(autoScale, windowStyle.minScale, windowStyle.maxScale);
    return autoScale;
}

float RocketRhythm::GetEffectiveScaleFactor()
{
    windowStyle.uiScale = *uiScaleCVar;

    float scaleFactor = windowStyle.enableAutoScaling ? CalculateAutoScaleFactor() : 1.0f;
    scaleFactor *= windowStyle.uiScale;

    return std::clamp(scaleFactor, 0.5f, 3.0f);
}

float RocketRhythm::GetScaledValue(float baseValue)
{
    return baseValue * GetEffectiveScaleFactor();
}

// ---------------------------

void RocketRhythm::DrawAlbumArtPlaceholderWithScale(float scale)
{
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    float size = windowStyle.albumArtSize * scale;
    
    ImU32 color_top = ImGui::GetColorU32(ImVec4(0.15f, 0.15f, 0.18f, 1.0f));
    ImU32 color_bottom = ImGui::GetColorU32(ImVec4(0.12f, 0.12f, 0.15f, 1.0f));
    
    draw_list->AddRectFilledMultiColor(pos, ImVec2(pos.x + size, pos.y + size),
        color_top, color_top, color_bottom, color_bottom);
    
    float center_x = pos.x + size * 0.5f;
    float center_y = pos.y + size * 0.5f;
    
    for (int i = 0; i < 3; i++)
    {
        float radius = size * 0.25f + i * (8.0f * scale);
        ImU32 ring_color = ImGui::GetColorU32(ImVec4(0.3f, 0.3f, 0.35f, 0.2f));
        draw_list->AddCircle(ImVec2(center_x, center_y), radius, ring_color, 0, 1.5f);
    }
    
    ImU32 note_color = ImGui::GetColorU32(windowStyle.accentColor);
    float font_size = ImGui::GetFontSize() * 2.0f;
    float offset = 15.0f * scale;
    draw_list->AddText(ImGui::GetFont(), font_size, ImVec2(center_x - offset, center_y - offset), note_color, "♪");
    
    ImU32 border_color = ImGui::GetColorU32(windowStyle.accentColor);
    draw_list->AddRect(pos, ImVec2(pos.x + size, pos.y + size),
        border_color, windowStyle.albumArtRounding * scale, 0, 2.0f);
    
    ImGui::SetCursorPos(ImGui::GetCursorPos() + ImVec2(size + (25.0f * scale), 0));
}

void RocketRhythm::DrawAlbumArtWithScale(float scale)
{
    // Check if we should load album art
    if (mediaState.hasAlbumArt && !mediaState.albumArtPath.empty())
    {
        LoadAlbumArt(mediaState.albumArtPath);
    }
    
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    float size = windowStyle.albumArtSize * scale;
    
    if (albumArtLoaded && albumArtTexture && albumArtTexture->IsLoadedForImGui())
    {
        if (ImTextureID texture_id = albumArtTexture->GetImGuiTex())
        {
            ImGui::Image(texture_id, ImVec2(size, size));
            ImU32 border_color = ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, 0.1f));
            draw_list->AddRect(pos, ImVec2(pos.x + size, pos.y + size), border_color, windowStyle.albumArtRounding * scale, 0, 1.0f);
            ImGui::SetCursorPos(ImGui::GetCursorPos() + ImVec2(size + 15.0f * scale, 0));
            return;
        }
    }
    
    // Fallback to placeholder
    DrawAlbumArtPlaceholderWithScale(scale);
}

// ---------------------------

void RocketRhythm::DrawAlbumArt()
{
    if (!windowStyle.showAlbumArt) return;
    
    // Get current dynamic scale from window size
    ImVec2 currentWindowSize = ImGui::GetWindowSize();
    float baseWindowWidth = windowStyle.showAlbumArt ?
        windowStyle.albumArtSize + 15.0f + 235.0f + 15.0f : 360.0f;
    float dynamicScale = currentWindowSize.x / baseWindowWidth;
    dynamicScale = std::max(0.5f, std::min(dynamicScale, 3.0f));
    
    DrawAlbumArtWithScale(dynamicScale);
}

// ---------------------------

void RocketRhythm::UpdateAnimation(float deltaTime)
{
    if (mediaState.isPlaying && windowStyle.enablePulse)
    {
        pulseAnimation += deltaTime * 2.0f;
        if (pulseAnimation > 6.28318f) pulseAnimation -= 6.28318f;
    }
}

// ---------------------------

void RocketRhythm::DrawProgressBar()
{
    if (!windowStyle.showProgressBar || mediaState.durationSec <= 0) return;
    
    int currentPosition = GetCurrentDisplayPosition();
    float progress = static_cast<float>(currentPosition) / mediaState.durationSec;
    progress = std::clamp(progress, 0.0f, 1.0f);
    
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    float width = ImGui::GetContentRegionAvail().x;
    float height = GetScaledValue(windowStyle.progressBarHeight);
    
    ImU32 bg_color = ImGui::GetColorU32(ImVec4(0.15f, 0.15f, 0.20f, 0.8f));
    draw_list->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + height),
        bg_color, GetScaledValue(windowStyle.progressBarRounding));
    
    if (progress > 0)
    {
        float fill_width = width * progress;
        ImVec4 color1 = windowStyle.accentColor;
        ImVec4 color2 = windowStyle.accentColor2;
        
        if (mediaState.isPlaying && windowStyle.enablePulse)
        {
            float pulse = 0.8f + 0.2f * sin(pulseAnimation);
            color1.x *= pulse;
            color1.y *= pulse;
            color1.z *= pulse;
            color2.x *= pulse;
            color2.y *= pulse;
            color2.z *= pulse;
        }
        
        ImU32 col1 = ImGui::GetColorU32(color1);
        ImU32 col2 = ImGui::GetColorU32(color2);
        draw_list->AddRectFilledMultiColor(pos, ImVec2(pos.x + fill_width, pos.y + height),
            col1, col2, col2, col1);
        
        ImU32 highlight = ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, 0.2f));
        draw_list->AddLine(ImVec2(pos.x, pos.y + 1),
            ImVec2(pos.x + fill_width, pos.y + 1), highlight, 1.0f);
    }
    
    // Draw time text
    std::string timeText = format_time(currentPosition) + " / " + format_time(mediaState.durationSec);
    float textWidth = ImGui::CalcTextSize(timeText.c_str()).x;
    float textX = pos.x + (width - textWidth) * 0.5f;
    ImGui::SetCursorScreenPos(ImVec2(textX, pos.y + height + GetScaledValue(4)));
    ImGui::TextColored(windowStyle.textColorDim, "%s", timeText.c_str());
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + GetScaledValue(12));
}

// ---------------------------

void RocketRhythm::DrawMusicStateCompact()
{
    if (customFontSegoeUI) ImGui::PushFont(customFontSegoeUI);
    
    // Title with pulse effect when playing
    if (!mediaState.title.empty())
    {
        if (mediaState.isPlaying && windowStyle.enablePulse)
        {
            float pulse = 0.9f + 0.1f * sin(pulseAnimation);
            ImVec4 titleColor = windowStyle.textColor;
            titleColor.w *= pulse;
            DrawScrollableText(mediaState.title, titleColor);
        }
        else
        {
            DrawScrollableText(mediaState.title, windowStyle.textColor);
        }
    }
    
    // Artist
    if (!mediaState.artist.empty())
    {
        DrawScrollableText(mediaState.artist, windowStyle.textColorDim);
    }
    
    // Album info
    if (windowStyle.showAlbumInfo && !mediaState.album.empty())
    {
        DrawScrollableText(mediaState.album, windowStyle.textColorFaint);
    }
    
    ImGui::Spacing();
    
    if (windowStyle.showProgressBar && mediaState.durationSec > 0)
    {
        DrawProgressBar();
    }
    
    if (customFontSegoeUI) ImGui::PopFont();
}

// ---------------------------

void RocketRhythm::DrawNoMusicState()
{
    ImVec2 window_size = ImGui::GetWindowSize();
    ImVec2 center(window_size.x * 0.5f, window_size.y * 0.5f);
    
    const char* main_text = "♫ No Music Playing";
    const char* sub_text = "Play a song to see track info";
    
    float main_width = ImGui::CalcTextSize(main_text).x;
    float sub_width = ImGui::CalcTextSize(sub_text).x;
    
    ImGui::SetCursorPos(ImVec2(center.x - main_width * 0.5f, center.y - 20));
    ImGui::TextColored(windowStyle.textColorFaint, "%s", main_text);
    
    ImGui::SetCursorPos(ImVec2(center.x - sub_width * 0.5f, center.y + 10));
    ImGui::TextColored(windowStyle.textColorFaint, "%s", sub_text);
}

// ---------------------------

void RocketRhythm::RenderSettings()
{
    auto& io = ImGui::GetIO();

    if (customSettingsFontUI) 
        ImGui::PushFont(customSettingsFontUI);
    else if (io.Fonts && !io.Fonts->Fonts.empty()) 
        ImGui::PushFont(io.Fonts->Fonts[0]);
    else 
        ImGui::PushFont(ImGui::GetFont());
    
    const std::string& plugin_name = GetPluginNameCached();
    ImGui::SetCursorPosX((ImGui::GetWindowWidth() - ImGui::CalcTextSize(plugin_name.c_str()).x) * 0.5f);
    ImGui::TextColored(windowStyle.accentColor, "%s", plugin_name.c_str());
    
    ImGui::Separator();
    
    bool enabled_value = *enabled;
    if (ImGui::Checkbox("Enable Plugin", &enabled_value))
    {
        *enabled = enabled_value;
        needs_window_open = false;
        needs_window_close = false;
    }
    ImGui::SameLine();
    DrawHelpMarker("Toggle the entire plugin on/off");
    
    ImGui::Checkbox("Hide When Not Playing", &hide_when_not_playing);
    ImGui::SameLine();
    DrawHelpMarker("Automatically hide overlay when no music is playing");
    
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    
    ImGui::TextColored(windowStyle.accentColor, "Media Status");
    ImGui::Text("Player: %s", media ? "Connected" : "Disconnected");
    ImGui::Text("State: %s", mediaState.isPlaying ? "Playing" : !mediaState.title.empty() ? "Paused" : "No Media");
    if (!mediaState.title.empty())
    {
        ImGui::Text("Track: %s", mediaState.title.c_str());
        if (!mediaState.artist.empty()) ImGui::Text("Artist: %s", mediaState.artist.c_str());
    }
    
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    
    ImGui::TextColored(windowStyle.accentColor, "UI Scaling");
    ImGui::Checkbox("Enable Auto Scaling", &windowStyle.enableAutoScaling);
    DrawHelpMarker("Automatically scale UI based on screen resolution");
    
    if (windowStyle.enableAutoScaling)
    {
        ImGui::SliderFloat("Min Scale", &windowStyle.minScale, 0.5f, 1.5f, "%.1f");
        ImGui::SameLine();
        DrawHelpMarker("Minimum scaling factor for auto-scaling");
        ImGui::SliderFloat("Max Scale", &windowStyle.maxScale, 1.0f, 3.0f, "%.1f");
        ImGui::SameLine();
        DrawHelpMarker("Maximum scaling factor for auto-scaling");
    }
    
    if (ImGui::SliderFloat("UI Scale Multiplier", &windowStyle.uiScale, 0.5f, 2.0f, "%.2f"))
    {
        *uiScaleCVar = windowStyle.uiScale;
    }
    DrawHelpMarker("Manual UI scale multiplier (applies on top of auto-scaling)");
    
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    
    ImGui::TextColored(windowStyle.accentColor, "Appearance");
    ImGui::ColorEdit4("Background", &windowStyle.backgroundColor.x, ImGuiColorEditFlags_NoInputs);
    ImGui::SameLine();
    ImGui::ColorEdit4("Accent", &windowStyle.accentColor.x, ImGuiColorEditFlags_NoInputs);
    ImGui::Checkbox("Show Album Art", &windowStyle.showAlbumArt);
    ImGui::SameLine();
    ImGui::Checkbox("Show Progress Bar", &windowStyle.showProgressBar);
    ImGui::SameLine();
    ImGui::Checkbox("Show Album Info", &windowStyle.showAlbumInfo);
    ImGui::SliderFloat("Album Art Size", &windowStyle.albumArtSize, 80.0f, 150.0f, "%.0f pixels");
    ImGui::SliderFloat("Window Rounding", &windowStyle.windowRounding, 0.0f, 30.0f, "%.0f");
    ImGui::SliderFloat("Opacity", &windowStyle.windowOpacity, 0.5f, 1.0f, "%.2f");
    
    ImGui::Spacing();
    ImGui::Checkbox("Enable Pulse Effect", &windowStyle.enablePulse);
    ImGui::SameLine();
    DrawHelpMarker("Adds a subtle pulse animation to the progress bar and title when music is playing");
    
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    
    if (ImGui::Button("Save Config", ImVec2(120, 30)))
    {
        SaveConfig();
        notify(Info, "{}: Config Saved!", PLUGIN_NAME_STR);
    }
    ImGui::SameLine();
    if (ImGui::Button("Load Config", ImVec2(120, 30)))
    {
        LoadConfig();
        notify(Info, "{}: Config Loaded!", PLUGIN_NAME_STR);
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset to Defaults", ImVec2(120, 30)))
    {
        *enabled = true;
        hide_when_not_playing = true;
        windowStyle = WindowStyle();
        *uiScaleCVar = windowStyle.uiScale;
        notify(Info, "{}: Settings Reset To Default!", PLUGIN_NAME_STR);
    }
    
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    
    ImGui::Text("Developed By: Mister9982");
    ImGui::Text("Version: %s", plugin_version.c_str());
    
    if (ImGui::Button("View On GitHub"))
    {
        ShellExecuteA(nullptr, "open", "https://github.com/99Anvar99/RocketRhythm", nullptr, nullptr, SW_SHOWNORMAL);
    }
    
    ImGui::PopFont();
}

std::string RocketRhythm::GetPluginName()
{
    return PLUGIN_NAME_STR;
}

// ---------------------------

void RocketRhythm::RenderWindow()
{
    if (!*enabled) return;
    
    static auto lastTime = std::chrono::steady_clock::now();
    auto currentTime = std::chrono::steady_clock::now();
    float deltaTime = std::chrono::duration<float>(currentTime - lastTime).count();
    lastTime = currentTime;
    
    UpdateAnimation(deltaTime);
    UpdateTextScroll(deltaTime);
    
    // Calculate dynamic scale factor
    float scaleFactor = GetEffectiveScaleFactor();
    
    // Calculate BASE window size (at scale = 1.0)
    float baseWindowWidth, baseWindowHeight;
    if (windowStyle.showAlbumArt)
    {
        baseWindowWidth = windowStyle.albumArtSize + 15.0f + 235.0f + 15.0f; // art + padding + text + padding
        baseWindowHeight = std::max(windowStyle.albumArtSize + 20.0f, 140.0f);
    }
    else
    {
        baseWindowWidth = 360.0f;
        baseWindowHeight = 130.0f;
    }
    
    // Calculate scaled window size
    float scaledWidth = baseWindowWidth * scaleFactor;
    float scaledHeight = baseWindowHeight * scaleFactor;
    
    // Position window (centered horizontally, near top) - only on first use
    ImVec2 screen_size = ImGui::GetIO().DisplaySize;
    float windowX = (screen_size.x - scaledWidth) * 0.5f;
    float windowY = 25.0f * scaleFactor;
    ImGui::SetNextWindowPos(ImVec2(windowX, windowY), ImGuiCond_FirstUseEver);
    
    // Set minimum window size to prevent making it too small
    float minWidth = baseWindowWidth * 0.5f;
    float minHeight = baseWindowHeight * 0.5f;
    ImGui::SetNextWindowSizeConstraints(
        ImVec2(minWidth * scaleFactor, minHeight * scaleFactor),
        ImVec2(FLT_MAX, FLT_MAX)
    );
    
    // Apply scaled styles
    ImVec4 bgColor = windowStyle.backgroundColor;
    bgColor.w *= windowStyle.windowOpacity;
    ImGui::PushStyleColor(ImGuiCol_WindowBg, bgColor);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, windowStyle.windowRounding * scaleFactor);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f * scaleFactor, 10.0f * scaleFactor));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f * scaleFactor, 2.0f * scaleFactor));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0f * scaleFactor, 2.0f * scaleFactor));
    
    if (customFontSegoeUI) ImGui::PushFont(customFontSegoeUI);
    
    // Allow resizing and moving
    if (ImGui::Begin("##RocketRhythmWindow", nullptr,
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoScrollbar))
    {
        // Get current window size (user might have resized it)
        ImVec2 currentWindowSize = ImGui::GetWindowSize();
        
        // Calculate dynamic scale factor based on current window size
        float dynamicScaleX = currentWindowSize.x / baseWindowWidth;
        float dynamicScaleY = currentWindowSize.y / baseWindowHeight;
        // Use the smaller scale to ensure content fits
        float dynamicScale = std::min(dynamicScaleX, dynamicScaleY);
        // Clamp to reasonable values
        dynamicScale = std::max(0.5f, std::min(dynamicScale, 3.0f));
        
        // Calculate font scale
        float fontScale = dynamicScale;
        // Reduce font scaling slightly on very large windows
        if (dynamicScale > 1.5f)
        {
            fontScale *= 0.95f;
        }
        
        // Apply font scale
        ImGui::SetWindowFontScale(fontScale);
        
        if (mediaState.title.empty() && mediaState.artist.empty())
        {
            DrawNoMusicState();
        }
        else
        {
            if (windowStyle.showAlbumArt)
            {
                ImGui::Columns(2, "music_columns", false);
                // Scale column width based on dynamic scale
                float columnWidth = (windowStyle.albumArtSize + 15.0f) * dynamicScale;
                ImGui::SetColumnWidth(0, columnWidth);
                DrawAlbumArtWithScale(dynamicScale);
                ImGui::NextColumn();
                DrawMusicStateCompact();
                ImGui::Columns(1);
            }
            else
            {
                DrawMusicStateCompact();
            }
        }
    }
    ImGui::End();
    
    if (customFontSegoeUI) ImGui::PopFont();
    ImGui::PopStyleVar(4);
    ImGui::PopStyleColor(1);
    ImGui::render_notifications();
}

// ---------------------------

void RocketRhythm::RenderCanvas(const CanvasWrapper& canvas)
{
    static auto lastTime = std::chrono::steady_clock::now();
    const auto currentTime = std::chrono::steady_clock::now();
    const float deltaTime = std::chrono::duration<float>(currentTime - lastTime).count();
    lastTime = currentTime;

    if (media)
    {
        // Now non-blocking: just copies cached state
        media->Update();
        mediaState = media->GetState();
        is_not_playing = !mediaState.isPlaying && mediaState.title.empty();
    }

    UpdateAnimation(deltaTime);
    UpdateWindowState();

    const std::string& menu_name = GetMenuNameCached();
    if (needs_window_open && !isWindowOpen_)
    {
        cvarManager->executeCommand("openmenu " + menu_name);
        needs_window_open = false;
    }
    else if (needs_window_close && isWindowOpen_)
    {
        cvarManager->executeCommand("closemenu " + menu_name);
        needs_window_close = false;
    }
}

// ---------------------------

void RocketRhythm::DrawHelpMarker(const char* desc)
{
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered())
    {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
        ImGui::TextUnformatted(desc);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

// ---------------------------

bool RocketRhythm::ShouldShowWindow() const
{
    if (!*enabled) return false;
    if (hide_when_not_playing && is_not_playing) return false;
    return true;
}

void RocketRhythm::UpdateWindowState()
{
    const bool show = ShouldShowWindow();
    if (show && !isWindowOpen_ && !needs_window_open)
    {
        needs_window_open = true;
        needs_window_close = false;
    }
    else if (!show && isWindowOpen_ && !needs_window_close)
    {
        needs_window_close = true;
        needs_window_open = false;
    }
}

// ---------------------------
void RocketRhythm::SaveConfig()
{
    if (!enabled || !uiScaleCVar) 
    {
        LOG("SaveConfig skipped: CVars not initialized yet");
        return;
    }

    try
    {
        const auto path = gameWrapper->GetDataFolder() / CONFIG_DIR / CONFIG_FILE_NAME;
        std::filesystem::create_directories(path.parent_path());

        nlohmann::json j = {
            {"version", PLUGIN_CONFIG_VERSION},
            {"enabled", *enabled},
            {"hide_when_not_playing", hide_when_not_playing},
            {"window_style", windowStyle}
        };

        const auto tmp_path = path.string() + ".tmp";

        {
            std::ofstream file(tmp_path, std::ios::binary | std::ios::trunc);
            if (!file)
            {
                LOG("Error saving config: could not open file for writing: {}", tmp_path);
                notify(Error, "Error saving config: could not open file for writing: {}", tmp_path);
                return;
            }

            file << j.dump(4);
            if (!file)
            {
                LOG("Error saving config: write failed: {}", tmp_path);
                notify(Error, "Error saving config: write failed: {}", tmp_path);
                return;
            }
        }

        std::error_code ec;
        std::filesystem::rename(tmp_path, path, ec);
        if (ec)
        {
            std::filesystem::remove(path, ec);
            ec.clear();
            std::filesystem::rename(tmp_path, path, ec);
        }

        if (ec)
        {
            LOG("Error saving config: failed to move temp file into place: {}", ec.message());
            return;
        }

        LOG("Config Saved!");
    }
    catch (const std::exception& e)
    {
        LOG("Error saving config: {}", e.what());
    }
}

void RocketRhythm::LoadConfig()
{
    if (!enabled || !uiScaleCVar)
    {
        LOG("LoadConfig skipped: CVars not initialized yet");
        return;
    }

    try
    {
        // Always start from defaults
        *enabled = true;
        hide_when_not_playing = true;
        windowStyle = DefaultWindowStyle();

        const auto path = gameWrapper->GetDataFolder() / CONFIG_DIR / CONFIG_FILE_NAME;

        // If no config exists, write defaults once
        if (!std::filesystem::exists(path))
        {
            if (uiScaleCVar) *uiScaleCVar = windowStyle.uiScale;
            SaveConfig();
            LOG("Config not found; created default config");
            return;
        }

        std::ifstream file(path, std::ios::binary);
        if (!file)
        {
            LOG("Error loading config: could not open file: {}", path.string());
            return;
        }

        nlohmann::json j;
        file >> j;

        const int version = j.value("version", 0);

        // If version mismatch, reset to defaults and rewrite
        if (version != PLUGIN_CONFIG_VERSION)
        {
            LOG("Config version mismatch (have {}, want {}); resetting to defaults", version, PLUGIN_CONFIG_VERSION);
            if (uiScaleCVar) *uiScaleCVar = windowStyle.uiScale;
            SaveConfig();
            return;
        }

        // Read values (overriding defaults only if present)
        *enabled = j.value("enabled", *enabled);
        hide_when_not_playing = j.value("hide_when_not_playing", hide_when_not_playing);

        if (j.contains("window_style") && j["window_style"].is_object())
        {
            windowStyle = j["window_style"].get<WindowStyle>();
        }

        if (uiScaleCVar) *uiScaleCVar = windowStyle.uiScale;

        LOG("Config Loaded!");
    }
    catch (const std::exception& e)
    {
        LOG("Error loading config: {}", e.what());
        // Optional: if load fails, rewrite defaults so next run is clean
        if (uiScaleCVar) *uiScaleCVar = windowStyle.uiScale;
        SaveConfig();
    }
}

// ---------------------------
const std::string& RocketRhythm::GetMenuNameCached()
{
    if (!menu_name_cached)
    {
        cached_menu_name = GetMenuName();
        menu_name_cached = true;
    }
    return cached_menu_name;
}

const std::string& RocketRhythm::GetPluginNameCached()
{
    if (!plugin_name_cached)
    {
        cached_plugin_name = GetPluginName();
        plugin_name_cached = true;
    }
    return cached_plugin_name;
}