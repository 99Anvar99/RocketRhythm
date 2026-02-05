#pragma once

#include <imgui/imgui.h>

#include <chrono>
#include <mutex>
#include <string>
#include <vector>
#include <format>

// ==============================
// Timing
// ==============================

using NotifyClock = std::chrono::steady_clock;

// ==============================
// Layout constants
// ==============================

constexpr float NOTIFY_PADDING_X = 20.f;
constexpr float NOTIFY_PADDING_Y = 20.f;
constexpr float NOTIFY_PADDING_MESSAGE_Y = 10.f;

constexpr float NOTIFY_FADE_IN_OUT_TIME = 200.f;
constexpr float NOTIFY_FLASH_TIME = NOTIFY_FADE_IN_OUT_TIME + 50.f;
constexpr float NOTIFY_DEFAULT_DISMISS = 4000.f;
constexpr float NOTIFY_OPACITY = 0.8f;

// ==============================
// Window flags
// ==============================

constexpr ImGuiWindowFlags notify_default_toast_flags =
    ImGuiWindowFlags_AlwaysAutoResize |
    ImGuiWindowFlags_NoDecoration |
    ImGuiWindowFlags_NoNav |
    ImGuiWindowFlags_NoBringToFrontOnFocus |
    ImGuiWindowFlags_NoFocusOnAppearing;

// ==============================
// Types
// ==============================

enum ImGuiToastType : uint8_t
{
    Success,
    Warning,
    Error,
    Info,
};

enum class NotifyPhase : uint8_t
{
    FadeIn,
    Wait,
    Flash,
    FadeOut,
    Expired
};

// ==============================
// Toast
// ==============================

class ImGuiToast
{
public:
    ImGuiToastType type;
    std::string content;
    float dismiss_time;
    NotifyClock::time_point creation_time;
    uint16_t repeats{0};

    template <typename... Args>
    explicit ImGuiToast(
        ImGuiToastType t,
        float dismiss,
        std::string_view fmt,
        Args&&... args
    )
        : type(t)
        , content(std::vformat(fmt, std::make_format_args(args...)))
        , dismiss_time(dismiss)
        , creation_time(NotifyClock::now())
    {}

    ImVec4 get_color() const;
    NotifyClock::duration elapsed() const;
    NotifyPhase get_phase() const;
    float get_opacity() const;
};

// ==============================
// ImGui API
// ==============================

namespace ImGui
{
    extern std::mutex g_notification_mutex;
    extern std::vector<ImGuiToast> notifications;

    void insert_notification(ImGuiToast toast);
    void render_notifications();
}

// ==============================
// Helper API
// ==============================

template <typename... Args>
inline void notify(ImGuiToastType type, float duration, std::string_view fmt, Args&&... args)
{
    ImGui::insert_notification(
        ImGuiToast(type, duration, fmt, std::forward<Args>(args)...)
    );
}

template <typename... Args>
inline void notify(ImGuiToastType type, std::chrono::milliseconds duration, std::string_view fmt, Args&&... args)
{
    notify(type, static_cast<float>(duration.count()), fmt, std::forward<Args>(args)...);
}

template <typename... Args>
inline void notify(ImGuiToastType type, std::string_view fmt, Args&&... args)
{
    notify(type, NOTIFY_DEFAULT_DISMISS, fmt, std::forward<Args>(args)...);
}