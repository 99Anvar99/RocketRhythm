#include "pch.h"
#include "RocketRhythm.h"

#include <urlmon.h>
#pragma comment(lib, "urlmon.lib")

#include <algorithm>
#include <cmath>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <shellapi.h>
#include <ShlObj.h>
#include <sstream>
#include <string>
#include <utility>
#include <Windows.h>

#include <nlohmann/json.hpp>
#include "notification.h"

#include "version.h"
#include "bakkesmod/wrappers/GuiManagerWrapper.h"
#include "IMGUI/imgui_internal.h"

std::shared_ptr<CVarManagerWrapper> _globalCvarManager;

static constexpr auto plugin_version =
    std::string(stringify(VERSION_MAJOR)) + "." +
    stringify(VERSION_MINOR) + "." +
    stringify(VERSION_PATCH) + "." +
    stringify(VERSION_BUILD);

static constexpr auto kConfigFileName = "config.json";
static constexpr auto kConfigDir = "RocketRhythm";
static constexpr auto kPluginNameStr = "RocketRhythm";

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

static std::string HashStringStable(const std::string& text)
{
    uint64_t h = 14695981039346656037ull;
    for (unsigned char c : text)
    {
        h ^= static_cast<uint64_t>(c);
        h *= 1099511628211ull;
    }

    char buf[17]{};
    snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(h));
    return std::string(buf);
}

static std::string ImageExtensionFromUrl(const std::string& url)
{
    const size_t query = url.find('?');
    const std::string clean = url.substr(0, query == std::string::npos ? url.size() : query);
    const size_t dot = clean.find_last_of('.');
    if (dot == std::string::npos)
        return ".jpg";

    std::string ext = clean.substr(dot);
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c)
    {
        return static_cast<char>(std::tolower(c));
    });

    if (ext == ".jpg" || ext == ".jpeg" || ext == ".png" || ext == ".webp" || ext == ".bmp")
        return ext;

    return ".jpg";
}

static nlohmann::json ImVec4ToJson(const ImVec4& c)
{
    return nlohmann::json::array({c.x, c.y, c.z, c.w});
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
    float waitTimeSec
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
    dl->PopClipRect();
}

// ------------------------------------------------------------
// JSON (WindowStyle)
// ------------------------------------------------------------

void to_json(nlohmann::json& j, const RocketRhythm::WindowStyle& s)
{
    j = nlohmann::json{
        {"background_color", ImVec4ToJson(s.backgroundColor)},
        {"accent_color", ImVec4ToJson(s.accentColor)},
        {"accent_color2", ImVec4ToJson(s.accentColor2)},
        {"text_color", ImVec4ToJson(s.textColor)},
        {"text_color_dim", ImVec4ToJson(s.textColorDim)},
        {"text_color_faint", ImVec4ToJson(s.textColorFaint)},

        {"window_rounding", s.windowRounding},
        {"album_art_rounding", s.albumArtRounding},
        {"progress_bar_height", s.progressBarHeight},
        {"progress_bar_rounding", s.progressBarRounding},
        {"album_art_size", s.albumArtSize},

        {"enable_pulse", s.enablePulse},
        {"show_album_art", s.showAlbumArt},
        {"show_progress_bar", s.showProgressBar},
        {"show_album_info", s.showAlbumInfo},
        {"window_opacity", s.windowOpacity},

        {"ui_scale", s.uiScale},
        {"enable_auto_scaling", s.enableAutoScaling},
        {"min_scale", s.minScale},
        {"max_scale", s.maxScale},

        {"enable_marquee", s.enableMarquee},
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
        if (it == j.end())
        {
            target = fallback;
            return;
        }
        ImVec4 tmp;
        if (JsonToImVec4(*it, tmp)) target = tmp;
        else target = fallback;
    };

    loadColor("background_color", s.backgroundColor, def.backgroundColor);
    loadColor("accent_color", s.accentColor, def.accentColor);
    loadColor("accent_color2", s.accentColor2, def.accentColor2);
    loadColor("text_color", s.textColor, def.textColor);
    loadColor("text_color_dim", s.textColorDim, def.textColorDim);
    loadColor("text_color_faint", s.textColorFaint, def.textColorFaint);

    int tdm = static_cast<int>(def.timeDisplayMode);
    tdm = std::clamp(tdm, 0, 1);

    AssignIfNumberOrBool(j, "window_rounding", s.windowRounding);
    AssignIfNumberOrBool(j, "album_art_rounding", s.albumArtRounding);
    AssignIfNumberOrBool(j, "progress_bar_height", s.progressBarHeight);
    AssignIfNumberOrBool(j, "progress_bar_rounding", s.progressBarRounding);
    AssignIfNumberOrBool(j, "album_art_size", s.albumArtSize);

    AssignIfNumberOrBool(j, "enable_pulse", s.enablePulse);
    AssignIfNumberOrBool(j, "show_album_art", s.showAlbumArt);
    AssignIfNumberOrBool(j, "show_progress_bar", s.showProgressBar);
    AssignIfNumberOrBool(j, "show_album_info", s.showAlbumInfo);
    AssignIfNumberOrBool(j, "window_opacity", s.windowOpacity);

    AssignIfNumberOrBool(j, "ui_scale", s.uiScale);
    AssignIfNumberOrBool(j, "enable_auto_scaling", s.enableAutoScaling);
    AssignIfNumberOrBool(j, "min_scale", s.minScale);
    AssignIfNumberOrBool(j, "max_scale", s.maxScale);

    AssignIfNumberOrBool(j, "enable_marquee", s.enableMarquee);
    AssignIfNumberOrBool(j, "marquee_speed_px", s.marqueeSpeedPx);
    AssignIfNumberOrBool(j, "marquee_wait_sec", s.marqueeWaitSec);
    AssignIfNumberOrBool(j, "time_display_mode", tdm);

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
    cvarManager->registerCvar("rr_uiscale", "1.0", "UI Scale factor", true, true, 0.5f, true, 2.0f).
                 bindTo(mUiScaleCvar);

    InitializeSpotify();
    LoadConfig();

    gameWrapper->RegisterDrawable([this](const CanvasWrapper& canvas) { RenderCanvas(canvas); });

    LOG("{} v{} loaded!", kPluginNameStr, plugin_version);
}

