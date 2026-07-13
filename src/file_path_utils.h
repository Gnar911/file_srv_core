// Cross-platform helpers for application data directory lookup
#pragma once

#include <filesystem>
#include <string>
#include <string_view>

#if defined(_WIN32)
#  include <windows.h>
#  include <shlobj.h>
#  include <combaseapi.h>
#endif

namespace util {

// Return the per-user application data directory (not including an app name).
// Windows: %LOCALAPPDATA% (SHGetKnownFolderPath FOLDERID_LocalAppData)
// Linux:   $XDG_DATA_HOME or ~/.local/share
inline std::filesystem::path GetAppDataDirectory() {
#if defined(_WIN32)
	PWSTR path = nullptr;
	if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &path)) && path != nullptr) {
		std::filesystem::path p(path);
		CoTaskMemFree(path);
		return p;
	}
	// Fallback to environment variable
	if (auto env = ::_wgetenv(L"LOCALAPPDATA")) {
		return std::filesystem::path(env);
	}
	return {};
#else
	if (const char* xdg = std::getenv("XDG_DATA_HOME")) {
		return std::filesystem::path(xdg);
	}
	if (const char* home = std::getenv("HOME")) {
		return std::filesystem::path(home) / ".local" / "share";
	}
	return {};
#endif
}

// Convenience: append an application folder name to the per-user app data dir.
inline std::filesystem::path GetAppDataDirectoryForApp(std::string_view app_name) {
	auto base = GetAppDataDirectory();
	if (base.empty()) return {};
#if defined(_WIN32)
	return base / std::filesystem::path(std::wstring(app_name.begin(), app_name.end()));
#else
	return base / std::filesystem::path(app_name);
#endif
}

// Return a stable, cross-platform system temp directory. Avoid calling
// std::filesystem::temp_directory_path() since that can be influenced by
// platform-specific configuration and may resolve relative paths.
inline std::filesystem::path GetSystemTempDirectory() {
#if defined(_WIN32)
	// Try Win32 API first
	char buf[MAX_PATH + 1] = {0};
	DWORD n = GetTempPathA(MAX_PATH, buf);
	if (n > 0 && n <= MAX_PATH) {
		return std::filesystem::path(std::string(buf, buf + n));
	}
	if (const char* local = std::getenv("LOCALAPPDATA")) {
		return std::filesystem::path(local) / "Temp";
	}
	if (const char* temp = std::getenv("TEMP")) {
		return std::filesystem::path(temp);
	}
	return std::filesystem::path("C:/Windows/Temp");
#else
	if (const char* td = std::getenv("TMPDIR")) {
		std::filesystem::path p(td);
		if (p.is_absolute()) return p;
	}
	// Use a sane default for POSIX systems.
	return std::filesystem::path("/tmp");
#endif
}

} // namespace util

// Lightweight forwarding into the `file_service` namespace for existing callers
inline std::filesystem::path GetAppDataDirectory() {
	return util::GetAppDataDirectory();
}
inline std::filesystem::path GetAppDataDirectoryForApp(std::string_view app_name) {
	return util::GetAppDataDirectoryForApp(app_name);
}
inline std::filesystem::path GetSystemTempDirectory() {
    return util::GetSystemTempDirectory();
}

