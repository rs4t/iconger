#include "online_icons.h"
#include "app_paths.h"
#include <windows.h>
#include <winhttp.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <future>
#include <nlohmann/json.hpp>
#include <stb_image.h>
#include <nanosvg.h>
#include <nanosvgrast.h>

using nlohmann::json;

namespace {

// Pinned where the library allows it, so an upstream rename can't break search.
constexpr const char* kDashboardBase = "https://cdn.jsdelivr.net/gh/homarr-labs/dashboard-icons@main/";
constexpr const char* kSimpleIconsBase = "https://cdn.jsdelivr.net/npm/simple-icons@16/";
constexpr int kIndexMaxAgeHours = 24 * 7;

// Icon themes hosted on GitHub: one API call lists the apps folder (cached a week),
// icons come from the jsDelivr CDN.
struct ThemeSource { IconLibrary lib; const char* repo; const char* branch; const char* path; };
constexpr ThemeSource kThemes[] = {
    { IconLibrary::WhiteSur, "vinceliuice/WhiteSur-icon-theme",           "master", "src/apps/scalable" },
    { IconLibrary::Fluent,   "vinceliuice/Fluent-icon-theme",             "master", "src/scalable/apps" },
    { IconLibrary::Papirus,  "PapirusDevelopmentTeam/papirus-icon-theme", "master", "Papirus/64x64/apps" },
    { IconLibrary::Tela,     "vinceliuice/Tela-icon-theme",               "master", "src/scalable/apps" },
    { IconLibrary::Candy,    "EliverLara/candy-icons",                    "master", "apps/scalable" },
};

const ThemeSource* FindTheme(IconLibrary lib)
{
    for (const auto& t : kThemes)
        if (t.lib == lib) return &t;
    return nullptr;
}

std::string ThemeFileUrl(const ThemeSource& t, const std::string& file)
{
    return std::string("https://cdn.jsdelivr.net/gh/") + t.repo + "@" + t.branch + "/" + t.path + "/" + file;
}

std::string Str(const std::vector<uint8_t>& v) { return std::string(v.begin(), v.end()); }

std::wstring CacheDir()
{
    std::wstring dir = DataDir() + L"\\cache";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir;
}

std::wstring CachePath(const std::string& url)
{
    uint64_t h = 1469598103934665603ull;
    for (unsigned char c : url) { h ^= c; h *= 1099511628211ull; }
    wchar_t name[32];
    swprintf_s(name, L"\\%016llx.bin", (unsigned long long)h);
    return CacheDir() + name;
}

bool FileAgeHours(const std::wstring& path, double& hours)
{
    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fad)) return false;
    FILETIME now;
    GetSystemTimeAsFileTime(&now);
    ULARGE_INTEGER a, b;
    a.LowPart = fad.ftLastWriteTime.dwLowDateTime; a.HighPart = fad.ftLastWriteTime.dwHighDateTime;
    b.LowPart = now.dwLowDateTime; b.HighPart = now.dwHighDateTime;
    hours = (double)(b.QuadPart - a.QuadPart) / 36e9;
    return true;
}

bool WriteFileAtomic(const std::wstring& path, const std::vector<uint8_t>& data)
{
    std::wstring tmp = path + L".tmp" + std::to_wstring(GetCurrentThreadId());
    HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    bool ok = data.empty() || (WriteFile(h, data.data(), (DWORD)data.size(), &written, nullptr) && written == data.size());
    CloseHandle(h);
    return ok && MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING);
}

// Library names like "firefox-dark" / "discord-light" are variants of the same app.
std::string StripVariant(const std::string& n)
{
    for (const char* suffix : { "dark", "light" }) {
        size_t len = strlen(suffix);
        if (n.size() > len && n.compare(n.size() - len, len, suffix) == 0) return n.substr(0, n.size() - len);
    }
    return n;
}