void RocketRhythm::onUnload()
{
    SaveConfig();
    SaveSpotifyConfig();

    if (mSpotify)
    {
        mSpotify->StopPolling();
    }

    if (mSpotifyAlbumArtThread.joinable())
    {
        mSpotifyAlbumArtThread.join();
    }

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

    const std::filesystem::path bmData = gameWrapper->GetDataFolder();
    const std::filesystem::path fontDir = bmData / "fonts" / "RocketRhythm";
    const std::filesystem::path fontFile = fontDir / "segoeui.ttf";

    static const std::wstring kFontUrl =
        LR"(https://raw.githubusercontent.com/99Anvar99/RocketRhythm/main/fonts/segoeui.ttf)";

    static constexpr auto kFontRel = R"(RocketRhythm/segoeui.ttf)";

    // Ensure directory exists
    std::error_code ec;
    std::filesystem::create_directories(fontDir, ec);
    if (ec)
    {
        LOG("Failed to create fonts dir: {} ({})", fontDir.string(), ec.message());
    }

    static std::atomic_bool sDownloadInFlight{false};

    const bool haveFontFile = std::filesystem::exists(fontFile);

    if (!haveFontFile && !sDownloadInFlight.exchange(true))
    {
        // capture only what we need by value
        const std::wstring urlCopy = kFontUrl;
        const std::filesystem::path dstFinal = fontFile;
        const std::filesystem::path dstTemp = fontDir / "segoeui.tmp";

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
    static constexpr auto kOverlayKey = "rr_overlay_24";
    static constexpr auto kSettingsKey = "rr_settings_16";

    if (std::filesystem::exists(fontFile))
    {
        // Build ranges once
        static ImVector<ImWchar> sGlyphRanges;
        static std::atomic_bool sRangesBuilt{false};

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
            auto [res, font] = gui.LoadFont(kOverlayKey, kFontRel, 24, nullptr, sGlyphRanges.Data);
            if ((res == 0 || res == 2) && font) mFontOverlay = font;
            if (!mFontOverlay) mFontOverlay = gui.GetFont(kOverlayKey);
        }

        if (!mFontSettings)
        {
            auto [res, font] = gui.LoadFont(kSettingsKey, kFontRel, 16, nullptr, sGlyphRanges.Data);
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
    if (path.empty())
    {
        if (!mAlbumArtLoaded && !mAlbumArtTexture && mAlbumArtPath.empty())
            return;

        mAlbumArtLoaded = false;
        mAlbumArtTexture.reset();
        mAlbumArtPath.clear();
        return;
    }

    if (mAlbumArtLoaded && mAlbumArtTexture && mAlbumArtPath == path)
        return;

    if (!IsValidImageFile(path))
    {
        mAlbumArtLoaded = false;
        mAlbumArtTexture.reset();
        mAlbumArtPath.clear();
        return;
    }

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

std::string RocketRhythm::GetSpotifyAlbumArtPath(const rrspotify::SpotifyTrackInfo& track)
{
    if (track.albumArtUrl.empty() || !gameWrapper)
        return {};

    {
        std::lock_guard<std::mutex> lock(mSpotifyAlbumArtMutex);
        if (track.albumArtUrl == mSpotifyAlbumArtUrl && !mSpotifyAlbumArtPath.empty())
            return mSpotifyAlbumArtPath;

        if (track.albumArtUrl != mSpotifyAlbumArtUrl)
        {
            if (mSpotifyAlbumArtDownloadInFlight)
                return {};

            mSpotifyAlbumArtUrl = track.albumArtUrl;
            mSpotifyAlbumArtPath.clear();
        }
    }

    const std::filesystem::path cacheDir = gameWrapper->GetDataFolder() / kConfigDir / "spotify_album_cache";
    const std::filesystem::path cachePath =
        cacheDir / (HashStringStable(track.albumArtUrl) + ImageExtensionFromUrl(track.albumArtUrl));

    if (IsValidImageFile(cachePath.string()))
    {
        std::lock_guard<std::mutex> lock(mSpotifyAlbumArtMutex);
        if (track.albumArtUrl == mSpotifyAlbumArtUrl)
            mSpotifyAlbumArtPath = cachePath.string();
        return cachePath.string();
    }

    {
        std::lock_guard<std::mutex> lock(mSpotifyAlbumArtMutex);
        if (mSpotifyAlbumArtDownloadInFlight)
            return {};

        mSpotifyAlbumArtDownloadInFlight = true;
    }

    if (mSpotifyAlbumArtThread.joinable())
    {
        mSpotifyAlbumArtThread.join();
    }

    const std::string url = track.albumArtUrl;
    mSpotifyAlbumArtThread = std::thread([this, url, cachePath]()
    {
        std::string err;
        const bool ok = DownloadToFile(ToWide(url), cachePath, err) && IsValidImageFile(cachePath.string());

        std::lock_guard<std::mutex> lock(mSpotifyAlbumArtMutex);
        if (url == mSpotifyAlbumArtUrl)
        {
            mSpotifyAlbumArtPath = ok ? cachePath.string() : std::string{};
        }
        mSpotifyAlbumArtDownloadInFlight = false;
    });

    return {};
}

// ------------------------------------------------------------
// Playback position smoothing
// ------------------------------------------------------------

int RocketRhythm::GetCurrentDisplayPositionSec()
{
    const auto now = std::chrono::steady_clock::now();

    const std::string trackKey = mMediaState.title + "|" + mMediaState.artist + "|" + mMediaState.album;
    const int duration = std::max(0, mMediaState.durationSec);
    const int rawPosition = std::clamp(mMediaState.positionSec, 0, duration);

    if (trackKey != mLastTrackKey)
    {
        mLastTrackKey = trackKey;
        mLastPositionSec = rawPosition;
        mAnchoredPositionSec = rawPosition;
        mLastProgressAnchor = now;
    }

    if (!mMediaState.isPlaying || duration <= 0)
    {
        mLastPositionSec = rawPosition;
        mAnchoredPositionSec = rawPosition;
        mLastProgressAnchor = now;
        return rawPosition;
    }

    const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - mLastProgressAnchor).count();
    int predicted = std::clamp(mAnchoredPositionSec + static_cast<int>(elapsedMs / 1000), 0, duration);

    if (rawPosition != mLastPositionSec)
    {
        const int drift = rawPosition - predicted;

        // Media APIs can report a stale position shortly after our local clock has advanced.
        // Ignore only small backward drift; large backward jumps are treated as real seeks.
        if (drift >= 0 || drift <= -5)
        {
            mLastPositionSec = rawPosition;
            mAnchoredPositionSec = rawPosition;
            mLastProgressAnchor = now;
            predicted = rawPosition;
        }
        else
        {
            mLastPositionSec = rawPosition;
        }
    }

    return predicted;
}

// ------------------------------------------------------------
// Scaling
// ------------------------------------------------------------

float RocketRhythm::GetDpiScaleFactor()
{
    HDC screen = GetDC(nullptr);
    if (!screen) return 1.0f;
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
    LoadAlbumArt(mMediaState.hasAlbumArt ? mMediaState.albumArtPath : std::string{});

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float size = mWindowStyle.albumArtSize * scale;

    if (mAlbumArtLoaded && mAlbumArtTexture && mAlbumArtTexture->IsLoadedForImGui())
    {
        if (ImTextureID tex = mAlbumArtTexture->GetImGuiTex())
        {
            ImGui::Image(tex, ImVec2(size, size));
            const ImU32 border = ImGui::GetColorU32(ImVec4(1, 1, 1, 0.10f));
            dl->AddRect(pos, ImVec2(pos.x + size, pos.y + size), border, mWindowStyle.albumArtRounding * scale, 0,
                        1.0f);

            ImGui::SetCursorPos(ImGui::GetCursorPos() + ImVec2(size + 15.0f * scale, 0));
            return;
        }
    }

    DrawAlbumArtPlaceholder(scale);
}

// ------------------------------------------------------------
// Drawing (progress bar)
// ------------------------------------------------------------

void RocketRhythm::DrawProgressBar(float scale)
{
    if (!mWindowStyle.showProgressBar || mMediaState.durationSec <= 0) return;

    const int currentPos = GetCurrentDisplayPositionSec();
    float progress = static_cast<float>(currentPos) / static_cast<float>(mMediaState.durationSec);
    progress = std::clamp(progress, 0.0f, 1.0f);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    const float height = mWindowStyle.progressBarHeight * scale;

    const ImU32 bgCol = ImGui::GetColorU32(ImVec4(0.15f, 0.15f, 0.20f, 0.8f));

    float bgRounding = mWindowStyle.progressBarRounding * scale;
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

        float fillRounding = mWindowStyle.progressBarRounding * scale;
        fillRounding = std::min(fillRounding, height * 0.5f);
        fillRounding = std::min(fillRounding, fillWidth * 0.5f);

        const ImU32 fillCol = ImGui::GetColorU32(fillColor);
        dl->AddRectFilled(pos, ImVec2(pos.x + fillWidth, pos.y + height), fillCol, fillRounding);

        const ImU32 highlight = ImGui::GetColorU32(ImVec4(1, 1, 1, 0.20f));
        dl->AddLine(ImVec2(pos.x, pos.y + 1), ImVec2(pos.x + fillWidth, pos.y + 1), highlight, 1.0f);
    }

    // Time labels
    const float yText = pos.y + height + 4.0f * scale;
    const ImU32 textCol = ImGui::GetColorU32(mWindowStyle.textColorDim);

    if (mWindowStyle.timeDisplayMode == WindowStyle::TimeDisplayMode::Corners)
    {
        const std::string leftTime = FormatTimeSeconds(currentPos);
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
    ImGui::Dummy(ImVec2(width, ImGui::GetTextLineHeight() + 12.0f * scale));
}

// ------------------------------------------------------------
// Drawing (music state)
// ------------------------------------------------------------

void RocketRhythm::DrawMusicStateCompact(float scale)
{
    if (mFontOverlay) ImGui::PushFont(mFontOverlay);

    const float speed = mWindowStyle.marqueeSpeedPx * scale;
    const float wait = mWindowStyle.marqueeWaitSec;

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
            DrawPingPongMarqueeText(mMediaState.title.c_str(), c, ImGui::GetContentRegionAvail().x, speed, wait);
        else
            ImGui::TextColored(c, "%s", mMediaState.title.c_str());
    }

    // Artist
    if (!mMediaState.artist.empty())
    {
        if (mWindowStyle.enableMarquee)
            DrawPingPongMarqueeText(mMediaState.artist.c_str(), mWindowStyle.textColorDim,
                                    ImGui::GetContentRegionAvail().x, speed, wait);
        else
            ImGui::TextColored(mWindowStyle.textColorDim, "%s", mMediaState.artist.c_str());
    }

    // Album
    if (mWindowStyle.showAlbumInfo && !mMediaState.album.empty())
    {
        if (mWindowStyle.enableMarquee)
            DrawPingPongMarqueeText(mMediaState.album.c_str(), mWindowStyle.textColorFaint,
                                    ImGui::GetContentRegionAvail().x, speed, wait);
        else
            ImGui::TextColored(mWindowStyle.textColorFaint, "%s", mMediaState.album.c_str());
    }

    ImGui::Spacing();

    if (mWindowStyle.showProgressBar && mMediaState.durationSec > 0)
        DrawProgressBar(scale);

    if (mFontOverlay) ImGui::PopFont();
}

void RocketRhythm::DrawNoMusicState()
{
    const ImVec2 ws = ImGui::GetWindowSize();
    const ImVec2 center(ws.x * 0.5f, ws.y * 0.5f);

    auto mainText = "No Music Playing";
    auto subText = "Play a song to see track info";

    const float mainW = ImGui::CalcTextSize(mainText).x;
    const float subW = ImGui::CalcTextSize(subText).x;

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

void RocketRhythm::DrawHelpMarkerIcon(const char* icon, const char* desc)
{
    ImGui::Text("%s", icon);
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

    // Apply modern styling
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14, 12));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(7, 4));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(7, 5));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_GrabRounding, 6.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);

    // Push modern colors
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.15f, 0.15f, 0.18f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.20f, 0.20f, 0.24f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.25f, 0.25f, 0.30f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Button, mWindowStyle.accentColor);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, mWindowStyle.accentColor2);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(
        mWindowStyle.accentColor.x * 0.9f,
        mWindowStyle.accentColor.y * 0.9f,
        mWindowStyle.accentColor.z * 0.9f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_SliderGrab, mWindowStyle.accentColor);
    ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, mWindowStyle.accentColor2);
    ImGui::PushStyleColor(ImGuiCol_CheckMark, mWindowStyle.accentColor);
    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.20f, 0.20f, 0.24f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.25f, 0.25f, 0.30f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.30f, 0.30f, 0.35f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Tab, ImVec4(0.15f, 0.15f, 0.18f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_TabHovered, mWindowStyle.accentColor2);
    ImGui::PushStyleColor(ImGuiCol_TabActive, mWindowStyle.accentColor);
    ImGui::PushStyleColor(ImGuiCol_TabUnfocused, ImVec4(0.15f, 0.15f, 0.18f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_TabUnfocusedActive, ImVec4(0.20f, 0.20f, 0.24f, 1.0f));

    RenderModernSettings();

    ImGui::PopStyleColor(17);
    ImGui::PopStyleVar(7);
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
        baseWidth = mWindowStyle.albumArtSize + 15.0f + 235.0f + 15.0f;
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
                DrawMusicStateCompact(dynamicScale);
                ImGui::Columns(1);
            }
            else
            {
                DrawMusicStateCompact(dynamicScale);
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
    (void)canvas;

    if (mMedia)
    {
        mMedia->Update();
        mMediaState = mMedia->GetState();
    }

    UpdateFromSpotify();
    mIsNotPlaying = !mMediaState.isPlaying && mMediaState.title.empty();

    if (mSpotifyConfigSavePending.exchange(false))
    {
        SaveSpotifyConfig();
    }

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
            LoadSpotifyConfig();
            return;
        }

        std::ifstream file(path, std::ios::binary);
        if (!file)
        {
            LOG("Error loading config: could not open file: {}", path.string());
            LoadSpotifyConfig();
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
            LoadSpotifyConfig();
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

    // Load Spotify config
    LoadSpotifyConfig();
}

