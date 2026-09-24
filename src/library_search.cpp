#include "library_search.h"
#include <algorithm>

void LibrarySearch::EnsureIndex()
{
    if (m_index || m_indexLoading) return;
    m_indexLoading = true;
    m_indexError.clear();
    m_jobs.Submit([this]() -> JobPool::Done {
        auto index = std::make_shared<IconIndex>();
        std::string err;
        bool ok = index->Load(err);
        return [this, index, ok, err] {
            m_indexLoading = false;
            if (ok) m_index = index;
            else m_indexError = err;
            m_ran.clear();
            Run();
        };
    });
}

void LibrarySearch::Search(const std::vector<std::string>& queries)
{
    m_wanted = queries;
    Run();
}

void LibrarySearch::Run()
{
    if (!m_index || m_wanted.empty() || m_wanted == m_ran) return;
    m_ran = m_wanted;
    ++m_gen; // results still downloading for the old query are dropped
    m_inFlight = 0;
    m_tiles.clear();
    for (auto& hit : m_index->Search(m_wanted, 36)) {
        Tile t;
        t.icon = std::move(hit);
        m_tiles.push_back(std::move(t));
    }
}

void LibrarySearch::Clear()
{
    m_tiles.clear();
    m_ran.clear();
    m_wanted.clear();
    m_inFlight = 0;
    ++m_gen;
}

void LibrarySearch::Reset()
{
    Clear();
    m_index.reset();
}

void LibrarySearch::Pump(int thumbSize)
{
    if (thumbSize != m_thumbSize) { // DPI change: thumbnails were rendered for the old size
        m_thumbSize = thumbSize;
        m_ran.clear();
        Run();
    }
    for (size_t i = 0; i < m_tiles.size() && m_inFlight < 8; ++i) {
        Tile& t = m_tiles[i];
        if (!t.loading || t.queued) continue;
        t.queued = true;
        ++m_inFlight;
        m_jobs.Submit([this, gen = m_gen, i, icon = t.icon, size = thumbSize]() -> JobPool::Done {
            Image img;
            std::string err;
            bool ok = FetchLibraryIcon(icon, size, img, err);
            return [this, gen, i, ok, img] {
                if (gen != m_gen || i >= m_tiles.size()) return;
                --m_inFlight;
                OnThumbnail(i, ok, img);
            };
        });
    }
}

void LibrarySearch::OnThumbnail(size_t i, bool ok, const Image& img)
{
    Tile& tile = m_tiles[i];
    tile.loading = false;
    tile.failed = !ok;
    if (!ok) return;
    uint64_t h = 1469598103934665603ull;
    for (uint8_t b : img.rgba) { h ^= b; h *= 1099511628211ull; }
    tile.hash = h;

    // Of tiles with the same picture, the first in result order is kept, whichever
    // download happened to finish first.
    auto same = [&](const Tile& t) { return !t.loading && !t.failed && t.hash == h; };
    for (size_t k = 0; k < i && !tile.duplicate; ++k)
        if (same(m_tiles[k])) tile.duplicate = true;
    if (tile.duplicate) return;
    tile.tex = Texture(img);
    for (size_t k = i + 1; k < m_tiles.size(); ++k) {
        if (same(m_tiles[k]) && !m_tiles[k].duplicate) {
            m_tiles[k].duplicate = true;
            m_tiles[k].tex.Reset();
        }
    }
}

bool LibrarySearch::NothingToShow() const
{
    return std::none_of(m_tiles.begin(), m_tiles.end(),
                        [](const Tile& t) { return t.loading || (!t.failed && !t.duplicate); });
}
