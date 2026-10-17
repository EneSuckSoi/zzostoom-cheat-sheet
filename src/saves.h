#pragma once
#include <cstdint>
#include <string>
#include <vector>

// Read-only view of the save files in %APPDATA%\EldenRing plus a backup helper.
namespace zz::saves {

struct Character {
    int slot = 0;
    std::string name;
    uint32_t level = 0;
    uint32_t seconds_played = 0;
};

struct SaveFile {
    std::wstring path;
    std::string steam_id;
    std::string file_name;  // e.g. "ER0000.co2"
    bool seamless = false;  // uses the Seamless Coop extension
    bool active = false;    // extension in use this session, most recently written
    std::string modified;   // local time, "2026-10-14 23:20"
    std::vector<Character> characters;
    std::string error;
};

// Extension the running game writes to: Seamless Coop's save_file_extension (default "co2")
// when ersc.dll is loaded, otherwise "sl2".
const std::string& ActiveExtension();
bool SeamlessLoaded();

std::vector<SaveFile> Scan();

// Copies the save to <save dir>\zzostoom_backups\<name>_<timestamp>.<ext>. Returns the backup
// path, or an empty string on failure.
std::wstring Backup(const SaveFile& save);
// Backs up the active save of the current session (once per session unless `force`).
std::string BackupActive(bool force);

}  // namespace zz::saves