// ------------------------------------------------------------
// Modern Settings UI Implementation
// ------------------------------------------------------------

void RocketRhythm::RenderModernSettings()
{
    ImGui::PushFont(mFontSettings);
    RenderSettingsTabBar();
    ImGui::PopFont();
}

void RocketRhythm::RenderSettingsTabBar()
{
    const std::string& pluginName = GetPluginNameCached();

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 5.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 7.0f));

    ImGui::PushStyleColor(ImGuiCol_Text, mWindowStyle.accentColor);
    ImGui::Text("%s", pluginName.c_str());
    ImGui::PopStyleColor();

    ImGui::PushStyleColor(ImGuiCol_Text, mWindowStyle.textColorFaint);
    ImGui::SameLine();
    ImGui::Text("v%s", plugin_version.c_str());
    ImGui::PopStyleColor();

    ImGui::Spacing();

    if (ImGui::BeginTabBar("RocketRhythmSettingsTabs", ImGuiTabBarFlags_FittingPolicyResizeDown))
    {
        if (ImGui::BeginTabItem("General"))
        {
            mSettingsTab = 0;
            RenderGeneralTab();
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Appearance"))
        {
            mSettingsTab = 1;
            RenderAppearanceTab();
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Spotify"))
        {
            mSettingsTab = 2;
            RenderSpotifyTab();
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("About"))
        {
            mSettingsTab = 3;
            RenderAboutTab();
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::PopStyleVar(2);
}

void RocketRhythm::RenderSectionHeader(const char* title, const char* subtitle)
{
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, mWindowStyle.accentColor);
    ImGui::Text("%s", title);
    ImGui::PopStyleColor();

    if (subtitle)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, mWindowStyle.textColorFaint);
        ImGui::TextWrapped("%s", subtitle);
        ImGui::PopStyleColor();
    }

    ImGui::Spacing();
}

