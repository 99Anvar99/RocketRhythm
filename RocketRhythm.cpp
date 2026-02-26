#include "pch.h"
#include "RocketRhythm.h"

#include <urlmon.h>
#pragma comment(lib, "urlmon.lib")

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <shellapi.h>
#include <ShlObj.h>
#include <string>
#include <Windows.h>

#include <nlohmann/json.hpp>
#include "notification.h"

#include "version.h"
#include "bakkesmod/wrappers/GuiManagerWrapper.h"
#include "IMGUI/imgui_internal.h"

std::shared_ptr<CVarManagerWrapper> _globalCvarManager;

static const auto plugin_version =
    std::string(stringify(VERSION_MAJOR)) + "." +
    stringify(VERSION_MINOR) + "." +
    stringify(VERSION_PATCH) + "." +
    stringify(VERSION_BUILD);

static constexpr const char* kConfigFileName = "config.json";
static constexpr const char* kConfigDir      = "RocketRhythm";
static constexpr const char* kPluginNameStr  = "RocketRhythm";

BAKKESMOD_PLUGIN(RocketRhythm, "RocketRhythm", plugin_version.c_str(), PLUGINTYPE_THREADED)

// ------------------------------------------------------------
// Helpers
// ------------------------------------------------------------

static std::wstring ToWide(const std::string& s)
{
    if (s.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring out(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), out.data(), len);
    return out;
}

static bool DownloadToFile(const std::wstring& url, const std::filesystem::path& outPath, std::string& err)
{
    std::error_code ec;
    std::filesystem::create_directories(outPath.parent_path(), ec);
    if (ec)
    {
        err = "create_directories failed: " + ec.message();
        return false;
    }

    HRESULT hr = URLDownloadToFileW(nullptr, url.c_str(), outPath.wstring().c_str(), 0, nullptr);
    if (FAILED(hr))
    {
        char buf[64];
        snprintf(buf, sizeof(buf), "HRESULT=0x%08lX", static_cast<unsigned long>(hr));
        err = buf;
        return false;
    }
    return true;
}

static std::string FormatTimeSeconds(int seconds)
{
    if (seconds <= 0) return "0:00";
    const int minutes = seconds / 60;
    const int secs = seconds % 60;

    char buf[16]{};
    snprintf(buf, sizeof(buf), "%d:%02d", minutes, secs);
    return std::string(buf);
}

static bool IsValidImageFile(const std::string& path)
{
    try
    {
        std::ifstream f(path, std::ios::binary);
        if (!f.is_open()) return false;
        f.seekg(0, std::ios::end);
        return f.tellg() > 100;
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
        if (it->is_boolean()) target = it->get<bool>();
    }
    else
    {
        if (it->is_number())
        {
            const double v = it->get<double>();
            target = static_cast<T>(v);
        }
    }
}

static void DrawPingPongMarqueeText(
    const char* text,
    const ImVec4& color,
    float availableWidth,
    float speedPxPerSec,
    float waitTimeSec,
    ImFont* font = nullptr
)
{
    if (!text || !*text)
    {
        ImGui::Dummy(ImVec2(availableWidth, ImGui::GetTextLineHeight()));
        return;
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float lineH = ImGui::GetTextLineHeight();

    // Reserve space for one line
    ImGui::Dummy(ImVec2(availableWidth, lineH));

    const float textW = ImGui::CalcTextSize(text).x;
    const float overflow = textW - availableWidth;

    dl->PushClipRect(pos, ImVec2(pos.x + availableWidth, pos.y + lineH), true);

    if (font) ImGui::PushFont(font);
    const ImU32 col = ImGui::GetColorU32(color);

    if (overflow <= 0.0f || speedPxPerSec <= 0.0f)
    {
        dl->AddText(pos, col, text);
    }
    else
    {
        const float t = static_cast<float>(ImGui::GetTime());
        const float moveTime = overflow / speedPxPerSec;

        // cycle = wait -> move -> wait -> move back
        const float cycle = waitTimeSec + moveTime + waitTimeSec + moveTime;
        const float phase = fmodf(t, cycle);

        float offset;
        if (phase < waitTimeSec)
            offset = 0.0f;
        else if (phase < waitTimeSec + moveTime)
            offset = (phase - waitTimeSec) * speedPxPerSec;
        else if (phase < waitTimeSec + moveTime + waitTimeSec)
            offset = overflow;
        else
            offset = overflow - (phase - (waitTimeSec + moveTime + waitTimeSec)) * speedPxPerSec;

        dl->AddText(ImVec2(pos.x - offset, pos.y), col, text);
    }

    if (font) ImGui::PopFont();
    dl->PopClipRect();
}

// ------------------------------------------------------------
// JSON (WindowStyle)
// ------------------------------------------------------------

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

        {"enable_pulse",     s.enablePulse},
        {"show_album_art",   s.showAlbumArt},
        {"show_progress_bar",s.showProgressBar},
        {"show_album_info",  s.showAlbumInfo},
        {"window_opacity",   s.windowOpacity},

        {"ui_scale",            s.uiScale},
        {"enable_auto_scaling", s.enableAutoScaling},
        {"min_scale",           s.minScale},
        {"max_scale",           s.maxScale},

        {"enable_marquee",   s.enableMarquee},
        {"marquee_speed_px", s.marqueeSpeedPx},
        {"marquee_wait_sec", s.marqueeWaitSec},
        {"time_display_mode", static_cast<int>(s.timeDisplayMode)},

    };
}

