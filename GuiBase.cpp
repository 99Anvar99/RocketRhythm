#include "pch.h"
#include "GuiBase.h"

#include "notification.h"

std::string SettingsWindowBase::GetPluginName()
{
	return "RocketRhythm";
}

void SettingsWindowBase::SetImGuiContext(uintptr_t ctx)
{
	ImGui::SetCurrentContext(reinterpret_cast<ImGuiContext*>(ctx));
}

std::string PluginWindowBase::GetMenuName()
{
	return "RocketRhythm";
}

std::string PluginWindowBase::GetMenuTitle()
{
	return menu_title;
}

void PluginWindowBase::SetImGuiContext(uintptr_t ctx)
{
	ImGui::SetCurrentContext(reinterpret_cast<ImGuiContext*>(ctx));
}

bool PluginWindowBase::ShouldBlockInput()
{
	return ImGui::GetIO().WantCaptureMouse || ImGui::GetIO().WantCaptureKeyboard;
}

bool PluginWindowBase::IsActiveOverlay()
{
	// If you don't want the overlay to close when a user presses esc, change this function to return false
	return false;
}

void PluginWindowBase::OnOpen()
{
	isWindowOpen_ = true;
}

void PluginWindowBase::OnClose()
{
	isWindowOpen_ = false;
}

void PluginWindowBase::Render()
{
	RenderWindow();

	if (!isWindowOpen_)
	{
		_globalCvarManager->executeCommand("openmenu " + GetMenuName());
	}
}