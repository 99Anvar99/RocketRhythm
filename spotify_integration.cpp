#include "pch.h"
#include "spotify_integration.h"

#include <nlohmann/json.hpp>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <Windows.h>
#include <bcrypt.h>
#include <winhttp.h>
#include <shellapi.h>

#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "ws2_32.lib")

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <iomanip>
#include <random>
#include <sstream>
#include <vector>

namespace rrspotify {

// Static constants
static constexpr auto kSpotifyAuthUrl = "https://accounts.spotify.com/authorize";
static constexpr auto kSpotifyTokenUrl = "https://accounts.spotify.com/api/token";
static constexpr auto kSpotifyApiBase = "https://api.spotify.com/v1";
static constexpr auto kRedirectUri = "http://127.0.0.1:9982/callback";
static constexpr int kAuthCallbackPort = 9982;
static constexpr int kAuthCallbackTimeoutSeconds = 180;

// Helper: Base64 encode
static std::string Base64EncodeBytes(const unsigned char* data, size_t size)
{
    static const char* chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string result;
    int val = 0, valb = -6;
    for (size_t i = 0; i < size; ++i)
    {
        const unsigned char c = data[i];
        val = (val << 8) + c;
        valb += 8;
        while (valb >= 0)
        {
            result.push_back(chars[(val >> valb) & 0x3F]);
            valb -= 6;
        }
    }
    if (valb > -6) result.push_back(chars[((val << 8) >> (valb + 8)) & 0x3F]);
    while (result.size() % 4) result.push_back('=');
    return result;
}

static std::string Base64Encode(const std::string& input)
{
    return Base64EncodeBytes(reinterpret_cast<const unsigned char*>(input.data()), input.size());
}

static std::string Base64UrlEncode(const std::vector<unsigned char>& input)
{
    std::string result = Base64EncodeBytes(input.data(), input.size());
    for (char& c : result)
    {
        if (c == '+') c = '-';
        else if (c == '/') c = '_';
    }

    while (!result.empty() && result.back() == '=')
        result.pop_back();

    return result;
}

static std::string GenerateRandomString(size_t length)
{
    constexpr char charset[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-._~";
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, sizeof(charset) - 2);

    std::string result;
    result.reserve(length);
    for (size_t i = 0; i < length; ++i)
        result.push_back(charset[dis(gen)]);
    return result;
}

static std::string GeneratePKCEVerifier()
{
    return GenerateRandomString(96);
}

static std::vector<unsigned char> Sha256(const std::string& input)
{
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    std::vector<unsigned char> hashObject;
    std::vector<unsigned char> digest;

    auto cleanup = [&]()
    {
        if (hash) BCryptDestroyHash(hash);
        if (alg) BCryptCloseAlgorithmProvider(alg, 0);
    };

    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
        return {};

    DWORD objectSize = 0;
    DWORD hashSize = 0;
    DWORD cbData = 0;

    if (BCryptGetProperty(alg, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objectSize),
                          sizeof(objectSize), &cbData, 0) < 0 ||
        BCryptGetProperty(alg, BCRYPT_HASH_LENGTH, reinterpret_cast<PUCHAR>(&hashSize),
                          sizeof(hashSize), &cbData, 0) < 0)
    {
        cleanup();
        return {};
    }

    hashObject.resize(objectSize);
    digest.resize(hashSize);

    if (BCryptCreateHash(alg, &hash, hashObject.data(), objectSize, nullptr, 0, 0) < 0 ||
        BCryptHashData(hash, reinterpret_cast<PUCHAR>(const_cast<char*>(input.data())),
                       static_cast<ULONG>(input.size()), 0) < 0 ||
        BCryptFinishHash(hash, digest.data(), hashSize, 0) < 0)
    {
        cleanup();
        return {};
    }

    cleanup();
    return digest;
}

static std::string UrlEncode(const std::string& input)
{
    std::ostringstream escaped;
    escaped << std::uppercase << std::hex << std::setfill('0');

    for (unsigned char c : input)
    {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
        {
            escaped << static_cast<char>(c);
        }
        else
        {
            escaped << '%' << std::setw(2) << static_cast<int>(c);
        }
    }

    return escaped.str();
}