bool RocketRhythm::BeginSettingsCard(const char* title, const char* icon, ImVec4 accentColor)
{
    ImVec4 titleColor = accentColor.w > 0 ? accentColor : mWindowStyle.accentColor;

    ImGui::PushID(title);
    ImGui::BeginGroup();

    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, titleColor);
    if (icon && *icon)
        ImGui::Text("%s %s", icon, title);
    else
        ImGui::Text("%s", title);
    ImGui::PopStyleColor();
    ImGui::Spacing();

    return true;
}

void RocketRhythm::EndSettingsCard()
{
    ImGui::EndGroup();
    ImGui::PopID();
    ImGui::Spacing();
}

void RocketRhythm::RenderColorPicker(const char* label, ImVec4& color)
{
    ImGui::Text("%s", label);
    ImGui::SameLine(130.0f);
    ImGui::PushItemWidth(90);
    ImGui::ColorEdit4(label, &color.x, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel | ImGuiColorEditFlags_AlphaBar);
    ImGui::PopItemWidth();
}

void RocketRhythm::RenderGeneralTab()
{
    if (BeginSettingsCard("Plugin", "General"))
    {
        bool enabledValue = mEnabled ? *mEnabled : true;
        if (ImGui::Checkbox("Enable Plugin", &enabledValue))
        {
            if (mEnabled) *mEnabled = enabledValue;
            mNeedsWindowOpen = false;
            mNeedsWindowClose = false;
        }
        ImGui::SameLine();
        DrawHelpMarker("Toggle the entire plugin on/off");

        // Hide when not playing
        ImGui::Checkbox("Hide When Not Playing", &mHideWhenNotPlaying);
        ImGui::SameLine();
        DrawHelpMarker("Automatically hide overlay when no music is playing");

        EndSettingsCard();
    }

    if (BeginSettingsCard("Scaling", "Scale"))
    {
        ImGui::Checkbox("Auto scale", &mWindowStyle.enableAutoScaling);
        ImGui::SameLine();
        DrawHelpMarker("Automatically scale UI based on screen resolution and DPI");

        ImGui::PushItemWidth(-1.0f);
        if (mWindowStyle.enableAutoScaling)
        {
            ImGui::SliderFloat("Min", &mWindowStyle.minScale, 0.5f, 1.5f, "%.2f");
            ImGui::SliderFloat("Max", &mWindowStyle.maxScale, 1.0f, 3.0f, "%.2f");
        }

        if (ImGui::SliderFloat("UI Scale", &mWindowStyle.uiScale, 0.5f, 2.0f, "%.2f"))
        {
            if (mUiScaleCvar) *mUiScaleCvar = mWindowStyle.uiScale;
        }
        ImGui::PopItemWidth();

        EndSettingsCard();
    }

    if (BeginSettingsCard("Now Playing", "Media"))
    {
        ImGui::TextColored(mWindowStyle.textColorDim, "Source");
        ImGui::SameLine(92.0f);
        ImGui::Text("%s", mSpotifyEnabled && mSpotify && mSpotify->IsAuthenticated() ? "Spotify" : (mMedia ? "Windows Media" : "Disconnected"));

        ImGui::TextColored(mWindowStyle.textColorDim, "State");
        ImGui::SameLine(92.0f);
        if (mMediaState.isPlaying)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, mWindowStyle.accentColor);
            ImGui::Text("Playing");
        }
        else if (!mMediaState.title.empty())
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.7f, 0.3f, 1.0f));
            ImGui::Text("Paused");
        }
        else
        {
            ImGui::PushStyleColor(ImGuiCol_Text, mWindowStyle.textColorDim);
            ImGui::Text("No Media");
        }
        ImGui::PopStyleColor();

        if (!mMediaState.title.empty())
        {
            ImGui::TextColored(mWindowStyle.textColorDim, "Track");
            ImGui::SameLine(92.0f);
            ImGui::TextWrapped("%s", mMediaState.title.c_str());
            ImGui::TextColored(mWindowStyle.textColorDim, "Artist");
            ImGui::SameLine(92.0f);
            ImGui::TextWrapped("%s", mMediaState.artist.c_str());
        }

        EndSettingsCard();
    }
}