void from_json(const nlohmann::json& j, RocketRhythm::WindowStyle& s)
{
    const RocketRhythm::WindowStyle def = RocketRhythm::DefaultWindowStyle();
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

    int tdm = static_cast<int>(def.timeDisplayMode);
    tdm = std::clamp(tdm, 0, 1);

    AssignIfNumberOrBool(j, "window_rounding",       s.windowRounding);
    AssignIfNumberOrBool(j, "album_art_rounding",    s.albumArtRounding);
    AssignIfNumberOrBool(j, "progress_bar_height",   s.progressBarHeight);
    AssignIfNumberOrBool(j, "progress_bar_rounding", s.progressBarRounding);
    AssignIfNumberOrBool(j, "album_art_size",        s.albumArtSize);

    AssignIfNumberOrBool(j, "enable_pulse",          s.enablePulse);
    AssignIfNumberOrBool(j, "show_album_art",        s.showAlbumArt);
    AssignIfNumberOrBool(j, "show_progress_bar",     s.showProgressBar);
    AssignIfNumberOrBool(j, "show_album_info",       s.showAlbumInfo);
    AssignIfNumberOrBool(j, "window_opacity",        s.windowOpacity);

    AssignIfNumberOrBool(j, "ui_scale",              s.uiScale);
    AssignIfNumberOrBool(j, "enable_auto_scaling",   s.enableAutoScaling);
    AssignIfNumberOrBool(j, "min_scale",             s.minScale);
    AssignIfNumberOrBool(j, "max_scale",             s.maxScale);

    AssignIfNumberOrBool(j, "enable_marquee",   s.enableMarquee);
    AssignIfNumberOrBool(j, "marquee_speed_px", s.marqueeSpeedPx);
    AssignIfNumberOrBool(j, "marquee_wait_sec", s.marqueeWaitSec);
    AssignIfNumberOrBool(j, "time_display_mode", tdm);

    auto clampf = [](float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; };

    s.windowOpacity = clampf(s.windowOpacity, 0.0f, 1.0f);

    s.uiScale   = clampf(s.uiScale, 0.5f, 2.0f);
    s.minScale  = clampf(s.minScale, 0.1f, 10.0f);
    s.maxScale  = clampf(s.maxScale, 0.1f, 10.0f);
    if (s.minScale > s.maxScale) std::swap(s.minScale, s.maxScale);

    s.windowRounding      = clampf(s.windowRounding, 0.0f, 50.0f);
    s.albumArtRounding    = clampf(s.albumArtRounding, 0.0f, 50.0f);
    s.progressBarHeight   = clampf(s.progressBarHeight, 0.0f, 50.0f);
    s.progressBarRounding = clampf(s.progressBarRounding, 0.0f, 50.0f);
    s.albumArtSize        = clampf(s.albumArtSize, 16.0f, 512.0f);

    s.marqueeSpeedPx = clampf(s.marqueeSpeedPx, 0.0f, 1000.0f);
    s.marqueeWaitSec = clampf(s.marqueeWaitSec, 0.0f, 10.0f);
    s.timeDisplayMode = static_cast<RocketRhythm::WindowStyle::TimeDisplayMode>(tdm);
}

// ------------------------------------------------------------
// RocketRhythm
// ------------------------------------------------------------

RocketRhythm::RocketRhythm()
    : mLastProgressAnchor(std::chrono::steady_clock::now())
{
}

RocketRhythm::~RocketRhythm() = default;

void RocketRhythm::onLoad()
{
    _globalCvarManager = cvarManager;

    mMedia = CreateMediaController(gameWrapper->GetDataFolder().string());

    mEnabled = std::make_shared<bool>(true);
    mUiScaleCvar = std::make_shared<float>(1.0f);

    cvarManager->registerCvar("rr_enabled", "1", "Enable RocketRhythm").bindTo(mEnabled);
    cvarManager->registerCvar("rr_uiscale", "1.0", "UI Scale factor", true, true, 0.5f, true, 2.0f).bindTo(mUiScaleCvar);

    LoadConfig();

    gameWrapper->RegisterDrawable([this](const CanvasWrapper& canvas) { RenderCanvas(canvas); });

    LOG("{} v{} loaded!", kPluginNameStr, plugin_version);
}

void RocketRhythm::onUnload()
{
    SaveConfig();

    mAlbumArtTexture.reset();
    cvarManager->removeCvar("rr_enabled");
    cvarManager->removeCvar("rr_uiscale");

    LOG("{} unloaded!", kPluginNameStr);
}

// ------------------------------------------------------------
// Cached names
// ------------------------------------------------------------

const std::string& RocketRhythm::GetMenuNameCached()
{
    if (!mMenuNameCached)
    {
        mCachedMenuName = GetMenuName();
        mMenuNameCached = true;
    }
    return mCachedMenuName;
}

const std::string& RocketRhythm::GetPluginNameCached()
{
    if (!mPluginNameCached)
    {
        mCachedPluginName = GetPluginName();
        mPluginNameCached = true;
    }
    return mCachedPluginName;
}

std::string RocketRhythm::GetPluginName()
{
    return kPluginNameStr;
}

