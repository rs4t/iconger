#include "updater.h"
#include "app_paths.h"
#include "online_icons.h"
#include <windows.h>
#include <bcrypt.h>
#include <cctype>
#include <cstdio>
#include <nlohmann/json.hpp>

using nlohmann::json;

namespace {

constexpr const char* kLatestReleaseApi = "https://api.github.com/repos/rs4t/iconger/releases/latest";
constexpr const char* kExeAssetName = "iconger.exe";

bool WriteWholeFile(const std::wstring& path, const std::vector<uint8_t>& data)
{
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    bool ok = data.empty() || (WriteFile(h, data.data(), (DWORD)data.size(), &written, nullptr) && written == data.size());
    ok = FlushFileBuffers(h) && ok;
    CloseHandle(h);
    if (!ok) DeleteFileW(path.c_str());
    return ok;
}

std::string LowerString(std::string s)
{
    for (char& c : s) c = (char)tolower((unsigned char)c);
    return s;
}

} // namespace

bool ParseVersion(const std::string& s, int out[3])
{
    size_t i = (!s.empty() && (s[0] == 'v' || s[0] == 'V')) ? 1 : 0;
    for (int part = 0; part < 3; ++part) {
        if (i >= s.size() || !isdigit((unsigned char)s[i])) return false;
        long v = 0;
        while (i < s.size() && isdigit((unsigned char)s[i])) {
            v = v * 10 + (s[i++] - '0');
            if (v > 1000000) return false;
        }
        out[part] = (int)v;
        if (part < 2) {
            if (i >= s.size() || s[i] != '.') return false;
            ++i;
        }
    }
    return i == s.size();
}

bool IsNewerVersion(const std::string& candidate, const std::string& current)
{
    int a[3], b[3];
    if (!ParseVersion(candidate, a) || !ParseVersion(current, b)) return false;
    for (int i = 0; i < 3; ++i)
        if (a[i] != b[i]) return a[i] > b[i];
    return false;
}

bool ParseLatestRelease(const std::string& text, ReleaseInfo& out)
{
    out = {};
    json j = json::parse(text, nullptr, false);
    if (!j.is_object() || !j.contains("tag_name") || !j["tag_name"].is_string()) return false;
    if (j.value("draft", false) || j.value("prerelease", false)) return false;
    std::string tag = j["tag_name"].get<std::string>();
    int v[3];
    if (!ParseVersion(tag, v)) return false;
    out.version = std::to_string(v[0]) + "." + std::to_string(v[1]) + "." + std::to_string(v[2]);
    if (j.contains("html_url") && j["html_url"].is_string()) out.pageUrl = j["html_url"].get<std::string>();
    if (j.contains("body") && j["body"].is_string()) out.notes = j["body"].get<std::string>();
    if (!j.contains("assets") || !j["assets"].is_array()) return false;
    for (const auto& a : j["assets"]) {
        if (!a.is_object() || !a.contains("name") || !a["name"].is_string()) continue;
        if (LowerString(a["name"].get<std::string>()) != kExeAssetName) continue;
        if (!a.contains("browser_download_url") || !a["browser_download_url"].is_string()) continue;
        out.exeUrl = a["browser_download_url"].get<std::string>();
        if (a.contains("size") && a["size"].is_number_unsigned()) out.exeSize = a["size"].get<uint64_t>();
        if (a.contains("digest") && a["digest"].is_string()) {
            std::string d = a["digest"].get<std::string>();
            if (d.rfind("sha256:", 0) == 0) out.sha256 = LowerString(d.substr(7));
        }
        // only download from GitHub itself
        return out.exeUrl.rfind("https://github.com/", 0) == 0;
    }
    return false;
}

bool FetchLatestRelease(ReleaseInfo& out, std::string& error)
{
    std::vector<uint8_t> data;
    if (!HttpGet(kLatestReleaseApi, data, error)) return false;
    if (!ParseLatestRelease(std::string(data.begin(), data.end()), out)) {
        error = "The latest release has no iconger.exe.";
        return false;
    }
    return true;
}

std::string Sha256Hex(const std::vector<uint8_t>& data)
{
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    unsigned char digest[32] = {};
    bool ok = BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) == 0 &&
              BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0) == 0 &&
              BCryptHashData(hash, (PUCHAR)data.data(), (ULONG)data.size(), 0) == 0 &&
              BCryptFinishHash(hash, digest, sizeof(digest), 0) == 0;
    if (hash) BCryptDestroyHash(hash);
    if (alg) BCryptCloseAlgorithmProvider(alg, 0);
    if (!ok) return {};
    char hex[65];
    for (int i = 0; i < 32; ++i) snprintf(hex + i * 2, 3, "%02x", digest[i]);
    return std::string(hex, 64);
}

bool VerifyDownload(const std::vector<uint8_t>& data, const ReleaseInfo& rel, std::string& error)
{
    if (data.size() < 1024 || data[0] != 'M' || data[1] != 'Z') { error = "The download isn't a Windows program."; return false; }
    if (rel.exeSize && data.size() != rel.exeSize) { error = "The download is incomplete."; return false; }
    if (!rel.sha256.empty() && Sha256Hex(data) != rel.sha256) { error = "The download is damaged (checksum mismatch)."; return false; }
    return true;
}

bool SwapInUpdate(const std::wstring& newFile, const std::wstring& targetExe, std::string& error)
{
    std::wstring old = targetExe + L".old";
    DeleteFileW(old.c_str()); // from an earlier update
    if (!MoveFileExW(targetExe.c_str(), old.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        error = "Couldn't replace iconger.exe (is its folder read-only?).";
        return false;
    }
    if (!MoveFileExW(newFile.c_str(), targetExe.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        MoveFileExW(old.c_str(), targetExe.c_str(), MOVEFILE_REPLACE_EXISTING); // put the old one back
        error = "Couldn't put the new version in place.";
        return false;
    }
    return true;
}

bool DownloadAndInstallUpdate(const ReleaseInfo& rel, const std::wstring& targetExe, std::string& error)
{
    std::vector<uint8_t> data;
    if (!HttpGet(rel.exeUrl, data, error)) { error = "Download failed: " + error; return false; }
    if (!VerifyDownload(data, rel, error)) return false;
    // next to the exe, so the final rename stays on one volume
    std::wstring newFile = targetExe + L".new";
    if (!WriteWholeFile(newFile, data)) { error = "Couldn't save the download next to iconger.exe."; return false; }
    if (!SwapInUpdate(newFile, targetExe, error)) { DeleteFileW(newFile.c_str()); return false; }
    return true;
}

void CleanupAfterUpdate(const std::wstring& exe)
{
    // The old process may still be exiting; its image is locked until it's gone.
    for (int i = 0; i < 20; ++i) {
        DWORD a = GetFileAttributesW((exe + L".old").c_str());
        if (a == INVALID_FILE_ATTRIBUTES || DeleteFileW((exe + L".old").c_str())) break;
        Sleep(100);
    }
    DeleteFileW((exe + L".new").c_str());
}
