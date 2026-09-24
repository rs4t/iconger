#pragma once
#include <string>
#include <map>

/// The icon a shortcut had before Iconger first touched it.
struct IconBackupEntry {
    std::wstring iconPath;  // raw, as stored in the .lnk; empty = no override (target's own icon)
    int          iconIndex = 0;
};

/// Remembers original icons so "Restore original" gives back exactly what the
/// shortcut had, instead of blindly clearing the icon location (which broke
/// shortcuts whose installer set a specific icon index).
/// Stored as UTF-8 TSV in %LOCALAPPDATA%\Iconger\backups.tsv, keyed by .lnk file name.
class IconBackup {
public:
    void Load();
    bool Save() const;

    bool Has(const std::wstring& lnkPath) const;
    const IconBackupEntry* Get(const std::wstring& lnkPath) const;

    /// Records the original only if there is no entry yet (first change wins).
    void RecordIfMissing(const std::wstring& lnkPath, const IconBackupEntry& original);
    void Remove(const std::wstring& lnkPath);

    const std::map<std::wstring, IconBackupEntry>& Entries() const { return m_entries; }

private:
    static std::wstring Key(const std::wstring& lnkPath);
    std::map<std::wstring, IconBackupEntry> m_entries;
};