// ------------------------------------------------------------
// Fonts
// ------------------------------------------------------------
void RocketRhythm::InitializeFonts()
{
    if (mFontOverlay && mFontSettings)
    {
        mFontsInitialized = true;
        return;
    }

    auto gui = gameWrapper->GetGUIManager();

    const std::filesystem::path bmData    = gameWrapper->GetDataFolder();
    const std::filesystem::path fontDir   = bmData / "fonts" / "RocketRhythm";
    const std::filesystem::path fontFile  = fontDir / "segoeui.ttf";

    static const std::wstring kFontUrl = L"https://raw.githubusercontent.com/99Anvar99/RocketRhythm/main/fonts/segoeui.ttf";

    const std::string fontRel = "RocketRhythm/segoeui.ttf";

    // Ensure directory exists
    std::error_code ec;
    std::filesystem::create_directories(fontDir, ec);
    if (ec)
    {
        LOG("Failed to create fonts dir: {} ({})", fontDir.string(), ec.message());
    }

    static std::atomic_bool sDownloadInFlight{ false };

    const bool haveFontFile = std::filesystem::exists(fontFile);

    if (!haveFontFile && !sDownloadInFlight.exchange(true))
    {
        // capture only what we need by value
        const std::wstring urlCopy = kFontUrl;
        const std::filesystem::path dstFinal = fontFile;
        const std::filesystem::path dstTemp  = fontDir / "segoeui.tmp";

        std::thread([urlCopy, dstFinal, dstTemp]()
        {
            HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

            // Download to temp first to avoid partial reads
            std::string err;
            bool ok = DownloadToFile(urlCopy, dstTemp, err);

            if (!ok)
            {
                LOG("Font download failed: {}", err);
                std::error_code ec2;
                std::filesystem::remove(dstTemp, ec2);
            }
            else
            {
                // Atomically replace/move temp -> final
                std::error_code ec3;
                std::filesystem::rename(dstTemp, dstFinal, ec3);
                if (ec3)
                {
                    // If rename fails (e.g., file exists), try over-write strategy
                    std::error_code ec4;
                    std::filesystem::remove(dstFinal, ec4);
                    ec3.clear();
                    std::filesystem::rename(dstTemp, dstFinal, ec3);
                }

                if (ec3)
                    LOG("Font move into place failed: {}", ec3.message());
                else
                    LOG("Font downloaded: {}", dstFinal.string());
            }

            if (SUCCEEDED(hr))
                CoUninitialize();

            sDownloadInFlight.store(false);
        }).detach();
    }

    // Try to load whenever the file exists (this will naturally succeed on later frames)
    static constexpr auto kOverlayKey  = "rr_overlay_24";
    static constexpr auto kSettingsKey = "rr_settings_16";

    if (std::filesystem::exists(fontFile))
    {
        // Build ranges once
        static ImVector<ImWchar> sGlyphRanges;
        static std::atomic_bool sRangesBuilt{ false };

        if (!sRangesBuilt.exchange(true))
        {
            ImGuiIO& io = ImGui::GetIO();

            ImFontGlyphRangesBuilder builder;
            builder.AddRanges(io.Fonts->GetGlyphRangesDefault());
            builder.AddRanges(io.Fonts->GetGlyphRangesCyrillic());
            builder.AddRanges(io.Fonts->GetGlyphRangesJapanese());
            builder.AddRanges(io.Fonts->GetGlyphRangesChineseSimplifiedCommon());
            builder.AddRanges(io.Fonts->GetGlyphRangesVietnamese());
            builder.AddRanges(io.Fonts->GetGlyphRangesKorean());
            builder.BuildRanges(&sGlyphRanges);
        }

        if (!mFontOverlay)
        {
            auto [res, font] = gui.LoadFont(kOverlayKey, fontRel, 24, nullptr, sGlyphRanges.Data);
            if ((res == 0 || res == 2) && font) mFontOverlay = font;
            if (!mFontOverlay) mFontOverlay = gui.GetFont(kOverlayKey);
        }

        if (!mFontSettings)
        {
            auto [res, font] = gui.LoadFont(kSettingsKey, fontRel, 16, nullptr, sGlyphRanges.Data);
            if ((res == 0 || res == 2) && font) mFontSettings = font;
            if (!mFontSettings) mFontSettings = gui.GetFont(kSettingsKey);
        }
    }

    mFontsInitialized = mFontOverlay != nullptr && mFontSettings != nullptr;
}

// ------------------------------------------------------------
// Album art
// ------------------------------------------------------------

void RocketRhythm::LoadAlbumArt(const std::string& path)
{
    if (path.empty() || !IsValidImageFile(path))
    {
        mAlbumArtLoaded = false;
        mAlbumArtTexture.reset();
        mAlbumArtPath.clear();
        return;
    }

    if (mAlbumArtLoaded && mAlbumArtTexture && mAlbumArtPath == path)
        return;

    try
    {
        mAlbumArtTexture.reset();
        mAlbumArtTexture = std::make_shared<ImageWrapper>(path, false, true);

        if (mAlbumArtTexture)
        {
            mAlbumArtLoaded = true;
            mAlbumArtPath = path;
        }
        else
        {
            mAlbumArtLoaded = false;
            mAlbumArtPath.clear();
        }
    }
    catch (const std::exception& e)
    {
        mAlbumArtLoaded = false;
        mAlbumArtTexture.reset();
        mAlbumArtPath.clear();
        LOG("Album art load error: {}", e.what());
    }
}

