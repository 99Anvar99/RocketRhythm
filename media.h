#pragma once
#include <string>
#include <memory>

struct MediaState
{
    bool isPlaying = false;
    std::string title;
    std::string artist;
    std::string album;
    int durationSec = 0;
    int positionSec = 0;
    float progress01 = 0.0f;
    std::string albumArtPath;
    bool hasAlbumArt = false;
};

class MediaController
{
public:
    virtual ~MediaController() = default;
    virtual void Update() = 0;               // now cheap / non-blocking
    virtual const MediaState& GetState() const = 0;
};

std::unique_ptr<MediaController> CreateMediaController(const std::string& dataFolder);