void RocketRhythm::RenderAppearanceTab()
{
    if (BeginSettingsCard("Colors", "Color"))
    {
        RenderColorPicker("Background", mWindowStyle.backgroundColor);
        RenderColorPicker("Accent Primary", mWindowStyle.accentColor);
        RenderColorPicker("Accent Secondary", mWindowStyle.accentColor2);
        RenderColorPicker("Text", mWindowStyle.textColor);
        RenderColorPicker("Text Dim", mWindowStyle.textColorDim);
        RenderColorPicker("Text Faint", mWindowStyle.textColorFaint);

        EndSettingsCard();
    }

    if (BeginSettingsCard("Time", "Time"))
    {
        int mode = static_cast<int>(mWindowStyle.timeDisplayMode);
        const char* items[] = {"Centered", "Corners"};

        ImGui::PushItemWidth(-1.0f);
        if (ImGui::Combo("Display", &mode, items, IM_ARRAYSIZE(items)))
        {
            mode = std::clamp(mode, 0, 1);
            mWindowStyle.timeDisplayMode = static_cast<WindowStyle::TimeDisplayMode>(mode);
        }
        ImGui::PopItemWidth();

        EndSettingsCard();
    }

    if (BeginSettingsCard("Layout", "Display"))
    {
        ImGui::Checkbox("Art", &mWindowStyle.showAlbumArt);
        ImGui::SameLine();
        ImGui::Checkbox("Progress", &mWindowStyle.showProgressBar);
        ImGui::SameLine();
        ImGui::Checkbox("Album", &mWindowStyle.showAlbumInfo);

        ImGui::PushItemWidth(-1.0f);
        if (mWindowStyle.showAlbumArt)
        {
            ImGui::SliderFloat("Art Size", &mWindowStyle.albumArtSize, 80.0f, 150.0f, "%.0f px");
            ImGui::SliderFloat("Art Round", &mWindowStyle.albumArtRounding, 0.0f, 30.0f, "%.0f px");
        }

        ImGui::SliderFloat("Window Round", &mWindowStyle.windowRounding, 0.0f, 30.0f, "%.0f px");
        ImGui::SliderFloat("Opacity", &mWindowStyle.windowOpacity, 0.5f, 1.0f, "%.2f");
        ImGui::PopItemWidth();

        EndSettingsCard();
    }

    if (BeginSettingsCard("Motion", "Motion"))
    {
        ImGui::Checkbox("Pulse", &mWindowStyle.enablePulse);
        ImGui::SameLine();
        DrawHelpMarker("Adds a subtle pulse animation when music is playing");

        ImGui::SameLine();
        ImGui::Checkbox("Marquee", &mWindowStyle.enableMarquee);
        ImGui::SameLine();
        DrawHelpMarker("Scroll long text left-right with pauses");

        ImGui::PushItemWidth(-1.0f);
        if (mWindowStyle.enableMarquee)
        {
            ImGui::SliderFloat("Speed", &mWindowStyle.marqueeSpeedPx, 10.0f, 200.0f, "%.0f px/sec");
            ImGui::SliderFloat("Wait", &mWindowStyle.marqueeWaitSec, 0.0f, 2.0f, "%.2f sec");
        }
        ImGui::PopItemWidth();

        EndSettingsCard();
    }

    ImGui::Spacing();
    float btnWidth = 112;
    float spacing = 6;
    float totalWidth = btnWidth * 3.0f + spacing * 2.0f;
    float startX = (ImGui::GetWindowWidth() - totalWidth) * 0.5f;

    ImGui::SetCursorPosX(startX);

    if (ImGui::Button("Save", ImVec2(btnWidth, 30)))
    {
        SaveConfig();
        notify(Info, "{}: Config Saved!", kPluginNameStr);
    }

    ImGui::SameLine(0, spacing);

    if (ImGui::Button("Load", ImVec2(btnWidth, 30)))
    {
        LoadConfig();
        notify(Info, "{}: Config Loaded!", kPluginNameStr);
    }

    ImGui::SameLine(0, spacing);

    if (ImGui::Button("Reset", ImVec2(btnWidth, 30)))
    {
        if (mEnabled) *mEnabled = true;
        mHideWhenNotPlaying = true;
        mWindowStyle = DefaultWindowStyle();
        if (mUiScaleCvar) *mUiScaleCvar = mWindowStyle.uiScale;
        notify(Info, "{}: Settings Reset To Default!", kPluginNameStr);
    }
}

