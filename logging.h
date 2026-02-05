#pragma once

#include <string>
#include <string_view>
#include <source_location>
#include <format>
#include <memory>
#include <type_traits>

#include "bakkesmod/wrappers/cvarmanagerwrapper.h"

extern std::shared_ptr<CVarManagerWrapper> _globalCvarManager;

constexpr bool DEBUG_LOG = false;

// ------------------------------------------------------------
// Internal helpers
// ------------------------------------------------------------

namespace detail
{
	template <typename CharT>
	using basic_sv = std::basic_string_view<CharT>;

	template <typename CharT>
	[[nodiscard]] std::basic_string<CharT>
	format_location(const std::source_location& loc)
	{
		if constexpr (std::is_same_v<CharT, char>)
		{
			return std::format(
				"[{} ({}:{})]",
				loc.function_name(),
				loc.file_name(),
				loc.line()
			);
		}
		else
		{
			auto tmp = std::format(
				"[{} ({}:{})]",
				loc.function_name(),
				loc.file_name(),
				loc.line()
			);
			return {tmp.begin(), tmp.end()};
		}
	}

	template <typename StringT>
	void log(StringT&& text)
	{
		if (_globalCvarManager)
			_globalCvarManager->log(std::forward<StringT>(text));
	}
}

// ------------------------------------------------------------
// Format wrapper (safe string_view only)
// ------------------------------------------------------------

template <typename CharT>
struct FormatText
{
	using string_view_t = detail::basic_sv<CharT>;

	string_view_t text;
	std::source_location location;

	consteval FormatText(
		const CharT* str,
		std::source_location loc = std::source_location::current()
	) noexcept
		: text(str), location(loc)
	{
	}

	constexpr FormatText(
		string_view_t str,
		std::source_location loc = std::source_location::current()
	) noexcept
		: text(str), location(loc)
	{
	}

	[[nodiscard]] std::basic_string<CharT>
	location_string() const
	{
		return detail::format_location<CharT>(location);
	}
};

using FormatString = FormatText<char>;
using FormatWstring = FormatText<wchar_t>;

// ------------------------------------------------------------
// LOG (always on)
// ------------------------------------------------------------

template <typename... Args>
void LOG(std::string_view fmt, Args&&... args)
{
	detail::log(std::vformat(fmt, std::make_format_args(args...)));
}

template <typename... Args>
void LOG(std::wstring_view fmt, Args&&... args)
{
	detail::log(std::vformat(fmt, std::make_wformat_args(args...)));
}

// ------------------------------------------------------------
// DEBUGLOG (compile-time stripped)
// ------------------------------------------------------------

template <typename CharT, typename... Args>
void DEBUGLOG(const FormatText<CharT>& fmt, Args&&... args)
{
	if constexpr (!DEBUG_LOG)
	{
	}
	else
	{
		using string_t = std::basic_string<CharT>;

		string_t message = std::vformat(
			fmt.text,
			std::conditional_t<
				std::is_same_v<CharT, char>,
				std::format_args,
				std::wformat_args
			>(
				std::forward<Args>(args)...
			)
		);

		detail::log(
			std::format(
				std::is_same_v<CharT, char> ? "{} {}" : L"{} {}",
				message,
				fmt.location_string()
			)
		);
	}
}