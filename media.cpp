#include "pch.h"
#include "media.h"

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.Storage.Streams.h>

#include <filesystem>
#include <fstream>
#include <string_view>
#include <vector>
#include <mutex>
#include <thread>
#include <atomic>
#include <condition_variable>
#include <chrono>
#include <algorithm>

using namespace winrt;
using namespace Windows::Media::Control;
using namespace Windows::Storage::Streams;

namespace
{
	constexpr uint32_t kMaxAlbumArtBytes = 8u * 1024u * 1024u; // 8MB hard cap

	constexpr uint64_t Fnv1a64(std::string_view s) noexcept
	{
		uint64_t h = 14695981039346656037ull;
		for (unsigned char c : s)
		{
			h ^= static_cast<uint64_t>(c);
			h *= 1099511628211ull;
		}
		return h;
	}

	std::string HashTrackStable(const std::string& t, const std::string& a, const std::string& al)
	{
		const std::string combined = t + '\n' + a + '\n' + al;
		const uint64_t h = Fnv1a64(combined);
		char buf[17]{};
		snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(h));
		return std::string(buf);
	}

	int64_t HnsToSeconds(const Windows::Foundation::TimeSpan& ts) noexcept
	{
		return ts.count() / 10'000'000;
	}

	bool WriteFileBytes(const std::filesystem::path& path, const std::vector<uint8_t>& bytes) noexcept
	{
		try
		{
			std::ofstream out(path, std::ios::binary);
			if (!out.is_open()) return false;
			out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
			out.flush();
			return out.good();
		}
		catch (...)
		{
			return false;
		}
	}

	std::vector<uint8_t> ReadAllBytesFromIRandomAccessStream(
		const IRandomAccessStream& stream,
		uint32_t size)
	{
		DataReader reader(stream);
		reader.InputStreamOptions(InputStreamOptions::None);

		const uint32_t loaded = reader.LoadAsync(size).get();
		if (loaded != size) return {};

		std::vector<uint8_t> bytes(size);
		reader.ReadBytes(bytes);
		return bytes;
	}
}

class MediaControllerGSMTC final : public MediaController
{
public:
	explicit MediaControllerGSMTC(const std::string& dataDir)
	{
		try
		{
			cacheDir_ = std::filesystem::path(dataDir) / "RocketRhythm" / "album_cache";
			std::filesystem::create_directories(cacheDir_);
		}
		catch (...)
		{
			cacheDir_.clear();
		}

		worker_ = std::thread([this] { WorkerLoop(); });
	}

	~MediaControllerGSMTC() override
	{
		stop_.store(true, std::memory_order_relaxed);
		{
			std::lock_guard lk(cvMutex_);
			dirty_.store(true, std::memory_order_relaxed);
		}
		cv_.notify_all();
		if (worker_.joinable()) worker_.join();
	}

	// Game thread: cheap copy only
	void Update() override
	{
		std::lock_guard lk(stateMutex_);
		state_ = cachedState_;
	}

	const MediaState& GetState() const override
	{
		return state_;
	}

private:
	// -------------------------
	// Worker (WinRT + Events)
	// -------------------------
	void WorkerLoop()
	{
		try
		{
			init_apartment(apartment_type::multi_threaded);
		}
		catch (...)
		{
			// If apartment init fails, we'll still try; refresh may fail gracefully.
		}

		try
		{
			manager_ = GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get();
		}
		catch (...)
		{
			manager_ = nullptr;
		}

		if (manager_)
		{
			// Subscribe to current session changes
			mgrSessionChangedRevoker_ = manager_.CurrentSessionChanged(auto_revoke, [this](const auto&, const auto&)
			{
				QueueRefresh(RefreshReason::SessionChanged);
			});

			// Attach initial session
			AttachSession(manager_.GetCurrentSession());
			QueueRefresh(RefreshReason::Initial);
		}
		else
		{
			Publish(MediaState{});
		}

		while (!stop_.load(std::memory_order_relaxed))
		{
			// Wait until something changes
			{
				std::unique_lock lk(cvMutex_);
				cv_.wait(lk, [this]
				{
					return stop_.load(std::memory_order_relaxed) || dirty_.load(std::memory_order_relaxed);
				});

				dirty_.store(false, std::memory_order_relaxed);
			}

			if (stop_.load(std::memory_order_relaxed)) break;

			// Coalesce reasons (not strictly needed, but good hygiene)
			const uint32_t reasons = pendingReasons_.exchange(0, std::memory_order_relaxed);

			// If session changed, reattach before reading properties
			if (reasons & static_cast<uint32_t>(RefreshReason::SessionChanged))
			{
				if (manager_)
				{
					AttachSession(manager_.GetCurrentSession());
				}
			}

			RefreshOnce();
		}

		// Clean detach on exit
		DetachSession();
		manager_ = nullptr;
	}

	enum class RefreshReason : uint32_t
	{
		None = 0,
		Initial = 1u << 0,
		SessionChanged = 1u << 1,
		MediaChanged = 1u << 2,
		PlaybackChanged = 1u << 3,
		TimelineChanged = 1u << 4,
	};

	void QueueRefresh(RefreshReason r)
	{
		pendingReasons_.fetch_or(static_cast<uint32_t>(r), std::memory_order_relaxed);
		{
			std::lock_guard lk(cvMutex_);
			dirty_.store(true, std::memory_order_relaxed);
		}
		cv_.notify_one();
	}

