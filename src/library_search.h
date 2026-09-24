#pragma once
#include "gfx.h"
#include "job_pool.h"
#include "online_icons.h"
#include <memory>
#include <string>
#include <vector>

/// The editor's online-library results: loads the index once, runs searches and
/// downloads thumbnails a few at a time on the job pool. UI thread only.
class LibrarySearch {
public:
    struct Tile {
        LibraryIcon icon;
        Texture tex;
        bool loading = true;
        bool queued = false;
        bool failed = false;
        bool duplicate = false; // same picture as an earlier tile (themes often alias one file)
        uint64_t hash = 0;
    };

    explicit LibrarySearch(JobPool& jobs) : m_jobs(jobs) {}

    /// Start loading the index unless it is loaded or loading already.
    void EnsureIndex();
    bool IndexLoading() const { return m_indexLoading; }
    bool HasIndex() const { return m_index != nullptr; }
    const std::string& IndexError() const { return m_indexError; }

    /// Show matches for these queries (first = what the user typed). Does nothing if
    /// they are the ones already shown; remembered and run later if the index isn't loaded yet.
    void Search(const std::vector<std::string>& queries);
    /// Drop the results (and pending downloads); the next Search runs again.
    void Clear();
    /// Clear() and forget the index, e.g. after its cached files were deleted.
    void Reset();

    /// Queue thumbnail downloads; call once per frame.
    void Pump(int thumbSize);

    const std::vector<Tile>& Tiles() const { return m_tiles; }
    /// True once every tile finished and none has a picture worth showing.
    bool NothingToShow() const;

private:
    void Run();
    void OnThumbnail(size_t i, bool ok, const Image& img);

    JobPool& m_jobs;
    std::shared_ptr<const IconIndex> m_index;
    bool m_indexLoading = false;
    std::string m_indexError;
    std::vector<std::string> m_wanted; // last requested queries
    std::vector<std::string> m_ran;    // queries the current tiles belong to
    std::vector<Tile> m_tiles;
    uint64_t m_gen = 0;                // bumps when tiles are replaced; late downloads are dropped
    int m_inFlight = 0;
    int m_thumbSize = 0;
};