static int HexValue(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static std::string UrlDecode(const std::string& input)
{
    std::string output;
    output.reserve(input.size());

    for (size_t i = 0; i < input.size(); ++i)
    {
        const char c = input[i];
        if (c == '+' )
        {
            output.push_back(' ');
        }
        else if (c == '%' && i + 2 < input.size())
        {
            const int hi = HexValue(input[i + 1]);
            const int lo = HexValue(input[i + 2]);
            if (hi >= 0 && lo >= 0)
            {
                output.push_back(static_cast<char>((hi << 4) | lo));
                i += 2;
            }
            else
            {
                output.push_back(c);
            }
        }
        else
        {
            output.push_back(c);
        }
    }

    return output;
}

static std::string GetQueryParam(const std::string& query, const std::string& name)
{
    size_t start = 0;
    while (start <= query.size())
    {
        const size_t end = query.find('&', start);
        const std::string part = query.substr(start, end == std::string::npos ? std::string::npos : end - start);
        const size_t eq = part.find('=');
        const std::string key = UrlDecode(part.substr(0, eq));
        if (key == name)
        {
            return eq == std::string::npos ? std::string{} : UrlDecode(part.substr(eq + 1));
        }

        if (end == std::string::npos) break;
        start = end + 1;
    }

    return {};
}

static std::string BuildHttpResponse(const std::string& body)
{
    std::ostringstream response;
    response << "HTTP/1.1 200 OK\r\n"
             << "Content-Type: text/html; charset=utf-8\r\n"
             << "Content-Length: " << body.size() << "\r\n"
             << "Connection: close\r\n\r\n"
             << body;
    return response.str();
}

bool SpotifyAuthConfig::IsExpired() const
{
    using namespace std::chrono;
    const auto now = duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
    return now >= tokenExpiry - 60; // Refresh 1 minute before expiry
}

void to_json(nlohmann::json& j, const SpotifyAuthConfig& c)
{
    j = nlohmann::json{
        {"client_id", c.clientId},
        {"client_secret", c.clientSecret},
        {"access_token", c.accessToken},
        {"refresh_token", c.refreshToken},
        {"token_expiry", c.tokenExpiry}
    };
}

void from_json(const nlohmann::json& j, SpotifyAuthConfig& c)
{
    c.clientId = j.value("client_id", "");
    c.clientSecret = j.value("client_secret", "");
    c.accessToken = j.value("access_token", "");
    c.refreshToken = j.value("refresh_token", "");
    c.tokenExpiry = j.value("token_expiry", 0);
}

// Implementation class
class SpotifyIntegration::Impl
{
public:
    Impl() = default;
    ~Impl()
    {
        StopPolling();
        StopAuthCallbackServer();
    }

    void SetAuthConfig(const SpotifyAuthConfig& config)
    {
        std::scoped_lock lock(authMutex_);
        authConfig_ = config;
    }

    SpotifyAuthConfig GetAuthConfig() const
    {
        std::scoped_lock lock(authMutex_);
        return authConfig_;
    }

    bool IsAuthenticated() const
    {
        std::scoped_lock lock(authMutex_);
        return authConfig_.IsValid() && !authConfig_.IsExpired();
    }

    bool IsAuthFlowActive() const
    {
        return authCallbackRunning_.load();
    }

    std::string GetLastAuthError() const
    {
        std::scoped_lock lock(statusMutex_);
        return lastAuthError_;
    }

    void StartAuthFlow()
    {
        {
            std::scoped_lock lock(authMutex_);
            if (authConfig_.clientId.empty())
            {
                SetLastAuthError("Spotify Client ID is required.");
                return;
            }
        }

        SetLastAuthError({});
        StartAuthCallbackServer();

        std::string url = GetAuthUrl();
        ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }

    void Logout()
    {
        StopPolling();
        StopAuthCallbackServer();

        {
            std::scoped_lock lock(authMutex_);
            authConfig_.accessToken.clear();
            authConfig_.refreshToken.clear();
            authConfig_.tokenExpiry = 0;
        }

        {
            std::scoped_lock trackLock(trackMutex_);
            currentTrack_ = SpotifyTrackInfo{};
        }

        NotifyAuthStateChanged(false);
    }

    std::string GetAuthUrl() const
    {
        std::scoped_lock lock(authMutex_);
        pkceVerifier_ = GeneratePKCEVerifier();
        authState_ = GenerateRandomString(32);
        const std::string codeChallenge = Base64UrlEncode(Sha256(pkceVerifier_));
        const std::string scope =
            "user-read-playback-state user-modify-playback-state user-read-currently-playing "
            "user-library-read user-library-modify";

        std::ostringstream oss;
        oss << kSpotifyAuthUrl
            << "?client_id=" << UrlEncode(authConfig_.clientId)
            << "&response_type=code"
            << "&redirect_uri=" << UrlEncode(kRedirectUri)
            << "&scope=" << UrlEncode(scope)
            << "&state=" << UrlEncode(authState_);

        if (!codeChallenge.empty())
        {
            oss << "&code_challenge_method=S256"
                << "&code_challenge=" << UrlEncode(codeChallenge);
        }

        return oss.str();
    }

    // HTTP request helper using WinHTTP
    std::string HttpRequest(const std::string& url, const std::string& method, const std::string& body = "", const std::string& authHeader = "")
    {
        URL_COMPONENTS urlComp = {};
        urlComp.dwStructSize = sizeof(urlComp);

        WCHAR szHostName[256] = {};
        WCHAR szUrlPath[1024] = {};
        WCHAR szExtraInfo[1024] = {};
        urlComp.lpszHostName = szHostName;
        urlComp.dwHostNameLength = ARRAYSIZE(szHostName);
        urlComp.lpszUrlPath = szUrlPath;
        urlComp.dwUrlPathLength = ARRAYSIZE(szUrlPath);
        urlComp.lpszExtraInfo = szExtraInfo;
        urlComp.dwExtraInfoLength = ARRAYSIZE(szExtraInfo);

        std::wstring wurl(url.begin(), url.end());
        if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &urlComp))
            return "";

        HINTERNET hSession = WinHttpOpen(L"RocketRhythm/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                       WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!hSession) return "";

        HINTERNET hConnect = WinHttpConnect(hSession, szHostName, urlComp.nPort, 0);
        if (!hConnect)
        {
            WinHttpCloseHandle(hSession);
            return "";
        }

        DWORD flags = (urlComp.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
        std::wstring requestTarget = szUrlPath;
        requestTarget += szExtraInfo;
        std::wstring wmethod(method.begin(), method.end());

        HINTERNET hRequest = WinHttpOpenRequest(hConnect, wmethod.c_str(), requestTarget.c_str(),
                                               nullptr, WINHTTP_NO_REFERER,
                                               WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
        if (!hRequest)
        {
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            return "";
        }

        // Add headers
        std::wstring headers = L"Accept: application/json\r\n";
        if (!body.empty())
        {
            headers += L"Content-Type: application/x-www-form-urlencoded\r\n";
        }
        if (!authHeader.empty())
        {
            headers += std::wstring(authHeader.begin(), authHeader.end()) + L"\r\n";
        }

        BOOL result = WinHttpSendRequest(hRequest, headers.c_str(), static_cast<DWORD>(headers.length()),
                                        body.empty() ? WINHTTP_NO_REQUEST_DATA : const_cast<char*>(body.c_str()),
                                        static_cast<DWORD>(body.length()),
                                        static_cast<DWORD>(body.length()), 0);

        if (!result)
        {
            WinHttpCloseHandle(hRequest);
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            return "";
        }

        if (!WinHttpReceiveResponse(hRequest, nullptr))
        {
            WinHttpCloseHandle(hRequest);
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            return "";
        }

        std::string response;
        DWORD dwSize = 0;
        do
        {
            dwSize = 0;
            WinHttpQueryDataAvailable(hRequest, &dwSize);
            if (dwSize == 0) break;

            std::vector<char> buffer(dwSize + 1);
            DWORD dwDownloaded = 0;
            WinHttpReadData(hRequest, buffer.data(), dwSize, &dwDownloaded);
            response.append(buffer.data(), dwDownloaded);
        } while (dwSize > 0);

        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);

        return response;
    }

    bool ExchangeCodeForTokens(const std::string& code, std::string& error)
    {
        SpotifyAuthConfig config;
        std::string verifier;
        {
            std::scoped_lock lock(authMutex_);
            config = authConfig_;
            verifier = pkceVerifier_;
        }

        if (config.clientId.empty())
        {
            error = "Spotify Client ID is required.";
            SetLastAuthError(error);
            return false;
        }

        std::ostringstream body;
        body << "grant_type=authorization_code"
             << "&code=" << UrlEncode(code)
             << "&redirect_uri=" << UrlEncode(kRedirectUri);

        if (config.clientSecret.empty())
        {
            body << "&client_id=" << UrlEncode(config.clientId);
        }

        if (!verifier.empty())
        {
            body << "&code_verifier=" << UrlEncode(verifier);
        }

        std::string auth;
        if (!config.clientSecret.empty())
        {
            std::string credentials = config.clientId + ":" + config.clientSecret;
            auth = "Authorization: Basic " + Base64Encode(credentials);
        }

        std::string response = HttpRequest(kSpotifyTokenUrl, "POST", body.str(), auth);
        if (response.empty())
        {
            error = "No response from Spotify token endpoint.";
            SetLastAuthError(error);
            NotifyAuthStateChanged(false);
            return false;
        }

        try
        {
            auto json = nlohmann::json::parse(response);
            if (json.contains("error"))
            {
                error = json.value("error_description", "Unknown error");
                SetLastAuthError(error);
                NotifyAuthStateChanged(false);
                return false;
            }

            config.accessToken = json.value("access_token", "");
            config.refreshToken = json.value("refresh_token", "");
            int expiresIn = json.value("expires_in", 0);

            using namespace std::chrono;
            config.tokenExpiry = duration_cast<seconds>(system_clock::now().time_since_epoch()).count() + expiresIn;

            {
                std::scoped_lock lock(authMutex_);
                authConfig_ = config;
            }

            const bool ok = !config.accessToken.empty();
            SetLastAuthError(ok ? std::string{} : "Spotify did not return an access token.");
            NotifyAuthStateChanged(ok);
            return ok;
        }
        catch (const std::exception& e)
        {
            error = e.what();
            SetLastAuthError(error);
            NotifyAuthStateChanged(false);
            return false;
        }
    }

    bool RefreshAccessToken()
    {
        SpotifyAuthConfig config;
        {
            std::scoped_lock lock(authMutex_);
            config = authConfig_;
        }

        if (config.refreshToken.empty()) return false;

        std::ostringstream body;
        body << "grant_type=refresh_token"
             << "&refresh_token=" << UrlEncode(config.refreshToken);

        if (config.clientSecret.empty())
        {
            body << "&client_id=" << UrlEncode(config.clientId);
        }

        std::string auth;
        if (!config.clientSecret.empty())
        {
            std::string credentials = config.clientId + ":" + config.clientSecret;
            auth = "Authorization: Basic " + Base64Encode(credentials);
        }

        std::string response = HttpRequest(kSpotifyTokenUrl, "POST", body.str(), auth);
        if (response.empty()) return false;

        try
        {
            auto json = nlohmann::json::parse(response);
            if (json.contains("error"))
            {
                SetLastAuthError(json.value("error_description", "Spotify token refresh failed."));
                NotifyAuthStateChanged(false);
                return false;
            }

            config.accessToken = json.value("access_token", "");
            int expiresIn = json.value("expires_in", 0);

            using namespace std::chrono;
            config.tokenExpiry = duration_cast<seconds>(system_clock::now().time_since_epoch()).count() + expiresIn;

            // Update refresh token if provided
            if (json.contains("refresh_token"))
            {
                config.refreshToken = json.value("refresh_token", "");
            }

            {
                std::scoped_lock lock(authMutex_);
                authConfig_ = config;
            }

            const bool ok = !config.accessToken.empty();
            NotifyAuthStateChanged(ok);
            return ok;
        }
        catch (...)
        {
            NotifyAuthStateChanged(false);
            return false;
        }
    }

    std::string MakeApiRequest(const std::string& endpoint, const std::string& method = "GET",
                               const std::string& body = "")
    {
        if (!IsAuthenticated())
        {
            if (!RefreshAccessToken()) return "";
        }

        std::string token;
        {
            std::scoped_lock lock(authMutex_);
            token = authConfig_.accessToken;
        }

        std::string url = std::string(kSpotifyApiBase) + endpoint;
        std::string auth = "Bearer " + token;

        return HttpRequest(url, method, body, "Authorization: " + auth);
    }

    void RefreshPlaybackState()
    {
        std::string response = MakeApiRequest("/me/player");
        if (response.empty()) return;

        try
        {
            auto json = nlohmann::json::parse(response);

            SpotifyTrackInfo info;
            info.isPlaying = json.value("is_playing", false);
            info.positionMs = json.value("progress_ms", 0);

            if (json.contains("item") && !json["item"].is_null())
            {
                auto item = json["item"];
                info.id = item.value("id", "");
                info.title = item.value("name", "");
                info.durationMs = item.value("duration_ms", 0);
                info.popularity = item.value("popularity", 0);

                if (item.contains("album") && !item["album"].is_null())
                {
                    auto album = item["album"];
                    info.album = album.value("name", "");

                    if (album.contains("images") && !album["images"].is_null() && !album["images"].empty())
                    {
                        info.albumArtUrl = album["images"][0].value("url", "");
                    }
                }

                if (item.contains("artists") && !item["artists"].is_null() && !item["artists"].empty())
                {
                    std::ostringstream artists;
                    bool firstArtist = true;
                    for (const auto& artist : item["artists"])
                    {
                        const std::string name = artist.value("name", "");
                        if (name.empty())
                            continue;

                        if (!firstArtist)
                            artists << ", ";

                        artists << name;
                        firstArtist = false;
                    }

                    info.artist = artists.str();
                }
            }

            if (json.contains("device") && !json["device"].is_null())
            {
                info.volumePercent = json["device"].value("volume_percent", 100);
            }

            // Check if liked
            if (!info.id.empty())
            {
                std::string likedResponse = MakeApiRequest("/me/tracks/contains?ids=" + info.id);
                if (!likedResponse.empty())
                {
                    try
                    {
                        auto likedJson = nlohmann::json::parse(likedResponse);
                        if (likedJson.is_array() && !likedJson.empty())
                        {
                            info.isLiked = likedJson[0].get<bool>();
                        }
                    }
                    catch (...) {}
                }
            }

            {
                std::scoped_lock lock(trackMutex_);
                currentTrack_ = info;
            }

            if (onTrackChanged_)
            {
                onTrackChanged_(info);
            }
        }
        catch (const std::exception&) {}
    }

    void Play() { MakeApiRequest("/me/player/play", "PUT"); }
    void Pause() { MakeApiRequest("/me/player/pause", "PUT"); }
    void Next() { MakeApiRequest("/me/player/next", "POST"); }
    void Previous() { MakeApiRequest("/me/player/previous", "POST"); }

    void SetVolume(int percent)
    {
        percent = std::clamp(percent, 0, 100);
        MakeApiRequest("/me/player/volume?volume_percent=" + std::to_string(percent), "PUT");
    }

    void Seek(int positionMs)
    {
        MakeApiRequest("/me/player/seek?position_ms=" + std::to_string(positionMs), "PUT");
    }

    void ToggleLike()
    {
        std::string trackId;
        bool currentlyLiked = false;
        {
            std::scoped_lock lock(trackMutex_);
            trackId = currentTrack_.id;
            currentlyLiked = currentTrack_.isLiked;
        }

        if (trackId.empty()) return;

        if (currentlyLiked)
        {
            MakeApiRequest("/me/tracks?ids=" + trackId, "DELETE");
        }
        else
        {
            MakeApiRequest("/me/tracks?ids=" + trackId, "PUT");
        }

        RefreshPlaybackState();
    }

    SpotifyTrackInfo GetCurrentTrack() const
    {
        std::scoped_lock lock(trackMutex_);
        return currentTrack_;
    }

    void StartPolling()
    {
        if (polling_.exchange(true)) return;

        pollThread_ = std::thread([this]()
        {
            while (polling_.load())
            {
                RefreshPlaybackState();
                std::this_thread::sleep_for(std::chrono::seconds(3));
            }
        });
    }

    void StopPolling()
    {
        polling_.store(false);
        if (pollThread_.joinable())
        {
            pollThread_.join();
        }
    }

    void SetOnTrackChanged(TrackChangedCallback cb) { onTrackChanged_ = std::move(cb); }
    void SetOnAuthStateChanged(AuthStateCallback cb) { onAuthStateChanged_ = std::move(cb); }

private:
    void SetLastAuthError(const std::string& error)
    {
        std::scoped_lock lock(statusMutex_);
        lastAuthError_ = error;
    }

    void NotifyAuthStateChanged(bool authenticated)
    {
        if (onAuthStateChanged_)
        {
            onAuthStateChanged_(authenticated);
        }
    }

    void StartAuthCallbackServer()
    {
        if (authCallbackRunning_.exchange(true))
            return;

        if (authCallbackThread_.joinable())
        {
            authCallbackThread_.join();
        }

        authCallbackThread_ = std::thread([this]()
        {
            AuthCallbackLoop();
        });
    }

    void StopAuthCallbackServer()
    {
        authCallbackRunning_.store(false);
        if (authCallbackThread_.joinable())
        {
            authCallbackThread_.join();
        }
    }

    std::string HandleAuthCallbackRequest(const std::string& request)
    {
        std::istringstream stream(request);
        std::string method;
        std::string target;
        std::string version;
        stream >> method >> target >> version;

        auto failure = [&](const std::string& message)
        {
            SetLastAuthError(message);
            NotifyAuthStateChanged(false);
            return BuildHttpResponse("<html><body><h2>Spotify authorization failed</h2><p>" +
                                     message + "</p></body></html>");
        };

        if (method != "GET" || target.empty())
        {
            return failure("Invalid callback request.");
        }

        const size_t queryPos = target.find('?');
        const std::string path = queryPos == std::string::npos ? target : target.substr(0, queryPos);
        const std::string query = queryPos == std::string::npos ? std::string{} : target.substr(queryPos + 1);

        if (path != "/callback")
        {
            return failure("Unexpected callback path.");
        }

        const std::string spotifyError = GetQueryParam(query, "error");
        if (!spotifyError.empty())
        {
            return failure("Spotify returned error: " + spotifyError);
        }

        const std::string state = GetQueryParam(query, "state");
        std::string expectedState;
        {
            std::scoped_lock lock(authMutex_);
            expectedState = authState_;
        }

        if (state.empty() || state != expectedState)
        {
            return failure("Spotify authorization state did not match.");
        }

        const std::string code = GetQueryParam(query, "code");
        if (code.empty())
        {
            return failure("Spotify did not return an authorization code.");
        }

        std::string tokenError;
        if (!ExchangeCodeForTokens(code, tokenError))
        {
            return failure(tokenError.empty() ? "Could not exchange Spotify authorization code." : tokenError);
        }

        SetLastAuthError({});
        StartPolling();

        return BuildHttpResponse(
            "<html><body><h2>Spotify connected</h2><p>You can close this tab and return to Rocket League.</p></body></html>");
    }

    void AuthCallbackLoop()
    {
        WSADATA wsaData{};
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
        {
            SetLastAuthError("Could not initialize Windows sockets for Spotify callback.");
            authCallbackRunning_.store(false);
            return;
        }

        SOCKET listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (listenSocket == INVALID_SOCKET)
        {
            SetLastAuthError("Could not create Spotify callback socket.");
            authCallbackRunning_.store(false);
            WSACleanup();
            return;
        }

        const BOOL reuseAddress = TRUE;
        setsockopt(listenSocket, SOL_SOCKET, SO_REUSEADDR,
                   reinterpret_cast<const char*>(&reuseAddress), sizeof(reuseAddress));

        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(kAuthCallbackPort);
        inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);

        if (bind(listenSocket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR ||
            listen(listenSocket, 1) == SOCKET_ERROR)
        {
            SetLastAuthError("Could not listen on http://127.0.0.1:9982/callback. Another app may be using port 9982.");
            closesocket(listenSocket);
            authCallbackRunning_.store(false);
            WSACleanup();
            return;
        }

        const auto deadline = std::chrono::steady_clock::now() +
            std::chrono::seconds(kAuthCallbackTimeoutSeconds);

        bool handledRequest = false;
        while (authCallbackRunning_.load() && std::chrono::steady_clock::now() < deadline)
        {
            fd_set readSet;
            FD_ZERO(&readSet);
            FD_SET(listenSocket, &readSet);

            timeval timeout{};
            timeout.tv_sec = 1;

            const int ready = select(0, &readSet, nullptr, nullptr, &timeout);
            if (ready <= 0)
            {
                continue;
            }

            SOCKET clientSocket = accept(listenSocket, nullptr, nullptr);
            if (clientSocket == INVALID_SOCKET)
            {
                continue;
            }

            std::string request;
            std::array<char, 2048> buffer{};
            int received = 0;
            do
            {
                received = recv(clientSocket, buffer.data(), static_cast<int>(buffer.size()), 0);
                if (received > 0)
                {
                    request.append(buffer.data(), received);
                }
            } while (received > 0 &&
                     request.find("\r\n\r\n") == std::string::npos &&
                     request.size() < 16384);

            const std::string response = HandleAuthCallbackRequest(request);
            send(clientSocket, response.c_str(), static_cast<int>(response.size()), 0);
            closesocket(clientSocket);
            handledRequest = true;
            break;
        }

        if (!handledRequest && authCallbackRunning_.load())
        {
            SetLastAuthError("Spotify authorization timed out.");
            NotifyAuthStateChanged(false);
        }

        closesocket(listenSocket);
        authCallbackRunning_.store(false);
        WSACleanup();
    }

    mutable std::mutex authMutex_;
    SpotifyAuthConfig authConfig_;
    mutable std::string pkceVerifier_;
    mutable std::string authState_;

    mutable std::mutex statusMutex_;
    std::string lastAuthError_;

    mutable std::mutex trackMutex_;
    SpotifyTrackInfo currentTrack_;

    std::atomic<bool> polling_{false};
    std::thread pollThread_;
    std::atomic<bool> authCallbackRunning_{false};
    std::thread authCallbackThread_;

    TrackChangedCallback onTrackChanged_;
    AuthStateCallback onAuthStateChanged_;
};

// Public interface implementation
SpotifyIntegration::SpotifyIntegration() : pImpl(std::make_unique<Impl>()) {}
SpotifyIntegration::~SpotifyIntegration() = default;

void SpotifyIntegration::SetAuthConfig(const SpotifyAuthConfig& config) { pImpl->SetAuthConfig(config); }
SpotifyAuthConfig SpotifyIntegration::GetAuthConfig() const { return pImpl->GetAuthConfig(); }
bool SpotifyIntegration::IsAuthenticated() const { return pImpl->IsAuthenticated(); }
bool SpotifyIntegration::IsAuthFlowActive() const { return pImpl->IsAuthFlowActive(); }
std::string SpotifyIntegration::GetLastAuthError() const { return pImpl->GetLastAuthError(); }

void SpotifyIntegration::StartAuthFlow() { pImpl->StartAuthFlow(); }
void SpotifyIntegration::Logout() { pImpl->Logout(); }
std::string SpotifyIntegration::GetAuthUrl() const { return pImpl->GetAuthUrl(); }

void SpotifyIntegration::Play() { pImpl->Play(); }
void SpotifyIntegration::Pause() { pImpl->Pause(); }
void SpotifyIntegration::Next() { pImpl->Next(); }
void SpotifyIntegration::Previous() { pImpl->Previous(); }
void SpotifyIntegration::SetVolume(int percent) { pImpl->SetVolume(percent); }
void SpotifyIntegration::Seek(int positionMs) { pImpl->Seek(positionMs); }
void SpotifyIntegration::ToggleLike() { pImpl->ToggleLike(); }

SpotifyTrackInfo SpotifyIntegration::GetCurrentTrack() const { return pImpl->GetCurrentTrack(); }
void SpotifyIntegration::RefreshPlaybackState() { pImpl->RefreshPlaybackState(); }

void SpotifyIntegration::StartPolling() { pImpl->StartPolling(); }
void SpotifyIntegration::StopPolling() { pImpl->StopPolling(); }

void SpotifyIntegration::SetOnTrackChanged(TrackChangedCallback callback) { pImpl->SetOnTrackChanged(std::move(callback)); }
void SpotifyIntegration::SetOnAuthStateChanged(AuthStateCallback callback) { pImpl->SetOnAuthStateChanged(std::move(callback)); }

bool SpotifyIntegration::ExchangeCodeForTokens(const std::string& code, std::string& error)
{
    return pImpl->ExchangeCodeForTokens(code, error);
}

} // namespace rrspotify
