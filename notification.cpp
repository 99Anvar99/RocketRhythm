#include "pch.h"
#include "notification.h"

using Clock = std::chrono::steady_clock;
using Ms    = std::chrono::milliseconds;

std::mutex ImGui::g_notification_mutex;
std::vector<ImGuiToast> ImGui::notifications;

static float ToMs(const Clock::duration& d)
{
    return std::chrono::duration_cast<Ms>(d).count();
}

ImVec4 ImGuiToast::get_color() const
{
    switch (type)
    {
        case Success: return { 0.f, 1.f, 0.f, 1.f };
        case Warning: return { 1.f, 1.f, 0.f, 1.f };
        case Error:   return { 1.f, 0.f, 0.f, 1.f };
        case Info:    return { 0.f, 0.62f, 1.f, 1.f };
    }
    return { 1.f, 1.f, 1.f, 1.f };
}

Clock::duration ImGuiToast::elapsed() const
{
    return Clock::now() - creation_time;
}

NotifyPhase ImGuiToast::get_phase() const
{
    const float t = ToMs(elapsed());
    const float fade = NOTIFY_FADE_IN_OUT_TIME;
    const float total = fade + dismiss_time + fade;

    if (t >= total)                         return NotifyPhase::Expired;
    if (t >= fade + dismiss_time)           return NotifyPhase::FadeOut;
    if (t >= fade)                          return NotifyPhase::Wait;
    if (repeats && t < NOTIFY_FLASH_TIME)   return NotifyPhase::Flash;
    return NotifyPhase::FadeIn;
}

float ImGuiToast::get_opacity() const
{
    const float t = ToMs(elapsed());
    constexpr float fade = NOTIFY_FADE_IN_OUT_TIME;

    switch (get_phase())
    {
        case NotifyPhase::FadeIn:
            return t / fade * NOTIFY_OPACITY;

        case NotifyPhase::FadeOut:
            return (1.f - (t - fade - dismiss_time) / fade) * NOTIFY_OPACITY;

        default:
            return NOTIFY_OPACITY;
    }
}

namespace ImGui
{
    void insert_notification(ImGuiToast toast)
    {
        std::scoped_lock lock(g_notification_mutex);

        for (auto& n : notifications)
        {
            if (n.content == toast.content)
            {
                n.creation_time = Clock::now();
                n.repeats = std::min(n.repeats + 1, 999);
                return;
            }
        }

        notifications.emplace_back(std::move(toast));
    }

    void render_notifications()
    {
        std::scoped_lock lock(g_notification_mutex);

        const ImVec2 display = GetIO().DisplaySize;
        float y_offset = 0.f;

        for (size_t i = 0; i < notifications.size();)
        {
            auto& toast = notifications[i];
            const NotifyPhase phase = toast.get_phase();

            if (phase == NotifyPhase::Expired)
            {
                notifications.erase(notifications.begin() + i);
                continue;
            }

            const float opacity = toast.get_opacity();
            const ImVec4 color = toast.get_color();

            SetNextWindowBgAlpha(opacity);
            SetNextWindowSize({ display.x / 6.f, 0.f });
            SetNextWindowPos(
                { display.x - NOTIFY_PADDING_X, NOTIFY_PADDING_Y + y_offset },
                ImGuiCond_Always,
                { 1.f, 0.f }
            );

            if (Begin(std::format("##TOAST{}", i).c_str(), nullptr, notify_default_toast_flags))
            {
                PushTextWrapPos(GetWindowWidth());

                TextUnformatted(toast.content.c_str());

                if (toast.repeats > 0)
                {
                    SameLine();
                    Text("(x%d)", toast.repeats);
                }

                PopTextWrapPos();

                y_offset += GetWindowHeight() + NOTIFY_PADDING_MESSAGE_Y;

                const float elapsed = ToMs(toast.elapsed());
                float bar_width = GetWindowWidth();

                if (phase == NotifyPhase::Wait)
                {
                    bar_width *= 1.f - (elapsed - NOTIFY_FADE_IN_OUT_TIME) / toast.dismiss_time;
                }
                else if (phase == NotifyPhase::FadeOut)
                {
                    bar_width = 0.f;
                }

                const ImVec2 bar_pos = {
                    GetWindowPos().x,
                    GetWindowPos().y + GetWindowHeight() - 4.f
                };

                GetBackgroundDrawList()->AddRectFilled(
                    bar_pos,
                    { bar_pos.x + bar_width, bar_pos.y + 3.f },
                    ImColor(color)
                );
            }

            End();
            ++i;
        }
    }
}