// ------------------------------------------------------------
// Playback position smoothing
// ------------------------------------------------------------

int RocketRhythm::GetCurrentDisplayPositionSec()
{
    const auto now = std::chrono::steady_clock::now();

    const std::string trackKey = mMediaState.title + "|" + mMediaState.artist + "|" + mMediaState.album;

    // Re-anchor when track changes or position updates externally
    if (trackKey != mLastTrackKey || mMediaState.positionSec != mLastPositionSec)
    {
        mLastTrackKey = trackKey;
        mLastPositionSec = mMediaState.positionSec;
        mAnchoredPositionSec = mMediaState.positionSec;
        mLastProgressAnchor = now;
    }

    if (!mMediaState.isPlaying || mMediaState.durationSec <= 0)
    {
        return std::clamp(mMediaState.positionSec, 0, (mMediaState.durationSec > 0 ? mMediaState.durationSec : 0));
    }

    const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - mLastProgressAnchor).count();
    const int predicted = mAnchoredPositionSec + static_cast<int>(elapsedMs / 1000);

    return std::clamp(predicted, 0, mMediaState.durationSec);
}

// ------------------------------------------------------------
// Scaling
// ------------------------------------------------------------

float RocketRhythm::GetDpiScaleFactor()
{
    HDC screen = GetDC(nullptr);
    const float dpiScaleX = static_cast<float>(GetDeviceCaps(screen, LOGPIXELSX)) / 96.0f;
    ReleaseDC(nullptr, screen);
    return dpiScaleX;
}

float RocketRhythm::CalculateAutoScaleFactor()
{
    const ImVec2 displaySize = ImGui::GetIO().DisplaySize;

    constexpr float baseWidth = 1920.0f;
    constexpr float baseHeight = 1080.0f;

    const float heightRatio = displaySize.y / baseHeight;
    const float widthRatio = displaySize.x / baseWidth;

    const float resolutionScale = std::min(heightRatio, widthRatio);
    const float dpiScale = GetDpiScaleFactor();

    float autoScale = resolutionScale * dpiScale;
    autoScale = std::clamp(autoScale, mWindowStyle.minScale, mWindowStyle.maxScale);
    return autoScale;
}

float RocketRhythm::GetEffectiveScaleFactor()
{
    if (mUiScaleCvar)
        mWindowStyle.uiScale = *mUiScaleCvar;

    float scale = mWindowStyle.enableAutoScaling ? CalculateAutoScaleFactor() : 1.0f;
    scale *= mWindowStyle.uiScale;

    return std::clamp(scale, 0.5f, 3.0f);
}

float RocketRhythm::GetScaledValue(float baseValue)
{
    return baseValue * GetEffectiveScaleFactor();
}

// ------------------------------------------------------------
// Animation
// ------------------------------------------------------------

void RocketRhythm::UpdateAnimation(float deltaTime)
{
    if (mMediaState.isPlaying && mWindowStyle.enablePulse)
    {
        mPulsePhase += deltaTime * 2.0f;
        if (mPulsePhase > 6.2831853f) mPulsePhase -= 6.2831853f;
    }
}

// ------------------------------------------------------------
// Drawing (album art)
// ------------------------------------------------------------

void RocketRhythm::DrawAlbumArtPlaceholder(float scale)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float size = mWindowStyle.albumArtSize * scale;

    const ImU32 top = ImGui::GetColorU32(ImVec4(0.15f, 0.15f, 0.18f, 1.0f));
    const ImU32 bot = ImGui::GetColorU32(ImVec4(0.12f, 0.12f, 0.15f, 1.0f));
    dl->AddRectFilledMultiColor(pos, ImVec2(pos.x + size, pos.y + size), top, top, bot, bot);

    const float cx = pos.x + size * 0.5f;
    const float cy = pos.y + size * 0.5f;

    for (int i = 0; i < 3; ++i)
    {
        const float radius = size * 0.25f + i * (8.0f * scale);
        const ImU32 ring = ImGui::GetColorU32(ImVec4(0.3f, 0.3f, 0.35f, 0.2f));
        dl->AddCircle(ImVec2(cx, cy), radius, ring, 0, 1.5f);
    }

    const ImU32 note = ImGui::GetColorU32(mWindowStyle.accentColor);
    const float fontSize = ImGui::GetFontSize() * 2.0f;
    const float off = 15.0f * scale;
    dl->AddText(ImGui::GetFont(), fontSize, ImVec2(cx - off, cy - off), note, "♪");

    const ImU32 border = ImGui::GetColorU32(mWindowStyle.accentColor);
    dl->AddRect(pos, ImVec2(pos.x + size, pos.y + size), border, mWindowStyle.albumArtRounding * scale, 0, 2.0f);

    // Advance cursor to the right (column layout)
    ImGui::SetCursorPos(ImGui::GetCursorPos() + ImVec2(size + 15.0f * scale, 0));
}

