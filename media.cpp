#include "pch.h"
#include "media.h"
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.Storage.Streams.h>
#include <filesystem>
#include <fstream>

using namespace winrt;
using namespace Windows::Media::Control;
using namespace Windows::Storage::Streams;

static std::string HashTrack(const std::string& t, const std::string& a, const std::string& al)
{
	std::hash<std::string> h;
	return std::to_string(h(t + a + al));
}

class MediaControllerGSMTC final : public MediaController
{
public:
	explicit MediaControllerGSMTC(const std::string& dataDir)
	{
		init_apartment();
		cacheDir = std::filesystem::path(dataDir) / "RocketRhythm" / "album_cache";
		std::filesystem::create_directories(cacheDir);
	}

	void Update() override
	{
		try
		{
			if (!manager) manager = GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get();
			session = manager.GetCurrentSession();
			if (!session)
			{
				state = {};
				return;
			}
			auto playback = session.GetPlaybackInfo();
			auto media = session.TryGetMediaPropertiesAsync().get();
			auto timeline = session.GetTimelineProperties();
			state.isPlaying = playback.PlaybackStatus() ==
				GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing;
			state.title = to_string(media.Title());
			state.artist = to_string(media.Artist());
			state.album = to_string(media.AlbumTitle());
			state.durationSec = static_cast<int>(timeline.EndTime().count() / 10'000'000);
			state.positionSec = static_cast<int>(timeline.Position().count() / 10'000'000);
			if (state.durationSec > 0) state.progress01 = state.positionSec / static_cast<float>(state.durationSec);
			HandleAlbumArt(media.Thumbnail());
		}
		catch (...) { state = {}; }
	}

	const MediaState& GetState() const override { return state; }

private:
	void HandleAlbumArt(const IRandomAccessStreamReference& thumbnail)
	{
		if (!thumbnail)
		{
			state.hasAlbumArt = false;
			state.albumArtPath.clear();
			return;
		}
		const auto hash = HashTrack(state.title, state.artist, state.album);
		auto path = cacheDir / (hash + ".png");
		state.albumArtPath = path.string();
		if (std::filesystem::exists(path))
		{
			state.hasAlbumArt = true;
			return;
		}
		auto stream = thumbnail.OpenReadAsync().get();
		Buffer buffer(static_cast<uint32_t>(stream.Size()));
		stream.ReadAsync(buffer, buffer.Capacity(), InputStreamOptions::None).get();
		std::ofstream out(path, std::ios::binary);
		out.write(reinterpret_cast<const char*>(buffer.data()), buffer.Length());
		state.hasAlbumArt = true;
	}

	MediaState state{};
	std::filesystem::path cacheDir;
	GlobalSystemMediaTransportControlsSessionManager manager{nullptr};
	GlobalSystemMediaTransportControlsSession session{nullptr};
};

std::unique_ptr<MediaController> CreateMediaController(const std::string& dataFolder)
{
	return std::make_unique<MediaControllerGSMTC>(dataFolder);
}