#pragma once
#include <cstdint>
#include <string>
#include <vector>

// Self-update from the GitHub releases of rs4t/iconger.
// Fetch/Download functions block: call them from worker threads.

struct ReleaseInfo {
    std::string version;   // "0.5.0" (tag without the leading v)
    std::string pageUrl;   // release page, for "download it yourself"
    std::string notes;     // release notes (markdown)
    std::string exeUrl;    // iconger.exe asset
    uint64_t exeSize = 0;
    std::string sha256;    // lower-case hex from the asset's digest, empty if GitHub gave none
};

/// "0.4.1" or "v0.4.1" -> {0, 4, 1}. False unless it is three whole numbers.
bool ParseVersion(const std::string& s, int out[3]);

/// True if `candidate` is a newer version than `current`. Unparsable = not newer.
bool IsNewerVersion(const std::string& candidate, const std::string& current);

/// Parse GitHub's /releases/latest response. False if it has no iconger.exe asset.
bool ParseLatestRelease(const std::string& json, ReleaseInfo& out);

/// Ask GitHub for the latest release.
bool FetchLatestRelease(ReleaseInfo& out, std::string& error);

/// Download the release's exe, check it, and put it in place of `targetExe` (normally the
/// running exe: Windows lets a running program be renamed, just not overwritten).
/// On failure `targetExe` is left as it was.
bool DownloadAndInstallUpdate(const ReleaseInfo& rel, const std::wstring& targetExe, std::string& error);

/// Checks a downloaded exe: a Windows program, the expected size and hash.
bool VerifyDownload(const std::vector<uint8_t>& data, const ReleaseInfo& rel, std::string& error);

/// Rename targetExe to targetExe.old and newFile to targetExe; undone if the second step fails.
bool SwapInUpdate(const std::wstring& newFile, const std::wstring& targetExe, std::string& error);

/// Delete what an earlier update left next to the exe (the .old copy, a partial .new).
void CleanupAfterUpdate(const std::wstring& exe);

std::string Sha256Hex(const std::vector<uint8_t>& data);