int ScoreName(const std::string& candidate, const std::string& q)
{
    if (candidate.empty() || q.size() < 2) return 0;
    if (candidate == q) return 100;
    if (candidate.rfind(q, 0) == 0) return 75 - std::min(20, (int)(candidate.size() - q.size()));
    if (candidate.size() >= 4 && q.rfind(candidate, 0) == 0) return 50;           // "visualstudiocode" vs "visualstudio"
    if (q.size() >= 4 && candidate.find(q) != std::string::npos) return 40 - std::min(15, (int)(candidate.size() - q.size()) / 2);
    return 0;
}

} // namespace

const char* LibraryName(IconLibrary lib)
{
    switch (lib) {
    case IconLibrary::Dashboard:   return "Dashboard Icons";
    case IconLibrary::WhiteSur:    return "WhiteSur";
    case IconLibrary::Fluent:      return "Fluent";
    case IconLibrary::Papirus:     return "Papirus";
    case IconLibrary::Tela:        return "Tela";
    case IconLibrary::Candy:       return "Candy";
    case IconLibrary::SimpleIcons: return "Simple Icons";
    default:                       return "";
    }
}

std::string LibraryIcon::Url() const
{
    switch (lib) {
    case IconLibrary::Dashboard:   return std::string(kDashboardBase) + "png/" + name + ".png";
    case IconLibrary::SimpleIcons: return std::string(kSimpleIconsBase) + "icons/" + name + ".svg";
    default:
        if (const ThemeSource* t = FindTheme(lib)) return ThemeFileUrl(*t, name + ".svg");
        return {};
    }
}

std::string LibraryIcon::Id() const
{
    std::string id = NormalizeName(LibraryName(lib)) + "-" + name;
    for (char& c : id)
        if (!isalnum((unsigned char)c) && c != '-' && c != '_' && c != '.') c = '_';
    return id;
}

std::string NormalizeName(const std::string& s)
{
    std::string out;
    for (unsigned char c : s)
        if (isalnum(c)) out.push_back((char)tolower(c));
    return out;
}

// ============================================================================
// Network
// ============================================================================

bool HttpGet(const std::string& url, std::vector<uint8_t>& out, std::string& error)
{
    out.clear();
    std::wstring wurl = Utf8ToWide(url);
    URL_COMPONENTS uc = { sizeof(uc) };
    wchar_t host[256] = {}, path[2048] = {};
    uc.lpszHostName = host; uc.dwHostNameLength = (DWORD)std::size(host);
    uc.lpszUrlPath = path;  uc.dwUrlPathLength = (DWORD)std::size(path);
    if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc)) { error = "Bad URL"; return false; }

    HINTERNET session = WinHttpOpen(L"Iconger/" ICONGER_CORE_VERSION " (+https://github.com/rs4t/iconger)",
                                    WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, nullptr, nullptr, 0);
    if (!session) { error = "No network"; return false; }
    WinHttpSetTimeouts(session, 8000, 8000, 15000, 30000);
    HINTERNET conn = WinHttpConnect(session, host, uc.nPort, 0);
    HINTERNET req = conn ? WinHttpOpenRequest(conn, L"GET", path, nullptr, WINHTTP_NO_REFERER,
                                              WINHTTP_DEFAULT_ACCEPT_TYPES,
                                              uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0)
                         : nullptr;
    bool ok = false;
    if (req && WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0, nullptr, 0, 0, 0) &&
        WinHttpReceiveResponse(req, nullptr)) {
        DWORD status = 0, len = sizeof(status);
        WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, nullptr, &status, &len, nullptr);
        if (status == 200) {
            DWORD avail = 0;
            ok = true;
            while (WinHttpQueryDataAvailable(req, &avail) && avail) {
                size_t at = out.size();
                out.resize(at + avail);
                DWORD read = 0;
                if (!WinHttpReadData(req, out.data() + at, avail, &read)) { ok = false; break; }
                out.resize(at + read);
                if (out.size() > (64u << 20)) { ok = false; break; }
            }
            if (!ok) error = "Download interrupted";
        } else {
            error = "HTTP " + std::to_string(status);
        }
    } else {
        error = "Can't reach " + WideToUtf8(host);
    }
    if (req) WinHttpCloseHandle(req);
    if (conn) WinHttpCloseHandle(conn);
    WinHttpCloseHandle(session);
    return ok;
}

