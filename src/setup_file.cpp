#include "setup_file.h"
#include "app_paths.h"
#include "icon_utils.h"
#include <windows.h>
#include <nlohmann/json.hpp>

using nlohmann::json;

namespace {

constexpr int kFormat = 1; // bump only for changes older versions can't read
constexpr const char* kB64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string U8(const std::wstring& w) { return WideToUtf8(w); }
std::wstring W(const json& j, const char* key)
{
    return j.contains(key) && j[key].is_string() ? Utf8ToWide(j[key].get<std::string>()) : std::wstring();
}

bool LooksLikeIco(const std::vector<uint8_t>& d)
{
    // ICONDIR: reserved 0, type 1, at least one image
    return d.size() > 22 && d[0] == 0 && d[1] == 0 && d[2] == 1 && d[3] == 0 && (d[4] | d[5] << 8) > 0;
}

} // namespace

std::string Base64Encode(const std::vector<uint8_t>& data)
{
    std::string out;
    out.reserve((data.size() + 2) / 3 * 4);
    for (size_t i = 0; i < data.size(); i += 3) {
        uint32_t n = data[i] << 16 | (i + 1 < data.size() ? data[i + 1] << 8 : 0) | (i + 2 < data.size() ? data[i + 2] : 0);
        out += kB64[(n >> 18) & 63];
        out += kB64[(n >> 12) & 63];
        out += i + 1 < data.size() ? kB64[(n >> 6) & 63] : '=';
        out += i + 2 < data.size() ? kB64[n & 63] : '=';
    }
    return out;
}

bool Base64Decode(const std::string& text, std::vector<uint8_t>& out)
{
    out.clear();
    uint32_t acc = 0;
    int bits = 0;
    for (char c : text) {
        if (c == '=' ) break;
        if (c == '\n' || c == '\r' || c == ' ') continue;
        const char* p = strchr(kB64, c);
        if (!p || !c) return false;
        acc = acc << 6 | (uint32_t)(p - kB64);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back((uint8_t)(acc >> bits));
        }
    }
    return true;
}

std::string SerializeSetup(const std::vector<SetupItem>& items, const std::string& appVersion)
{
    json j;
    j["iconger"] = kFormat;
    j["appVersion"] = appVersion;
    json arr = json::array();
    for (const SetupItem& it : items) {
        json e;
        e["name"] = U8(it.name);
        if (!it.shortcut.empty()) e["shortcut"] = U8(it.shortcut);
        if (!it.program.empty()) e["program"] = U8(it.program);
        if (!it.programPath.empty()) e["programPath"] = U8(it.programPath);
        if (!it.aumid.empty()) e["appId"] = U8(it.aumid);
        if (it.unpinned) e["unpinned"] = true;
        e["ico"] = Base64Encode(it.ico);
        arr.push_back(std::move(e));
    }
    j["apps"] = std::move(arr);
    return j.dump(1);
}

bool ParseSetup(const std::string& text, std::vector<SetupItem>& out, std::string& error)
{
    out.clear();
    json j = json::parse(text, nullptr, false);
    if (!j.is_object() || !j.contains("iconger") || !j["iconger"].is_number_integer()) {
        error = "This isn't an Iconger setup file.";
        return false;
    }
    if (j["iconger"].get<int>() > kFormat) {
        error = "This setup was saved by a newer Iconger. Update Iconger first.";
        return false;
    }
    if (!j.contains("apps") || !j["apps"].is_array()) { error = "The setup file is damaged."; return false; }
    for (const json& e : j["apps"]) {
        if (!e.is_object() || !e.contains("ico") || !e["ico"].is_string()) continue;
        SetupItem it;
        it.name = W(e, "name");
        it.shortcut = W(e, "shortcut");
        it.program = W(e, "program");
        it.programPath = W(e, "programPath");
        it.aumid = W(e, "appId");
        it.unpinned = e.contains("unpinned") && e["unpinned"].is_boolean() && e["unpinned"].get<bool>();
        if (!Base64Decode(e["ico"].get<std::string>(), it.ico) || !LooksLikeIco(it.ico)) continue; // skip broken entries
        if (it.name.empty() && it.shortcut.empty() && it.program.empty()) continue;
        out.push_back(std::move(it));
    }
    return true;
}

bool IcoFileBytes(const std::wstring& path, int index, std::vector<uint8_t>& out)
{
    out.clear();
    if (LowerExt(path) == L".ico" && index == 0) return ReadWholeFile(path, out) && LooksLikeIco(out);
    // an icon inside an .exe/.dll: extract it at every size and write a real .ico
    std::vector<Image> sizes;
    for (int s : { 256, 128, 64, 48, 32, 24, 16 }) {
        Image img;
        if (!LoadIconImage(path, index, s, img)) return false;
        sizes.push_back(img.w == s && img.h == s ? std::move(img) : MakeSquareResized(img, s));
    }
    wchar_t dir[MAX_PATH], file[MAX_PATH];
    if (!GetTempPathW(MAX_PATH, dir) || !GetTempFileNameW(dir, L"icg", 0, file)) return false;
    bool ok = WriteIco(file, sizes) && ReadWholeFile(file, out) && LooksLikeIco(out);
    DeleteFileW(file);
    return ok;
}
