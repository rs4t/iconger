#include "icon_backup.h"
#include "app_paths.h"
#include <windows.h>

static std::wstring BackupFile() { return DataDir() + L"\\backups.tsv"; }

std::wstring IconBackup::Key(const std::wstring& lnkPath)
{
    size_t slash = lnkPath.find_last_of(L"\\/");
    std::wstring key = slash == std::wstring::npos ? lnkPath : lnkPath.substr(slash + 1);
    // CharLowerBuff is Unicode-aware; towlower only folds ASCII in the C locale
    if (!key.empty()) CharLowerBuffW(key.data(), (DWORD)key.size());
    return key;
}

void IconBackup::Load()
{
    m_entries.clear();
    std::vector<uint8_t> bytes;
    if (!ReadWholeFile(BackupFile(), bytes)) return;
    std::wstring text = Utf8ToWide(std::string(bytes.begin(), bytes.end()));

    size_t pos = 0;
    while (pos < text.size()) {
        size_t eol = text.find(L'\n', pos);
        if (eol == std::wstring::npos) eol = text.size();
        std::wstring line = text.substr(pos, eol - pos);
        pos = eol + 1;
        if (!line.empty() && line.back() == L'\r') line.pop_back();

        // key \t iconPath \t iconIndex
        size_t t1 = line.find(L'\t');
        size_t t2 = t1 == std::wstring::npos ? t1 : line.find(L'\t', t1 + 1);
        if (t2 == std::wstring::npos) continue;
        IconBackupEntry e;
        e.iconPath = line.substr(t1 + 1, t2 - t1 - 1);
        e.iconIndex = _wtoi(line.c_str() + t2 + 1);
        m_entries[line.substr(0, t1)] = e;
    }
}

bool IconBackup::Save() const
{
    std::wstring text;
    for (const auto& [key, e] : m_entries)
        text += key + L"\t" + e.iconPath + L"\t" + std::to_wstring(e.iconIndex) + L"\n";
    std::string utf8 = WideToUtf8(text);

    // write-then-rename so a crash never leaves a half-written backup file
    std::wstring tmp = BackupFile() + L".tmp";
    HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    bool ok = utf8.empty() || (WriteFile(h, utf8.data(), (DWORD)utf8.size(), &written, nullptr) && written == utf8.size());
    CloseHandle(h);
    return ok && MoveFileExW(tmp.c_str(), BackupFile().c_str(), MOVEFILE_REPLACE_EXISTING);
}

bool IconBackup::Has(const std::wstring& lnkPath) const { return m_entries.count(Key(lnkPath)) != 0; }

const IconBackupEntry* IconBackup::Get(const std::wstring& lnkPath) const
{
    auto it = m_entries.find(Key(lnkPath));
    return it == m_entries.end() ? nullptr : &it->second;
}

void IconBackup::RecordIfMissing(const std::wstring& lnkPath, const IconBackupEntry& original)
{
    m_entries.emplace(Key(lnkPath), original);
}

void IconBackup::Remove(const std::wstring& lnkPath) { m_entries.erase(Key(lnkPath)); }