void RocketRhythm::RenderSpotifyTab()
{
    if (!mSpotify)
    {
        if (BeginSettingsCard("Spotify Integration", "Spotify"))
        {
            ImGui::TextColored(mWindowStyle.textColorDim, "Spotify integration is initializing...");
            EndSettingsCard();
        }
        return;
    }

    bool isAuthenticated = mSpotify->IsAuthenticated();
    ImVec4 spotifyGreen = ImVec4(0.13f, 0.74f, 0.35f, 1.0f);

    if (BeginSettingsCard(isAuthenticated ? "Connection" : "Spotify Authentication",
                          isAuthenticated ? "Status" : "Auth", spotifyGreen))
    {
        if (ImGui::Checkbox("Use Spotify for overlay data", &mSpotifyEnabled))
        {
            if (mSpotifyEnabled)
                mSpotify->StartPolling();
            SaveSpotifyConfig();
        }
        ImGui::SameLine();
        DrawHelpMarker("When enabled, Spotify Web API metadata overrides the Windows media session when available.");

        ImGui::SameLine();
        if (ImGui::Checkbox("Controls", &mShowSpotifyControls))
        {
            SaveSpotifyConfig();
        }

        ImGui::Spacing();

        if (!isAuthenticated)
        {
            ImGui::TextColored(mWindowStyle.textColorDim,
                "Connect Spotify to use Web API metadata and playback controls.");

            ImGui::PushItemWidth(-1.0f);
            ImGui::InputText("Client ID", &mSpotifyClientIdInput);
            ImGui::InputText("Client Secret", &mSpotifyClientSecretInput, ImGuiInputTextFlags_Password);
            ImGui::PopItemWidth();
            ImGui::SameLine();
            DrawHelpMarker("Optional when using Spotify's PKCE flow. If you enter a secret, it is stored locally in the plugin config.");

            const bool authActive = mSpotify->IsAuthFlowActive();
            const std::string authError = mSpotify->GetLastAuthError();
            if (authActive)
            {
                ImGui::TextColored(mWindowStyle.textColorDim, "Waiting for Spotify authorization in your browser...");
            }

            if (!authError.empty())
            {
                ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.35f, 1.0f), "%s", authError.c_str());
            }

            const bool connectDisabled = mSpotifyClientIdInput.empty() || authActive;
            if (connectDisabled)
                ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.55f);

            if (ImGui::Button("Connect", ImVec2(112, 30)) && !connectDisabled)
            {
                rrspotify::SpotifyAuthConfig config;
                config.clientId = mSpotifyClientIdInput;
                config.clientSecret = mSpotifyClientSecretInput;
                mSpotify->SetAuthConfig(config);
                SaveSpotifyConfig();
                mSpotify->StartAuthFlow();
                notify(Info, "{}: Opening Spotify authorization page...", kPluginNameStr);
            }

            if (connectDisabled)
                ImGui::PopStyleVar();

            ImGui::TextColored(mWindowStyle.textColorFaint,
                "Set your Spotify app redirect URI to http://127.0.0.1:9982/callback.");
        }
        else
        {
            ImGui::PushStyleColor(ImGuiCol_Text, spotifyGreen);
            ImGui::Text("Connected to Spotify");
            ImGui::PopStyleColor();

            rrspotify::SpotifyTrackInfo track = mSpotify->GetCurrentTrack();

            if (!track.title.empty())
            {
                ImGui::TextColored(mWindowStyle.textColorDim, "Track");
                ImGui::SameLine(70.0f);
                ImGui::Text("%s", track.title.c_str());
                ImGui::TextColored(mWindowStyle.textColorDim, "Artist");
                ImGui::SameLine(70.0f);
                ImGui::TextWrapped("%s", track.artist.c_str());
                ImGui::TextColored(mWindowStyle.textColorDim, "Album");
                ImGui::SameLine(70.0f);
                ImGui::TextWrapped("%s", track.album.c_str());

                if (track.isLiked)
                {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.9f, 0.2f, 0.2f, 1.0f));
                    if (ImGui::Button("Liked", ImVec2(86, 28)))
                    {
                        mSpotify->ToggleLike();
                    }
                    ImGui::PopStyleColor();
                }
                else
                {
                    if (ImGui::Button("Favorite", ImVec2(86, 28)))
                    {
                        mSpotify->ToggleLike();
                    }
                }
            }
            else
            {
                ImGui::TextColored(mWindowStyle.textColorDim, "No Spotify playback data yet");
            }

            ImGui::SameLine();
            if (ImGui::Button("Refresh", ImVec2(86, 28)))
            {
                mSpotify->RefreshPlaybackState();
            }

            ImGui::SameLine();

            if (ImGui::Button("Disconnect", ImVec2(100, 28)))
            {
                mSpotify->Logout();
                mSpotifyEnabled = false;
                SaveSpotifyConfig();
                notify(Info, "{}: Disconnected from Spotify", kPluginNameStr);
            }
        }

        EndSettingsCard();
    }

    if (isAuthenticated && mShowSpotifyControls)
    {
        if (BeginSettingsCard("Playback Controls", "Controls", spotifyGreen))
        {
            float btnWidth = 72;
            float spacing = 6;
            float totalWidth = btnWidth * 4 + spacing * 3;
            float startX = (ImGui::GetContentRegionAvail().x - totalWidth) * 0.5f;

            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + startX);

            ImGui::PushStyleColor(ImGuiCol_Button, spotifyGreen);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(spotifyGreen.x * 1.1f, spotifyGreen.y * 1.1f, spotifyGreen.z * 1.1f, 1.0f));

            if (ImGui::Button("Prev", ImVec2(btnWidth, 30)))
            {
                mSpotify->Previous();
            }

            ImGui::SameLine(0, spacing);

            if (ImGui::Button("Play", ImVec2(btnWidth, 30)))
            {
                mSpotify->Play();
            }

            ImGui::SameLine(0, spacing);

            if (ImGui::Button("Pause", ImVec2(btnWidth, 30)))
            {
                mSpotify->Pause();
            }

            ImGui::SameLine(0, spacing);

            if (ImGui::Button("Next", ImVec2(btnWidth, 30)))
            {
                mSpotify->Next();
            }

            ImGui::PopStyleColor(2);

            EndSettingsCard();
        }
    }
}

