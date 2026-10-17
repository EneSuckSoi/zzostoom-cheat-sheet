#include "saves.h"

#include <windows.h>
#include <shlobj.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>

#include "log.h"

namespace zz::saves {

namespace {

std::string Narrow(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), nullptr, 0,
                                nullptr, nullptr);
    std::string out(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), out.data(), n, nullptr,
                        nullptr);
    return out;
}

std::wstring Widen(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring out(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), out.data(), n);
    return out;
}

std::wstring SaveRoot() {
    wchar_t* appdata = nullptr;
    std::wstring root;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &appdata)))
        root = std::wstring(appdata) + L"\\EldenRing";
    CoTaskMemFree(appdata);
    return root;
}

std::string ReadSeamlessExtension() {
    HMODULE ersc = GetModuleHandleW(L"ersc.dll");
    if (!ersc) return {};
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(ersc, path, MAX_PATH);
    std::wstring ini(path);
    ini = ini.substr(0, ini.find_last_of(L"\\/") + 1) + L"ersc_settings.ini";
    wchar_t ext[32] = {};
    GetPrivateProfileStringW(L"SAVE", L"save_file_extension", L"co2", ext, 32, ini.c_str());
    std::string out = Narrow(ext);
    out.erase(std::remove_if(out.begin(), out.end(), [](char c) { return c == ' ' || c == '.'; }),
              out.end());
    return out.empty() ? "co2" : out;
}

std::string FormatTime(const FILETIME& ft) {
    FILETIME local;
    SYSTEMTIME st;
    FileTimeToLocalFileTime(&ft, &local);
    FileTimeToSystemTime(&local, &st);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d", st.wYear, st.wMonth, st.wDay,
                  st.wHour, st.wMinute);
    return buf;
}

// ER saves are a BND4 container: USER_DATA000-009 are the character slots and USER_DATA010
// holds the menu summary (active slot flags + name/level of each slot).
void ParseCharacters(SaveFile& save) {
    std::ifstream f(save.path.c_str(), std::ios::binary);
    if (!f) {
        save.error = "cannot open file";
        return;
    }
    auto read_at = [&](uint64_t off, void* out, size_t n) {
        f.seekg(static_cast<std::streamoff>(off));
        f.read(static_cast<char*>(out), static_cast<std::streamsize>(n));
        return static_cast<size_t>(f.gcount()) == n;
    };
    char magic[4];
    int32_t count = 0;
    int64_t header_size = 0;
    if (!read_at(0, magic, 4) || std::memcmp(magic, "BND4", 4) != 0 ||
        !read_at(0x0C, &count, 4) || !read_at(0x20, &header_size, 8) || count < 11 ||
        header_size < 0x18) {
        save.error = "unknown save format";
        return;
    }
    uint32_t data_offset = 0;
    if (!read_at(0x40 + 10 * header_size + 0x10, &data_offset, 4)) {
        save.error = "unreadable";
        return;
    }
    const uint64_t summary = data_offset + 0x10;  // skip the MD5 checksum
    uint8_t active[10] = {};
    if (!read_at(summary + 0x1954, active, sizeof(active))) {
        save.error = "unreadable";
        return;
    }
    for (int i = 0; i < 10; ++i) {
        if (!active[i]) continue;
        const uint64_t base = summary + 0x195E + static_cast<uint64_t>(i) * 0x24C;
        wchar_t name[17] = {};
        Character c;
        c.slot = i;
        read_at(base, name, 34);
        read_at(base + 0x22, &c.level, 4);
        read_at(base + 0x26, &c.seconds_played, 4);
        name[16] = 0;
        c.name = Narrow(name);
        save.characters.push_back(c);
    }
}

bool g_backed_up_this_session = false;

}  // namespace

bool SeamlessLoaded() { return GetModuleHandleW(L"ersc.dll") != nullptr; }

const std::string& ActiveExtension() {
    static std::string ext = [] {
        std::string e = ReadSeamlessExtension();
        return e.empty() ? std::string("sl2") : e;
    }();
    return ext;
}

std::vector<SaveFile> Scan() {
    std::vector<SaveFile> out;
    const std::wstring root = SaveRoot();
    if (root.empty()) return out;
    const std::string active_ext = ActiveExtension();

    WIN32_FIND_DATAW dir;
    HANDLE hd = FindFirstFileW((root + L"\\*").c_str(), &dir);
    if (hd == INVALID_HANDLE_VALUE) return out;
    FILETIME newest_active{};
    int newest_index = -1;
    do {
        if (!(dir.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || dir.cFileName[0] == L'.') continue;
        const std::wstring folder = root + L"\\" + dir.cFileName;
        WIN32_FIND_DATAW file;
        HANDLE hf = FindFirstFileW((folder + L"\\ER0000.*").c_str(), &file);
        if (hf == INVALID_HANDLE_VALUE) continue;
        do {
            std::wstring name = file.cFileName;
            if (name.size() >= 4 && name.substr(name.size() - 4) == L".bak") continue;
            SaveFile s;
            s.path = folder + L"\\" + name;
            s.steam_id = Narrow(dir.cFileName);
            s.file_name = Narrow(name);
            std::string ext = s.file_name.substr(s.file_name.find_last_of('.') + 1);
            s.seamless = ext != "sl2";
            s.modified = FormatTime(file.ftLastWriteTime);
            ParseCharacters(s);
            if (ext == active_ext &&
                (newest_index < 0 || CompareFileTime(&file.ftLastWriteTime, &newest_active) > 0)) {
                newest_active = file.ftLastWriteTime;
                newest_index = static_cast<int>(out.size());
            }
            out.push_back(std::move(s));
        } while (FindNextFileW(hf, &file));
        FindClose(hf);
    } while (FindNextFileW(hd, &dir));
    FindClose(hd);
    if (newest_index >= 0) out[newest_index].active = true;
    return out;
}

std::wstring Backup(const SaveFile& save) {
    const std::wstring folder = save.path.substr(0, save.path.find_last_of(L"\\/"));
    const std::wstring backup_dir = folder + L"\\zzostoom_backups";
    CreateDirectoryW(backup_dir.c_str(), nullptr);
    SYSTEMTIME t;
    GetLocalTime(&t);
    wchar_t stamp[32];
    swprintf(stamp, 32, L"%04d%02d%02d_%02d%02d%02d", t.wYear, t.wMonth, t.wDay, t.wHour,
             t.wMinute, t.wSecond);
    std::wstring file = Widen(save.file_name);
    size_t dot = file.find_last_of(L'.');
    std::wstring dest = backup_dir + L"\\" + file.substr(0, dot) + L"_" + stamp + file.substr(dot);
    if (!CopyFileW(save.path.c_str(), dest.c_str(), TRUE)) {
        ZZ_LOG("Backup failed (%lu)", GetLastError());
        return {};
    }
    ZZ_LOG("Backup written: %s", Narrow(dest).c_str());
    return dest;
}

std::string BackupActive(bool force) {
    if (g_backed_up_this_session && !force) return {};
    for (auto& s : Scan()) {
        if (!s.active) continue;
        std::wstring dest = Backup(s);
        if (dest.empty()) return {};
        g_backed_up_this_session = true;
        return Narrow(dest);
    }
    return {};
}

}  // namespace zz::saves