void RocketRhythm::DrawAlbumArt(float scale)
{
    if (mMediaState.hasAlbumArt && !mMediaState.albumArtPath.empty())
        LoadAlbumArt(mMediaState.albumArtPath);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float size = mWindowStyle.albumArtSize * scale;

    if (mAlbumArtLoaded && mAlbumArtTexture && mAlbumArtTexture->IsLoadedForImGui())
    {
        if (ImTextureID tex = mAlbumArtTexture->GetImGuiTex())
        {
            ImGui::Image(tex, ImVec2(size, size));
            const ImU32 border = ImGui::GetColorU32(ImVec4(1, 1, 1, 0.10f));
            dl->AddRect(pos, ImVec2(pos.x + size, pos.y + size), border, mWindowStyle.albumArtRounding * scale, 0, 1.0f);

            ImGui::SetCursorPos(ImGui::GetCursorPos() + ImVec2(size + 15.0f * scale, 0));
            return;
        }
    }

    DrawAlbumArtPlaceholder(scale);
}

// ------------------------------------------------------------
// Drawing (progress bar)
// ------------------------------------------------------------

void RocketRhythm::DrawProgressBar()
{
    if (!mWindowStyle.showProgressBar || mMediaState.durationSec <= 0) return;

    const int currentPos = GetCurrentDisplayPositionSec();
    float progress = static_cast<float>(currentPos) / static_cast<float>(mMediaState.durationSec);
    progress = std::clamp(progress, 0.0f, 1.0f);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float width  = ImGui::GetContentRegionAvail().x;
    const float height = GetScaledValue(mWindowStyle.progressBarHeight);

    const ImU32 bgCol = ImGui::GetColorU32(ImVec4(0.15f, 0.15f, 0.20f, 0.8f));

    float bgRounding = GetScaledValue(mWindowStyle.progressBarRounding);
    bgRounding = std::min(bgRounding, height * 0.5f);

    // Background
    dl->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + height), bgCol, bgRounding);

    // Fill
    if (progress > 0.0f)
    {
        const float fillWidth = width * progress;

        ImVec4 fillColor = mWindowStyle.accentColor;
        if (mMediaState.isPlaying && mWindowStyle.enablePulse)
        {
            const float pulse = 0.8f + 0.2f * sinf(mPulsePhase);
            fillColor.x *= pulse;
            fillColor.y *= pulse;
            fillColor.z *= pulse;
        }

        float fillRounding = GetScaledValue(mWindowStyle.progressBarRounding);
        fillRounding = std::min(fillRounding, height * 0.5f);
        fillRounding = std::min(fillRounding, fillWidth * 0.5f);

        const ImU32 fillCol = ImGui::GetColorU32(fillColor);
        dl->AddRectFilled(pos, ImVec2(pos.x + fillWidth, pos.y + height), fillCol, fillRounding);

        const ImU32 highlight = ImGui::GetColorU32(ImVec4(1, 1, 1, 0.20f));
        dl->AddLine(ImVec2(pos.x, pos.y + 1), ImVec2(pos.x + fillWidth, pos.y + 1), highlight, 1.0f);
    }

    // Time labels
    const float yText = pos.y + height + GetScaledValue(4.0f);
    const ImU32 textCol = ImGui::GetColorU32(mWindowStyle.textColorDim);

    if (mWindowStyle.timeDisplayMode == WindowStyle::TimeDisplayMode::Corners)
    {
        const std::string leftTime  = FormatTimeSeconds(currentPos);
        const std::string rightTime = FormatTimeSeconds(mMediaState.durationSec);

        dl->AddText(ImVec2(pos.x, yText), textCol, leftTime.c_str());

        const float rightW = ImGui::CalcTextSize(rightTime.c_str()).x;
        dl->AddText(ImVec2(pos.x + width - rightW, yText), textCol, rightTime.c_str());
    }
    else // CenterSlash
    {
        const std::string timeText =
            FormatTimeSeconds(currentPos) + " / " + FormatTimeSeconds(mMediaState.durationSec);

        const float textW = ImGui::CalcTextSize(timeText.c_str()).x;
        const float x = pos.x + (width - textW) * 0.5f;

        dl->AddText(ImVec2(x, yText), textCol, timeText.c_str());
    }

    // Reserve space (text line + spacing)
    ImGui::Dummy(ImVec2(width, ImGui::GetTextLineHeight() + GetScaledValue(12.0f)));
}

// ------------------------------------------------------------
// Drawing (music state)
// ------------------------------------------------------------

void RocketRhythm::DrawMusicStateCompact()
{
    if (mFontOverlay) ImGui::PushFont(mFontOverlay);

    const float speed = mWindowStyle.marqueeSpeedPx * GetEffectiveScaleFactor();
    const float wait  = mWindowStyle.marqueeWaitSec;

    // Title
    if (!mMediaState.title.empty())
    {
        ImVec4 c = mWindowStyle.textColor;
        if (mMediaState.isPlaying && mWindowStyle.enablePulse)
        {
            const float pulse = 0.9f + 0.1f * sinf(mPulsePhase);
            c.w *= pulse;
        }

        if (mWindowStyle.enableMarquee)
            DrawPingPongMarqueeText(mMediaState.title.c_str(), c, ImGui::GetContentRegionAvail().x, speed, wait, mFontOverlay);
        else
            ImGui::TextColored(c, "%s", mMediaState.title.c_str());
    }

    // Artist
    if (!mMediaState.artist.empty())
    {
        if (mWindowStyle.enableMarquee)
            DrawPingPongMarqueeText(mMediaState.artist.c_str(), mWindowStyle.textColorDim, ImGui::GetContentRegionAvail().x, speed, wait, mFontOverlay);
        else
            ImGui::TextColored(mWindowStyle.textColorDim, "%s", mMediaState.artist.c_str());
    }

    // Album
    if (mWindowStyle.showAlbumInfo && !mMediaState.album.empty())
    {
        if (mWindowStyle.enableMarquee)
            DrawPingPongMarqueeText(mMediaState.album.c_str(), mWindowStyle.textColorFaint, ImGui::GetContentRegionAvail().x, speed, wait, mFontOverlay);
        else
            ImGui::TextColored(mWindowStyle.textColorFaint, "%s", mMediaState.album.c_str());
    }

    ImGui::Spacing();

    if (mWindowStyle.showProgressBar && mMediaState.durationSec > 0)
        DrawProgressBar();

    if (mFontOverlay) ImGui::PopFont();
}

