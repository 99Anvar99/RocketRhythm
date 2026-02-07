#pragma once
#include <memory>
#include <string>
#include <chrono>
#include <unordered_map>
#include "GuiBase.h"
#include "media.h"
#include "bakkesmod/plugin/bakkesmodplugin.h"
#include "IMGUI/imgui.h"
#include "bakkesmod/wrappers/wrapperstructs.h"
#include "bakkesmod/wrappers/ImageWrapper.h"

class RocketRhythm : public BakkesMod::Plugin::BakkesModPlugin, public SettingsWindowBase, public PluginWindowBase
{
	// Configuration 
	std::shared_ptr<bool> enabled;
	std::shared_ptr<float> uiScaleCVar;
	bool hide_when_not_playing = false;
	bool needs_window_open = false;
	bool needs_window_close = false;
	bool is_not_playing = false;
	mutable std::string cached_menu_name;
	mutable std::string cached_plugin_name;
	bool menu_name_cached = false;
	bool plugin_name_cached = false;

	std::unique_ptr<MediaController> media;
	MediaState mediaState;
	ImFont* customFontSegoeUI = nullptr;
	ImFont* customSettingsFontUI = nullptr;

	float pulseAnimation = 0.0f;
	std::chrono::steady_clock::time_point lastProgressUpdate;
	int lastPositionSec = 0;
	int interpolatedPositionSec = 0;

	std::shared_ptr<ImageWrapper> albumArtTexture;
	bool albumArtLoaded = false;
	std::string currentAlbumArtPath;

	struct ScrollState
	{
		float offset = 0.0f;
		float textWidth = 0.0f;
		bool needsScrolling = false;
	};

	float scrollSpeed = 25.0f;
	std::unordered_map<std::string, ScrollState> scrollStates;
	std::string currentSongHash;

	struct WindowStyle
	{
		// Colors 
		ImVec4 backgroundColor = ImVec4(0.0f, 0.0f, 0.0f, 0.85f);
		ImVec4 accentColor = ImVec4(0.0f, 0.9884f, 1.0f, 1.0f);
		ImVec4 accentColor2 = ImVec4(0.2f, 0.68f, 1.0f, 1.0f);
		ImVec4 textColor = ImVec4(1.00f, 1.00f, 1.00f, 1.0f);
		ImVec4 textColorDim = ImVec4(0.75f, 0.75f, 0.75f, 1.0f);
		ImVec4 textColorFaint = ImVec4(0.55f, 0.55f, 0.55f, 1.0f);

		float windowRounding = 5.0f;
		float albumArtRounding = 14.0f;
		float progressBarHeight = 8.0f;
		float progressBarRounding = 4.0f;
		float albumArtSize = 110.0f;
		bool enablePulse = true;

		bool showAlbumArt = true;
		bool showProgressBar = true;
		bool showAlbumInfo = true;
		float windowOpacity = 1.0f;
		float uiScale = 1.0f;
		bool enableAutoScaling = true;
		float minScale = 0.8f;
		float maxScale = 2.0f;
	} windowStyle;

	ImVector<ImWchar> mergedGlyphRanges;

	const std::string& GetMenuNameCached();
	const std::string& GetPluginNameCached();
	void DrawHelpMarker(const char* desc);
	bool ShouldShowWindow() const;
	void UpdateWindowState();
	void InitializeFont();
	void LoadAlbumArt(const std::string& path);
	void UpdateAnimation(float deltaTime);
	void DrawMusicStateCompact();
	int GetCurrentDisplayPosition();
	void UpdateTextScroll(float deltaTime);
	void DrawScrollableText(const std::string& text, const ImVec4& color);
	float GetDPIScaleFactor();
	float CalculateAutoScaleFactor();
	float GetEffectiveScaleFactor();
	float GetScaledValue(float baseValue);
	void DrawAlbumArtPlaceholderWithScale(float scale);
	void DrawAlbumArtWithScale(float scale);
	void DrawNoMusicState();
	void DrawAlbumArt();
	void DrawProgressBar();

public:
	RocketRhythm();
	~RocketRhythm() override;
	void onLoad() override;
	void onUnload() override;
	void RenderSettings() override;
	std::string GetPluginName() override;
	void RenderWindow() override;
	void RenderCanvas(const CanvasWrapper& canvas);
	void SaveConfig();
	void LoadConfig();
};