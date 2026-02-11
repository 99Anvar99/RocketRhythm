#pragma once
#include <memory>
#include <string>
#include <chrono>
#include <nlohmann/json_fwd.hpp>

#include "GuiBase.h"
#include "media.h"
#include "bakkesmod/plugin/bakkesmodplugin.h"
#include "IMGUI/imgui.h"
#include "bakkesmod/wrappers/wrapperstructs.h"
#include "bakkesmod/wrappers/ImageWrapper.h"

inline constexpr int kPluginConfigVersion = 10;

class RocketRhythm : public BakkesMod::Plugin::BakkesModPlugin, public SettingsWindowBase, public PluginWindowBase
{
public:
    RocketRhythm();
    ~RocketRhythm() override;

    void onLoad() override;
    void onUnload() override;

    void RenderSettings() override;
    std::string GetPluginName() override;
    void RenderWindow() override;

    void RenderCanvas(const CanvasWrapper& canvas);

private:
    // ---------------------------
    // Configurable style/settings
    // ---------------------------
    struct WindowStyle
    {
        ImVec4 backgroundColor = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);
        ImVec4 accentColor     = ImVec4(0.0f, 0.9884f, 1.0f, 1.0f);
        ImVec4 accentColor2    = ImVec4(0.2f, 0.68f, 1.0f, 1.0f);
        ImVec4 textColor       = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
        ImVec4 textColorDim    = ImVec4(0.75f, 0.75f, 0.75f, 1.0f);
        ImVec4 textColorFaint  = ImVec4(0.55f, 0.55f, 0.55f, 1.0f);

        float windowRounding      = 5.0f;
        float albumArtRounding    = 14.0f;
        float progressBarHeight   = 8.0f;
        float progressBarRounding = 4.0f;
        float albumArtSize        = 118.0f;

        bool  enablePulse      = true;
        bool  showAlbumArt     = true;
        bool  showProgressBar  = true;
        bool  showAlbumInfo    = true;
        float windowOpacity    = 1.0f;

        // Scaling
        float uiScale           = 1.0f;
        bool  enableAutoScaling = false;
        float minScale          = 0.8f;
        float maxScale          = 2.0f;

        // Marquee
        bool  enableMarquee     = true;
        float marqueeSpeedPx    = 40.0f;
        float marqueeWaitSec    = 0.80f;

        enum class TimeDisplayMode : int
        {
            CenterSlash = 0,    // "0:10 / 3:45" centered
            Corners = 1         // "0:10" left, "3:45" right
        };

        TimeDisplayMode timeDisplayMode = TimeDisplayMode::Corners;
    };

    static WindowStyle DefaultWindowStyle() { return WindowStyle{}; }

    // ---------------------------
    // Runtime state
    // ---------------------------
    std::shared_ptr<bool>  mEnabled;
    std::shared_ptr<float> mUiScaleCvar;

    bool mHideWhenNotPlaying = false;
    bool mNeedsWindowOpen    = false;
    bool mNeedsWindowClose   = false;
    bool mIsNotPlaying       = false;

    mutable std::string mCachedMenuName;
    mutable std::string mCachedPluginName;
    bool mMenuNameCached   = false;
    bool mPluginNameCached = false;

    std::unique_ptr<MediaController> mMedia;
    MediaState mMediaState;

    bool   mFontsInitialized = false;
    ImFont* mFontOverlay     = nullptr;
    ImFont* mFontSettings    = nullptr;

    float mPulsePhase = 0.0f;

    std::chrono::steady_clock::time_point mLastProgressAnchor;
    int mLastPositionSec      = 0;
    int mAnchoredPositionSec  = 0;
    std::string mLastTrackKey;

    std::shared_ptr<ImageWrapper> mAlbumArtTexture;
    bool mAlbumArtLoaded = false;
    std::string mAlbumArtPath;

    ImVector<ImWchar> mMergedGlyphRanges;

    WindowStyle mWindowStyle{};

    // ---------------------------
    // Helpers / rendering
    // ---------------------------
    const std::string& GetMenuNameCached();
    const std::string& GetPluginNameCached();

    void DrawHelpMarker(const char* desc);

    bool ShouldShowWindow() const;
    void UpdateWindowState();

    void InitializeFonts();
    void LoadAlbumArt(const std::string& path);

    void UpdateAnimation(float deltaTime);

    int  GetCurrentDisplayPositionSec();

    float GetDpiScaleFactor();
    float CalculateAutoScaleFactor();
    float GetEffectiveScaleFactor();
    float GetScaledValue(float baseValue);

    void DrawNoMusicState();
    void DrawAlbumArtPlaceholder(float scale);
    void DrawAlbumArt(float scale);

    void DrawMusicStateCompact();
    void DrawProgressBar();

    // Persistence
    void SaveConfig();
    void LoadConfig();

    friend void to_json(nlohmann::json& j, const WindowStyle& s);
    friend void from_json(const nlohmann::json& j, WindowStyle& s);
};