void RocketRhythm::DrawNoMusicState()
{
    const ImVec2 ws = ImGui::GetWindowSize();
    const ImVec2 center(ws.x * 0.5f, ws.y * 0.5f);

    auto mainText = "No Music Playing";
    auto subText  = "Play a song to see track info";

    const float mainW = ImGui::CalcTextSize(mainText).x;
    const float subW  = ImGui::CalcTextSize(subText).x;

    ImGui::SetCursorPos(ImVec2(center.x - mainW * 0.5f, center.y - 20));
    ImGui::TextColored(mWindowStyle.textColorFaint, "%s", mainText);

    ImGui::SetCursorPos(ImVec2(center.x - subW * 0.5f, center.y + 10));
    ImGui::TextColored(mWindowStyle.textColorFaint, "%s", subText);
}

// ------------------------------------------------------------
// Settings UI
// ------------------------------------------------------------

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

void RocketRhythm::RenderSettings()
{
    if (!mFontsInitialized)
        InitializeFonts();

    const std::string& pluginName = GetPluginNameCached();
    ImGui::SetCursorPosX((ImGui::GetWindowWidth() - ImGui::CalcTextSize(pluginName.c_str()).x) * 0.5f);
    ImGui::TextColored(mWindowStyle.accentColor, "%s", pluginName.c_str());

    ImGui::Separator();

    // Enable
    bool enabledValue = mEnabled ? *mEnabled : true;
    if (ImGui::Checkbox("Enable Plugin", &enabledValue))
    {
        if (mEnabled) *mEnabled = enabledValue;
        mNeedsWindowOpen = false;
        mNeedsWindowClose = false;
    }
    ImGui::SameLine();
    DrawHelpMarker("Toggle the entire plugin on/off");

    ImGui::Checkbox("Hide When Not Playing", &mHideWhenNotPlaying);
    ImGui::SameLine();
    DrawHelpMarker("Automatically hide overlay when no music is playing");

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::TextColored(mWindowStyle.accentColor, "Media Status");
    ImGui::Text("Player: %s", mMedia ? "Connected" : "Disconnected");
    ImGui::Text("State: %s", mMediaState.isPlaying ? "Playing" : (!mMediaState.title.empty() ? "Paused" : "No Media"));
    if (!mMediaState.title.empty())
    {
        ImGui::Text("Track: %s", mMediaState.title.c_str());
        if (!mMediaState.artist.empty()) ImGui::Text("Artist: %s", mMediaState.artist.c_str());
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::TextColored(mWindowStyle.accentColor, "UI Scaling");

    ImGui::Checkbox("Enable Auto Scaling", &mWindowStyle.enableAutoScaling);
    ImGui::SameLine();
    DrawHelpMarker("Automatically scale UI based on screen resolution and DPI");

    if (mWindowStyle.enableAutoScaling)
    {
        ImGui::SliderFloat("Min Scale", &mWindowStyle.minScale, 0.5f, 1.5f, "%.2f");
        ImGui::SameLine();
        DrawHelpMarker("Minimum scaling factor for auto-scaling");

        ImGui::SliderFloat("Max Scale", &mWindowStyle.maxScale, 1.0f, 3.0f, "%.2f");
        ImGui::SameLine();
        DrawHelpMarker("Maximum scaling factor for auto-scaling");
    }

    if (ImGui::SliderFloat("UI Scale Multiplier", &mWindowStyle.uiScale, 0.5f, 2.0f, "%.2f"))
    {
        if (mUiScaleCvar) *mUiScaleCvar = mWindowStyle.uiScale;
    }
    ImGui::SameLine();
    DrawHelpMarker("Manual UI scale multiplier (applies on top of auto-scaling)");

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::TextColored(mWindowStyle.accentColor, "Appearance");

    ImGui::ColorEdit4("Background", &mWindowStyle.backgroundColor.x, ImGuiColorEditFlags_NoInputs);
    ImGui::SameLine();
    ImGui::ColorEdit4("Accent", &mWindowStyle.accentColor.x, ImGuiColorEditFlags_NoInputs);

    ImGui::Checkbox("Show Album Art", &mWindowStyle.showAlbumArt);
    ImGui::SameLine();
    ImGui::Checkbox("Show Progress Bar", &mWindowStyle.showProgressBar);
    ImGui::SameLine();
    ImGui::Checkbox("Show Album Info", &mWindowStyle.showAlbumInfo);

    ImGui::SliderFloat("Album Art Size", &mWindowStyle.albumArtSize, 80.0f, 150.0f, "%.0f px");
    ImGui::SliderFloat("Window Rounding", &mWindowStyle.windowRounding, 0.0f, 30.0f, "%.0f");
    ImGui::SliderFloat("Opacity", &mWindowStyle.windowOpacity, 0.5f, 1.0f, "%.2f");

    ImGui::Spacing();
    ImGui::Checkbox("Enable Pulse Effect", &mWindowStyle.enablePulse);
    ImGui::SameLine();
    DrawHelpMarker("Adds a subtle pulse animation to the progress bar and title when music is playing");

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::TextColored(mWindowStyle.accentColor, "Text / Marquee");

    ImGui::Checkbox("Enable Marquee Scrolling", &mWindowStyle.enableMarquee);
    ImGui::SameLine();
    DrawHelpMarker("When enabled, long title/artist/album text scrolls left-right with pauses.");

    ImGui::SliderFloat("Marquee Speed", &mWindowStyle.marqueeSpeedPx, 10.0f, 200.0f, "%.0f px/sec");
    ImGui::SameLine();
    DrawHelpMarker("Scroll speed for overflowing text (before scaling).");

    ImGui::SliderFloat("Marquee Wait", &mWindowStyle.marqueeWaitSec, 0.0f, 2.0f, "%.2f sec");
    ImGui::SameLine();
    DrawHelpMarker("Pause duration at each end before reversing.");

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::TextColored(mWindowStyle.accentColor, "Time Display");

    int mode = static_cast<int>(mWindowStyle.timeDisplayMode);
    const char* items[] = {"Centered (0:10 / 3:45)", "Corners (0:10 ... 3:45)"};

    if (ImGui::Combo("Time Display", &mode, items, IM_ARRAYSIZE(items)))
    {
        mode = std::clamp(mode, 0, 1);
        mWindowStyle.timeDisplayMode = static_cast<WindowStyle::TimeDisplayMode>(mode);
    }

    ImGui::SameLine();
    DrawHelpMarker("Choose whether time is centered as \"current / total\" or shown at the left/right edges.");

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (ImGui::Button("Save Config", ImVec2(120, 30)))
    {
        SaveConfig();
        notify(Info, "{}: Config Saved!", kPluginNameStr);
    }
    ImGui::SameLine();
    if (ImGui::Button("Load Config", ImVec2(120, 30)))
    {
        LoadConfig();
        notify(Info, "{}: Config Loaded!", kPluginNameStr);
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset to Defaults", ImVec2(140, 30)))
    {
        if (mEnabled) *mEnabled = true;
        mHideWhenNotPlaying = true;
        mWindowStyle = DefaultWindowStyle();
        if (mUiScaleCvar) *mUiScaleCvar = mWindowStyle.uiScale;
        notify(Info, "{}: Settings Reset To Default!", kPluginNameStr);
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
}

// ------------------------------------------------------------
// Window show/hide logic
// ------------------------------------------------------------

bool RocketRhythm::ShouldShowWindow() const
{
    if (!mEnabled || !*mEnabled) return false;
    if (mHideWhenNotPlaying && mIsNotPlaying) return false;
    return true;
}

void RocketRhythm::UpdateWindowState()
{
    const bool show = ShouldShowWindow();
    if (show && !isWindowOpen_ && !mNeedsWindowOpen)
    {
        mNeedsWindowOpen = true;
        mNeedsWindowClose = false;
    }
    else if (!show && isWindowOpen_ && !mNeedsWindowClose)
    {
        mNeedsWindowClose = true;
        mNeedsWindowOpen = false;
    }
}

// ------------------------------------------------------------
// RenderWindow (overlay window)
// ------------------------------------------------------------

void RocketRhythm::RenderWindow()
{
    if (!mEnabled || !*mEnabled) return;

    if (!mFontsInitialized)
        InitializeFonts();

    static auto lastTime = std::chrono::steady_clock::now();
    const auto now = std::chrono::steady_clock::now();
    const float dt = std::chrono::duration<float>(now - lastTime).count();
    lastTime = now;

    UpdateAnimation(dt);

    const float scaleFactor = GetEffectiveScaleFactor();

    // Base size at scale=1 (approx)
    float baseWidth = 360.0f;
    float baseHeight = 130.0f;
    if (mWindowStyle.showAlbumArt)
    {
        baseWidth  = mWindowStyle.albumArtSize + 15.0f + 235.0f + 15.0f;
        baseHeight = std::max(mWindowStyle.albumArtSize + 20.0f, 140.0f);
    }

    const float scaledW = baseWidth * scaleFactor;

    const ImVec2 screenSize = ImGui::GetIO().DisplaySize;
    const float winX = (screenSize.x - scaledW) * 0.5f;
    const float winY = 25.0f * scaleFactor;

    ImGui::SetNextWindowPos(ImVec2(winX, winY), ImGuiCond_FirstUseEver);

    const float minW = baseWidth * 0.5f;
    const float minH = baseHeight * 0.5f;
    ImGui::SetNextWindowSizeConstraints(
        ImVec2(minW * scaleFactor, minH * scaleFactor),
        ImVec2(FLT_MAX, FLT_MAX)
    );

    ImVec4 bg = mWindowStyle.backgroundColor;
    bg.w *= mWindowStyle.windowOpacity;

    ImGui::PushStyleColor(ImGuiCol_WindowBg, bg);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, mWindowStyle.windowRounding * scaleFactor);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f * scaleFactor, 10.0f * scaleFactor));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f * scaleFactor, 2.0f * scaleFactor));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0f * scaleFactor, 2.0f * scaleFactor));

    if (mFontOverlay) ImGui::PushFont(mFontOverlay);

    if (ImGui::Begin("##RocketRhythmWindow", nullptr,
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoScrollbar))
    {
        const ImVec2 currentWindowSize = ImGui::GetWindowSize();
        const float dynamicScaleX = currentWindowSize.x / baseWidth;
        const float dynamicScaleY = currentWindowSize.y / baseHeight;
        float dynamicScale = std::min(dynamicScaleX, dynamicScaleY);
        dynamicScale = std::clamp(dynamicScale, 0.5f, 3.0f);

        float fontScale = dynamicScale;
        if (dynamicScale > 1.5f) fontScale *= 0.95f;
        ImGui::SetWindowFontScale(fontScale);

        if (mMediaState.title.empty() && mMediaState.artist.empty())
        {
            DrawNoMusicState();
        }
        else
        {
            if (mWindowStyle.showAlbumArt)
            {
                ImGui::Columns(2, "music_columns", false);
                const float colW = (mWindowStyle.albumArtSize + 15.0f) * dynamicScale;
                ImGui::SetColumnWidth(0, colW);

                DrawAlbumArt(dynamicScale);

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

    if (mFontOverlay) ImGui::PopFont();
    ImGui::PopStyleVar(4);
    ImGui::PopStyleColor(1);

    ImGui::render_notifications();
}

// ------------------------------------------------------------
// RenderCanvas (update media + open/close menu window)
// ------------------------------------------------------------

void RocketRhythm::RenderCanvas(const CanvasWrapper& canvas)
{
    static auto lastTime = std::chrono::steady_clock::now();
    const auto now = std::chrono::steady_clock::now();
    const float dt = std::chrono::duration<float>(now - lastTime).count();
    lastTime = now;

    if (mMedia)
    {
        mMedia->Update();
        mMediaState = mMedia->GetState();
        mIsNotPlaying = !mMediaState.isPlaying && mMediaState.title.empty();
    }

    UpdateAnimation(dt);
    UpdateWindowState();

    const std::string& menuName = GetMenuNameCached();
    if (mNeedsWindowOpen && !isWindowOpen_)
    {
        cvarManager->executeCommand("openmenu " + menuName);
        mNeedsWindowOpen = false;
    }
    else if (mNeedsWindowClose && isWindowOpen_)
    {
        cvarManager->executeCommand("closemenu " + menuName);
        mNeedsWindowClose = false;
    }
}

// ------------------------------------------------------------
// Config
// ------------------------------------------------------------

void RocketRhythm::SaveConfig()
{
    if (!mEnabled || !mUiScaleCvar)
    {
        LOG("SaveConfig skipped: CVars not initialized yet");
        return;
    }

    try
    {
        const auto path = gameWrapper->GetDataFolder() / kConfigDir / kConfigFileName;
        std::filesystem::create_directories(path.parent_path());

        nlohmann::json j = {
            {"version", kPluginConfigVersion},
            {"enabled", *mEnabled},
            {"hide_when_not_playing", mHideWhenNotPlaying},
            {"window_style", mWindowStyle}
        };

        const auto tmpPath = path.string() + ".tmp";

        {
            std::ofstream file(tmpPath, std::ios::binary | std::ios::trunc);
            if (!file)
            {
                LOG("Error saving config: could not open file for writing: {}", tmpPath);
                notify(Error, "Error saving config: could not open file for writing: {}", tmpPath);
                return;
            }

            file << j.dump(4);
            if (!file)
            {
                LOG("Error saving config: write failed: {}", tmpPath);
                notify(Error, "Error saving config: write failed: {}", tmpPath);
                return;
            }
        }

        std::error_code ec;
        std::filesystem::rename(tmpPath, path, ec);
        if (ec)
        {
            std::filesystem::remove(path, ec);
            ec.clear();
            std::filesystem::rename(tmpPath, path, ec);
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
    if (!mEnabled || !mUiScaleCvar)
    {
        LOG("LoadConfig skipped: CVars not initialized yet");
        return;
    }

    try
    {
        // Defaults
        *mEnabled = true;
        mHideWhenNotPlaying = true;
        mWindowStyle = DefaultWindowStyle();

        const auto path = gameWrapper->GetDataFolder() / kConfigDir / kConfigFileName;

        if (!std::filesystem::exists(path))
        {
            *mUiScaleCvar = mWindowStyle.uiScale;
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

        if (version != kPluginConfigVersion)
        {
            LOG("Config version mismatch (have {}, want {}); resetting to defaults", version, kPluginConfigVersion);
            *mUiScaleCvar = mWindowStyle.uiScale;
            SaveConfig();
            return;
        }

        *mEnabled = j.value("enabled", *mEnabled);
        mHideWhenNotPlaying = j.value("hide_when_not_playing", mHideWhenNotPlaying);

        if (j.contains("window_style") && j["window_style"].is_object())
            mWindowStyle = j["window_style"].get<WindowStyle>();

        *mUiScaleCvar = mWindowStyle.uiScale;

        LOG("Config Loaded!");
    }
    catch (const std::exception& e)
    {
        LOG("Error loading config: {}", e.what());
        *mUiScaleCvar = mWindowStyle.uiScale;
        SaveConfig();
    }
}