void RocketRhythm::RenderAboutTab()
{
    if (BeginSettingsCard("About RocketRhythm", "Info"))
    {
        // Centered logo/name
        ImGui::PushStyleColor(ImGuiCol_Text, mWindowStyle.accentColor);
        ImGui::SetCursorPosX((ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize("RocketRhythm").x) * 0.5f);
        ImGui::Text("RocketRhythm");
        ImGui::PopStyleColor();

        ImGui::Spacing();

        ImGui::TextColored(mWindowStyle.textColorDim, "A modern music overlay for Rocket League");

        ImGui::Spacing();

        ImGui::Text("Version: %s", plugin_version.c_str());
        ImGui::Text("Developed by: Mister9982");

        ImGui::Spacing();

        // Links
        float btnWidth = 140;
        float spacing = 10;
        float totalWidth = btnWidth * 2 + spacing;
        float startX = (ImGui::GetContentRegionAvail().x - totalWidth) * 0.5f;

        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + startX);

        if (ImGui::Button("GitHub", ImVec2(btnWidth, 36)))
        {
            ShellExecuteA(nullptr, "open", "https://github.com/99Anvar99/RocketRhythm", nullptr, nullptr, SW_SHOWNORMAL);
        }

        ImGui::SameLine(0, spacing);

        if (ImGui::Button("Website", ImVec2(btnWidth, 36)))
        {
            ShellExecuteA(nullptr, "open", "https://rocket-rhythm-website.vercel.app", nullptr, nullptr, SW_SHOWNORMAL);
        }

        EndSettingsCard();
    }
}

// ------------------------------------------------------------
// Spotify Integration
// ------------------------------------------------------------

void RocketRhythm::InitializeSpotify()
{
    if (!mSpotify)
    {
        mSpotify = std::make_unique<rrspotify::SpotifyIntegration>();

        mSpotify->SetOnAuthStateChanged([this](bool authenticated)
        {
            if (authenticated)
            {
                mSpotifyConfigSavePending.store(true);
                notify(Info, "{}: Spotify connected", kPluginNameStr);
            }
        });
    }
}