bool CachedGet(const std::string& url, int maxAgeHours, std::vector<uint8_t>& out, std::string& error)
{
    std::wstring file = CachePath(url);
    double age = 0;
    bool cached = FileAgeHours(file, age);
    if (cached && (maxAgeHours < 0 || age < maxAgeHours) && ReadWholeFile(file, out) && !out.empty())
        return true;
    if (HttpGet(url, out, error)) {
        WriteFileAtomic(file, out);
        return true;
    }
    // offline: an old copy beats nothing
    return cached && ReadWholeFile(file, out) && !out.empty();
}

// ============================================================================
// Index
// ============================================================================

std::vector<LibraryIcon> IconIndex::ParseDashboardTree(const std::string& text)
{
    std::vector<LibraryIcon> icons;
    json j = json::parse(text, nullptr, false);
    if (!j.is_object() || !j.contains("png") || !j["png"].is_array()) return icons;
    for (const auto& f : j["png"]) {
        if (!f.is_string()) continue;
        std::string n = f.get<std::string>();
        if (n.size() < 5 || n.compare(n.size() - 4, 4, ".png") != 0) continue;
        LibraryIcon ic;
        ic.lib = IconLibrary::Dashboard;
        ic.name = n.substr(0, n.size() - 4);
        icons.push_back(std::move(ic));
    }
    return icons;
}

std::vector<LibraryIcon> IconIndex::ParseSimpleIcons(const std::string& text)
{
    std::vector<LibraryIcon> icons;
    json j = json::parse(text, nullptr, false);
    if (j.is_object() && j.contains("icons")) j = j["icons"]; // older data layout
    if (!j.is_array()) return icons;
    for (const auto& e : j) {
        if (!e.is_object() || !e.contains("title") || !e.contains("hex")) continue;
        LibraryIcon ic;
        ic.lib = IconLibrary::SimpleIcons;
        ic.title = e["title"].get<std::string>();
        // slugs are given explicitly in current data; derive like simple-icons does otherwise
        ic.name = e.contains("slug") ? e["slug"].get<std::string>() : NormalizeName(ic.title);
        ic.brandRgb = (uint32_t)strtoul(e["hex"].get<std::string>().c_str(), nullptr, 16);
        if (e.contains("aliases") && e["aliases"].contains("aka"))
            for (const auto& a : e["aliases"]["aka"])
                if (a.is_string()) ic.aliases.push_back(a.get<std::string>());
        icons.push_back(std::move(ic));
    }
    return icons;
}

std::vector<LibraryIcon> IconIndex::ParseGitHubTree(const std::string& text, IconLibrary lib)
{
    std::vector<LibraryIcon> icons;
    json j = json::parse(text, nullptr, false);
    if (!j.is_object() || !j.contains("tree")) return icons;
    for (const auto& e : j["tree"]) {
        if (!e.contains("path") || !e["path"].is_string()) continue;
        std::string p = e["path"].get<std::string>();
        if (p.size() < 5 || p.compare(p.size() - 4, 4, ".svg") != 0) continue;
        LibraryIcon ic;
        ic.lib = lib;
        ic.name = p.substr(0, p.size() - 4);
        icons.push_back(std::move(ic));
    }
    return icons;
}

void IconIndex::AddForTests(std::vector<LibraryIcon> icons)
{
    m_icons.insert(m_icons.end(), std::make_move_iterator(icons.begin()), std::make_move_iterator(icons.end()));
}

