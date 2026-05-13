#pragma once
#include <cstdint>
#include <string>
#include <functional>
#include <memory>
#include <atomic>
#include <thread>
#include <mutex>
#include <optional>

#include <nlohmann/json_fwd.hpp>

namespace rrspotify {

struct SpotifyTrackInfo
{
    std::string id;
    std::string title;
    std::string artist;
    std::string album;
    std::string albumArtUrl;
    int durationMs = 0;
    int positionMs = 0;
    bool isPlaying = false;
    bool isLiked = false;
    int popularity = 0;
    float volumePercent = 100.0f;
};

struct SpotifyAuthConfig
{
    std::string clientId;
    std::string clientSecret;
    std::string accessToken;
    std::string refreshToken;
    int64_t tokenExpiry = 0;

    bool IsValid() const { return !accessToken.empty() && tokenExpiry > 0; }
    bool IsExpired() const;
};

class SpotifyIntegration
{
public:
    using TrackChangedCallback = std::function<void(const SpotifyTrackInfo&)>;
    using AuthStateCallback = std::function<void(bool authenticated)>;

    SpotifyIntegration();
    ~SpotifyIntegration();

    // Configuration
    void SetAuthConfig(const SpotifyAuthConfig& config);
    SpotifyAuthConfig GetAuthConfig() const;
    bool IsAuthenticated() const;
    bool IsAuthFlowActive() const;
    std::string GetLastAuthError() const;

    // Authorization flow
    void StartAuthFlow();
    void Logout();
    std::string GetAuthUrl() const;

    // Playback control
    void Play();
    void Pause();
    void Next();
    void Previous();
    void SetVolume(int percent);
    void Seek(int positionMs);
    void ToggleLike();

    // Current state
    SpotifyTrackInfo GetCurrentTrack() const;
    void RefreshPlaybackState();

    // Callbacks
    void SetOnTrackChanged(TrackChangedCallback callback);
    void SetOnAuthStateChanged(AuthStateCallback callback);

    // Polling thread control
    void StartPolling();
    void StopPolling();

    // Exchange auth code for tokens
    bool ExchangeCodeForTokens(const std::string& code, std::string& error);

private:
    class Impl;
    std::unique_ptr<Impl> pImpl;
};

// Helper functions for config serialization
void to_json(nlohmann::json& j, const SpotifyAuthConfig& c);
void from_json(const nlohmann::json& j, SpotifyAuthConfig& c);

} // namespace rrspotify