void RocketRhythm::UpdateFromSpotify()
{
    if (!mSpotifyEnabled || !mSpotify)
        return;

    const rrspotify::SpotifyTrackInfo track = mSpotify->GetCurrentTrack();
    if (track.title.empty() && track.artist.empty())
        return;

    MediaState spotifyState;
    spotifyState.isPlaying = track.isPlaying;
    spotifyState.title = track.title;
    spotifyState.artist = track.artist;
    spotifyState.album = track.album;
    spotifyState.durationSec = std::max(0, track.durationMs / 1000);

    const auto now = std::chrono::steady_clock::now();
    const std::string trackKey = !track.id.empty()
                                     ? track.id
                                     : (track.title + "|" + track.artist + "|" + track.album);
    const bool newTrack = trackKey != mLastSpotifyTrackKey;
    const int durationMs = std::max(0, track.durationMs);
    const int rawPositionMs = std::clamp(track.positionMs, 0, durationMs);
    const bool reportedPositionChanged = newTrack || rawPositionMs != mLastSpotifyReportedPositionMs;
    int predictedPositionMs = rawPositionMs;
    if (!newTrack && durationMs > 0)
    {
        const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - mLastSpotifyPositionAnchor).count();
        predictedPositionMs = std::clamp(
            mLastSpotifyPositionMs + static_cast<int>(std::max<int64_t>(0, elapsedMs)),
            0,
            durationMs);
    }
    const int driftMs = rawPositionMs - predictedPositionMs;

    if (newTrack || !track.isPlaying || (reportedPositionChanged && (driftMs >= 1000 || driftMs <= -5000)))
    {
        mLastSpotifyTrackKey = trackKey;
        mLastSpotifyPositionMs = rawPositionMs;
        mLastSpotifyPositionAnchor = now;
    }
    mLastSpotifyReportedPositionMs = rawPositionMs;

    int displayPositionMs = mLastSpotifyPositionMs;
    if (track.isPlaying && durationMs > 0)
    {
        const auto anchoredElapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - mLastSpotifyPositionAnchor).count();
        displayPositionMs = std::clamp(
            mLastSpotifyPositionMs + static_cast<int>(std::max<int64_t>(0, anchoredElapsedMs)),
            0,
            durationMs);
    }

    spotifyState.positionSec = std::clamp(displayPositionMs / 1000, 0, spotifyState.durationSec);
    spotifyState.progress01 = spotifyState.durationSec > 0
                                  ? std::clamp(spotifyState.positionSec / static_cast<float>(spotifyState.durationSec),
                                               0.0f, 1.0f)
                                  : 0.0f;

    const std::string spotifyAlbumArtPath = GetSpotifyAlbumArtPath(track);
    if (!spotifyAlbumArtPath.empty())
    {
        spotifyState.hasAlbumArt = true;
        spotifyState.albumArtPath = spotifyAlbumArtPath;
    }

    const bool sameTrackAsGstmtc =
        !mMediaState.title.empty() &&
        mMediaState.title == spotifyState.title &&
        (mMediaState.artist.empty() || spotifyState.artist.empty() || mMediaState.artist == spotifyState.artist);

    if (!spotifyState.hasAlbumArt && sameTrackAsGstmtc && mMediaState.hasAlbumArt)
    {
        spotifyState.hasAlbumArt = true;
        spotifyState.albumArtPath = mMediaState.albumArtPath;
    }

    mMediaState = std::move(spotifyState);
}

void RocketRhythm::SaveSpotifyConfig()
{
    if (!mSpotify) return;

    try
    {
        const auto path = gameWrapper->GetDataFolder() / kConfigDir / "spotify_config.json";
        std::filesystem::create_directories(path.parent_path());

        rrspotify::SpotifyAuthConfig config = mSpotify->GetAuthConfig();

        nlohmann::json j;
        j["spotify_auth"] = config;
        j["spotify_enabled"] = mSpotifyEnabled;
        j["show_spotify_controls"] = mShowSpotifyControls;

        const auto tmpPath = path.string() + ".tmp";
        {
            std::ofstream file(tmpPath, std::ios::binary | std::ios::trunc);
            if (!file)
            {
                LOG("Error saving Spotify config: could not open file for writing: {}", tmpPath);
                return;
            }

            file << j.dump(4);
            if (!file)
            {
                LOG("Error saving Spotify config: write failed: {}", tmpPath);
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
            LOG("Error saving Spotify config: failed to move temp file into place: {}", ec.message());
        }
    }
    catch (const std::exception& e)
    {
        LOG("Error saving Spotify config: {}", e.what());
    }
}

void RocketRhythm::LoadSpotifyConfig()
{
    if (!mSpotify) return;

    try
    {
        const auto path = gameWrapper->GetDataFolder() / kConfigDir / "spotify_config.json";

        if (!std::filesystem::exists(path))
        {
            return;
        }

        std::ifstream file(path, std::ios::binary);
        if (!file) return;

        nlohmann::json j;
        file >> j;

        mSpotifyEnabled = j.value("spotify_enabled", false);
        mShowSpotifyControls = j.value("show_spotify_controls", true);

        if (j.contains("spotify_auth"))
        {
            rrspotify::SpotifyAuthConfig config = j["spotify_auth"].get<rrspotify::SpotifyAuthConfig>();
            mSpotify->SetAuthConfig(config);
            mSpotifyClientIdInput = config.clientId;
            mSpotifyClientSecretInput = config.clientSecret;

            if (config.IsValid() || !config.refreshToken.empty())
            {
                mSpotify->StartPolling();
            }
        }
    }
    catch (const std::exception& e)
    {
        LOG("Error loading Spotify config: {}", e.what());
    }
}