bool IconIndex::Load(std::string& error)
{
    // Every index downloads in parallel; a library that fails is simply left out.
    struct Part { std::vector<LibraryIcon> icons; std::string error; };
    std::vector<std::future<Part>> parts;
    auto fetch = [](std::string url, auto parse) {
        return std::async(std::launch::async, [url, parse] {
            Part part;
            std::vector<uint8_t> data;
            if (CachedGet(url, kIndexMaxAgeHours, data, part.error)) part.icons = parse(Str(data));
            return part;
        });
    };
    parts.push_back(fetch(std::string(kDashboardBase) + "tree.json",
                          [](const std::string& j) { return ParseDashboardTree(j); }));
    for (const ThemeSource& t : kThemes) {
        std::string url = std::string("https://api.github.com/repos/") + t.repo + "/git/trees/" + t.branch + ":" + t.path;
        IconLibrary lib = t.lib;
        parts.push_back(fetch(url, [lib](const std::string& j) { return ParseGitHubTree(j, lib); }));
    }
    parts.push_back(fetch(std::string(kSimpleIconsBase) + "data/simple-icons.json",
                          [](const std::string& j) { return ParseSimpleIcons(j); }));

    m_icons.clear();
    std::string lastError;
    for (auto& f : parts) {
        Part part = f.get();
        if (part.icons.empty()) { lastError = part.error; continue; }
        AddForTests(std::move(part.icons));
    }
    if (m_icons.empty()) error = lastError.empty() ? "No icon libraries could be loaded." : lastError;
    return !m_icons.empty();
}

std::vector<LibraryIcon> IconIndex::Search(const std::vector<std::string>& queries, size_t limit, size_t perLibrary) const
{
    std::vector<std::string> qs;
    for (const auto& q : queries) {
        std::string n = NormalizeName(q);
        if (n.size() >= 2 && std::find(qs.begin(), qs.end(), n) == qs.end()) qs.push_back(n);
    }
    if (qs.empty()) return {};

    struct Hit { int score; size_t idx; };
    std::vector<Hit> hits;
    for (size_t i = 0; i < m_icons.size(); ++i) {
        const LibraryIcon& ic = m_icons[i];
        std::string name = NormalizeName(ic.name);
        std::string base = NormalizeName(StripVariant(ic.name));
        int best = 0;
        for (const auto& q : qs) {
            best = std::max(best, ScoreName(name, q));
            best = std::max(best, ScoreName(base, q) - 3); // "firefox-dark" just behind "firefox"
            if (!ic.title.empty()) best = std::max(best, ScoreName(NormalizeName(ic.title), q));
            for (const auto& a : ic.aliases) best = std::max(best, ScoreName(NormalizeName(a), q) - 5);
        }
        if (best > 0) hits.push_back({ best * 16 - (int)ic.lib, i }); // tie-break only: library order (see enum)
    }
    std::stable_sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) { return a.score > b.score; });
    std::vector<LibraryIcon> out;
    size_t perLib[(int)IconLibrary::Count] = {};
    for (size_t k = 0; k < hits.size() && out.size() < limit; ++k) {
        const LibraryIcon& ic = m_icons[hits[k].idx];
        if (perLib[(int)ic.lib]++ >= perLibrary) continue;
        out.push_back(ic);
    }
    return out;
}

// ============================================================================
// Rendering
// ============================================================================

bool RenderSvg(const std::string& svg, int size, Image& out, uint32_t forceColor)
{
    std::string buf = svg; // nsvgParse writes into its input
    NSVGimage* image = nsvgParse(buf.data(), "px", 96.0f);
    if (!image) return false;
    bool ok = false;
    if (image->width > 0 && image->height > 0 && image->shapes) {
        if (forceColor) {
            for (NSVGshape* s = image->shapes; s; s = s->next) {
                if (s->fill.type != NSVG_PAINT_NONE) { s->fill.type = NSVG_PAINT_COLOR; s->fill.color = forceColor; }
                if (s->stroke.type != NSVG_PAINT_NONE) { s->stroke.type = NSVG_PAINT_COLOR; s->stroke.color = forceColor; }
            }
        }
        NSVGrasterizer* rast = nsvgCreateRasterizer();
        if (rast) {
            float scale = std::min(size / image->width, size / image->height);
            float tx = (size - image->width * scale) * 0.5f, ty = (size - image->height * scale) * 0.5f;
            out.w = out.h = size;
            out.rgba.assign((size_t)size * size * 4, 0);
            nsvgRasterize(rast, image, tx, ty, scale, out.rgba.data(), size, size, size * 4);
            nsvgDeleteRasterizer(rast);
            ok = true;
        }
    }
    nsvgDelete(image);
    return ok;
}