	void AttachSession(const GlobalSystemMediaTransportControlsSession& newSession)
	{
		DetachSession();
		session_ = newSession;

		if (!session_) return;

		// Session event subscriptions
		mediaChangedRevoker_ = session_.MediaPropertiesChanged(auto_revoke, [this](const auto&, const auto&)
		{
			QueueRefresh(RefreshReason::MediaChanged);
		});

		playbackChangedRevoker_ = session_.PlaybackInfoChanged(auto_revoke, [this](const auto&, const auto&)
		{
			QueueRefresh(RefreshReason::PlaybackChanged);
		});

		timelineChangedRevoker_ = session_.TimelinePropertiesChanged(auto_revoke, [this](const auto&, const auto&)
		{
			QueueRefresh(RefreshReason::TimelineChanged);
		});
	}

	void DetachSession()
	{
		// Revokers auto-unsubscribe on destruction/reset; resetting session_ also helps.
		mediaChangedRevoker_ = {};
		playbackChangedRevoker_ = {};
		timelineChangedRevoker_ = {};
		session_ = nullptr;
	}

	void RefreshOnce()
	{
		MediaState local{};

		if (!manager_ || !session_)
		{
			Publish(local);
			return;
		}

		try
		{
			const auto playback = session_.GetPlaybackInfo();
			const auto timeline = session_.GetTimelineProperties();
			const auto media = session_.TryGetMediaPropertiesAsync().get();

			local.isPlaying = playback.PlaybackStatus() == GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing;

			local.title = to_string(media.Title());
			local.artist = to_string(media.Artist());
			local.album = to_string(media.AlbumTitle());

			const int64_t dur = std::max<int64_t>(0, HnsToSeconds(timeline.EndTime()));
			const int64_t pos = std::clamp<int64_t>(HnsToSeconds(timeline.Position()), 0, dur);

			local.durationSec = static_cast<int>(dur);
			local.positionSec = static_cast<int>(pos);
			local.progress01 = local.durationSec > 0 ? std::clamp(local.positionSec / static_cast<float>(local.durationSec), 0.0f, 1.0f) : 0.0f;

			// Album art: only on track change (or first load), and only if thumbnail exists
			const std::string songKey = local.title + "|" + local.artist + "|" + local.album;
			if (songKey != lastSongKey_)
			{
				lastSongKey_ = songKey;
				local.hasAlbumArt = false;
				local.albumArtPath.clear();

				TryCacheAlbumArt(media.Thumbnail(), local);
				lastAlbumArtPath_ = local.albumArtPath;
			}
			else
			{
				local.albumArtPath = lastAlbumArtPath_;
				local.hasAlbumArt = !lastAlbumArtPath_.empty();
			}

			Publish(local);
		}
		catch (...)
		{
			Publish(MediaState{});
		}
	}

	void TryCacheAlbumArt(const IRandomAccessStreamReference& thumbnail, MediaState& outState)
	{
		if (!thumbnail || cacheDir_.empty())
		{
			outState.hasAlbumArt = false;
			outState.albumArtPath.clear();
			return;
		}

		const auto hash = HashTrackStable(outState.title, outState.artist, outState.album);
		const auto path = cacheDir_ / (hash + ".png");
		outState.albumArtPath = path.string();

		try
		{
			if (std::filesystem::exists(path))
			{
				outState.hasAlbumArt = true;
				return;
			}
		}
		catch (...)
		{
			outState.hasAlbumArt = false;
			outState.albumArtPath.clear();
			return;
		}

		try
		{
			auto stream = thumbnail.OpenReadAsync().get();
			const uint64_t size64 = stream.Size();
			if (size64 == 0 || size64 > kMaxAlbumArtBytes)
			{
				outState.hasAlbumArt = false;
				outState.albumArtPath.clear();
				return;
			}

			const uint32_t size = static_cast<uint32_t>(size64);
			const auto bytes = ReadAllBytesFromIRandomAccessStream(stream, size);
			if (bytes.empty())
			{
				outState.hasAlbumArt = false;
				outState.albumArtPath.clear();
				return;
			}

			if (!WriteFileBytes(path, bytes))
			{
				outState.hasAlbumArt = false;
				outState.albumArtPath.clear();
				return;
			}

			outState.hasAlbumArt = true;
		}
		catch (...)
		{
			outState.hasAlbumArt = false;
			outState.albumArtPath.clear();
		}
	}

	void Publish(const MediaState& s)
	{
		std::lock_guard lk(stateMutex_);
		cachedState_ = s;
	}

	// Game-thread visible state
	mutable MediaState state_{};
	std::mutex stateMutex_;
	MediaState cachedState_{};

	// Event-driven worker sync
	std::thread worker_;
	std::atomic<bool> stop_{false};

	std::condition_variable cv_;
	std::mutex cvMutex_;
	std::atomic<bool> dirty_{false};
	std::atomic<uint32_t> pendingReasons_{0};

	// WinRT objects live on worker thread
	GlobalSystemMediaTransportControlsSessionManager manager_{nullptr};
	GlobalSystemMediaTransportControlsSession session_{nullptr};

	// Event revokers (auto-unsubscribe)
	GlobalSystemMediaTransportControlsSessionManager::CurrentSessionChanged_revoker mgrSessionChangedRevoker_{};

	GlobalSystemMediaTransportControlsSession::MediaPropertiesChanged_revoker mediaChangedRevoker_{};
	GlobalSystemMediaTransportControlsSession::PlaybackInfoChanged_revoker playbackChangedRevoker_{};
	GlobalSystemMediaTransportControlsSession::TimelinePropertiesChanged_revoker timelineChangedRevoker_{};

	// Album art cache
	std::filesystem::path cacheDir_;
	std::string lastSongKey_;
	std::string lastAlbumArtPath_;
};

std::unique_ptr<MediaController> CreateMediaController(const std::string& dataFolder)
{
	return std::make_unique<MediaControllerGSMTC>(dataFolder);
}