Image MakeBrandTile(const std::string& glyphSvg, uint32_t brandRgb, int size)
{
    Image tile;
    tile.w = tile.h = size;
    tile.rgba.assign((size_t)size * size * 4, 0);
    const float r = (float)((brandRgb >> 16) & 0xFF), g = (float)((brandRgb >> 8) & 0xFF), b = (float)(brandRgb & 0xFF);
    const float lum = (0.2126f * r + 0.7152f * g + 0.0722f * b) / 255.0f;

    // rounded square, anti-aliased via a signed distance
    const float margin = size * 0.06f, radius = size * 0.22f;
    const float half = size * 0.5f - margin, inner = half - radius;
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            float px = std::fabs(x + 0.5f - size * 0.5f) - inner, py = std::fabs(y + 0.5f - size * 0.5f) - inner;
            float dist = std::hypot(std::max(px, 0.0f), std::max(py, 0.0f)) + std::min(std::max(px, py), 0.0f) - radius;
            float cov = std::clamp(0.5f - dist, 0.0f, 1.0f);
            uint8_t* p = &tile.rgba[((size_t)y * size + x) * 4];
            p[0] = (uint8_t)r; p[1] = (uint8_t)g; p[2] = (uint8_t)b; p[3] = (uint8_t)std::lround(cov * 255);
        }
    }

    // glyph: white, or near-black on very light brands so it stays visible
    Image glyph;
    int gs = std::max(8, (int)std::lround(size * 0.56f));
    uint32_t glyphColor = lum > 0.72f ? 0xFF211B1Bu : 0xFFFFFFFFu;
    if (RenderSvg(glyphSvg, gs, glyph, glyphColor)) {
        int off = (size - gs) / 2;
        for (int y = 0; y < gs; ++y) {
            for (int x = 0; x < gs; ++x) {
                const uint8_t* s = &glyph.rgba[((size_t)y * gs + x) * 4];
                uint8_t* d = &tile.rgba[((size_t)(y + off) * size + (x + off)) * 4];
                float a = s[3] / 255.0f;
                for (int c = 0; c < 3; ++c) d[c] = (uint8_t)std::lround(s[c] * a + d[c] * (1 - a));
            }
        }
    }
    return tile;
}

bool FetchLibraryIcon(const LibraryIcon& icon, int size, Image& out, std::string& error)
{
    std::vector<uint8_t> data;
    std::string url = icon.Url();
    if (!CachedGet(url, -1, data, error)) return false;

    switch (icon.lib) {
    case IconLibrary::Dashboard: {
        int w = 0, h = 0, n = 0;
        stbi_uc* px = stbi_load_from_memory(data.data(), (int)data.size(), &w, &h, &n, 4);
        if (!px) { error = "Broken image"; return false; }
        Image img;
        img.w = w; img.h = h;
        img.rgba.assign(px, px + (size_t)w * h * 4);
        stbi_image_free(px);
        out = MakeSquareResized(img, size);
        return true;
    }
    case IconLibrary::SimpleIcons:
        out = MakeBrandTile(Str(data), icon.brandRgb, size);
        return !out.empty();
    default: {
        const ThemeSource* theme = FindTheme(icon.lib);
        if (!theme) { error = "Unknown library"; return false; }
        // Aliases in these repos are symlinks; the CDN serves them as a file holding the target's name.
        for (int hop = 0; hop < 3 && data.size() < 200 && Str(data).find("<svg") == std::string::npos; ++hop) {
            std::string target = Str(data);
            target.erase(std::remove_if(target.begin(), target.end(), [](char c) { return c == '\n' || c == '\r' || c == ' '; }), target.end());
            if (target.empty() || target.find('/') != std::string::npos) break;
            if (!CachedGet(ThemeFileUrl(*theme, target), -1, data, error)) return false;
        }
        if (!RenderSvg(Str(data), size, out)) { error = "Broken SVG"; return false; }
        return true;
    }
    }
    return false;
}
