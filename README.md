# zzostoom cheat sheet

**In-game cheat menu for Elden Ring, no Cheat Engine needed.**

Loaded by [Mod Engine 3](https://github.com/garyttierney/me3) as a DLL. Give yourself any item, toggle cheats instantly, run faster, fly with noclip, reveal the map and unlock every Site of Grace, all from inside the game, with keyboard/mouse **or controller**. Works with normal saves and **Seamless Coop** (`.co2`).

**[Download the latest release](https://github.com/EneSuckSoi/zzostoom-cheat-sheet/releases/latest)**

![Items](images/01_items_weapons.png)

## Features

- **Items**: every item in the game, DLC included (2,700+), grouped like the in-game inventory (weapon types, armor, Runes, Smithing Stones, Flora, Spells, Spirit Ashes, Ashes of War, Key Items...). Pick quantity, upgrade level, affinity and spirit ash level. Search box and "Give all".
- **Cheats** (applied the moment you tick them): God Mode, No Death, Attacks Pass Through, Infinite HP / FP / Stamina, No Stagger, **No Fall Damage**, no item / arrow consumption, One-Shot Kills, Torrent No Death, Freeze Enemy AI, Enemies Can't Move, experimental Invisible / Silent, and a **killing aura** (enemies within a radius die instantly; players, co-op partners and NPCs are safe).
- **Remove items**: take any item out of your inventory, with an amount (even ones the game won't let you discard).
- **Enemies**: spawn any of 935 enemies and bosses in front of you for damage tests and challenges (bosses get their health bar).
- **Co-op**: see the players in your Seamless Coop session and teleport next to them.
- **Movement**: player speed, Torrent speed, jump boost (all working with Seamless Coop), fly mode with noclip (jump rises, crouch sinks, no fall damage), teleport up/down, save a position and teleport back.
- **World**: reveal the whole map, unlock all Sites of Grace (base game and Shadow of the Erdtree separately).
- **Character**: add runes, edit attributes, Scadutree and Revered Spirit Ash blessings.
- **Shortcuts**: bind any cheat to a key and/or a controller combo (hold L3 or R3 + a button). Saved between sessions.
- **10 languages**: English, Deutsch, Français, Español, Português (Brasil), Русский, 日本語, 한국어, 简体中文, Türkçe. Item names use the game's official translations.
- **Saves**: list of your save files and characters, automatic backup before the first item of a session.

| | |
|---|---|
| ![Cheats](images/04_cheats.png) | ![Movement](images/05_movement_fly_mode.png) |
| ![World](images/06_world_map_graces.png) | ![Shortcuts](images/07_shortcuts_controller.png) |
| ![Runes](images/02_items_runes.png) | ![Smithing Stones](images/03_items_smithing_stones.png) |

## Requirements

- Elden Ring, latest patch (Steam, `eldenring.exe` 2.7.2.0; 2.7.1.0 also works)
- [Mod Engine 3](https://github.com/garyttierney/me3)
- Windows 10/11, DirectX 12
- Optional: Seamless Coop

## Installation

1. Close the game and download the latest `zzostoom-cheat-sheet-<version>.zip` from [Releases](https://github.com/EneSuckSoi/zzostoom-cheat-sheet/releases/latest).
2. Copy `zzostoom_cheat_sheet.dll` into your ME3 profile's mod folder, usually  
   `%LOCALAPPDATA%\garyttierney\me3\config\profiles\eldenring-mods\`
3. Open the `.me3` profile you launch the game with (e.g. `eldenring-default.me3`) and add:
   ```toml
   [[natives]]
   path = "eldenring-mods/zzostoom_cheat_sheet.dll"
   ```
   The path is relative to the `.me3` file. Using Seamless Coop? Keep its `ersc.dll` entry and just add this one.
4. Start the game through ME3. "zzostoom cheat sheet ready" appears in the top-left corner.

To uninstall, remove those two lines and delete the DLL.

## Controls

| | Keyboard | Controller |
|---|---|---|
| Open / close menu | `INSERT` (changeable, e.g. the key under Esc) | `L3 + R3` |
| Navigate | mouse | D-pad / left stick, A select, B back, LB/RB tabs, Start close |
| Fly mode up / down | Space or Page Up / C or Page Down | A or RT / L3 or LT |

Default controller shortcuts: `L3+Y` God Mode, `L3+B` Infinite FP, `L3+X` Infinite Stamina, `L3+A` No Stagger, `R3+Up` Player Speed, `R3+Right` Torrent Speed, `R3+Down` Fly Mode, `R3+Left` Save Position, `R3+LB` Teleport to saved position.
PlayStation controllers work through Steam Input.

Settings and shortcuts are saved in `zzostoom_cheat_sheet.ini` next to the DLL. If something doesn't work, open an issue and attach `zzostoom_cheat_sheet.log`.

## Important

- **Offline / Seamless Coop only.** Do not use it on the official online servers.
- Back up your saves. The mod backs up the active save automatically, but use it at your own risk.
- Unlocking all Sites of Grace can skip parts of the game.

## Building from source

The DLL is built with the free [llvm-mingw](https://github.com/mstorsjo/llvm-mingw/releases) toolchain (UCRT, x86_64) and Python 3. No Visual Studio needed.

```bash
set ZZ_LLVM_MINGW=C:\path	o\llvm-mingw-ucrt-x86_64in
py build.py            # -> build/zzostoom_cheat_sheet.dll
py build.py --install  # build and copy the DLL into the ME3 eldenring-mods folder
```

| Folder | What's inside |
|---|---|
| `src/` | the mod: overlay (DX12 + ImGui), menu, cheats, controller input, game memory |
| `src/generated/` | item, map and Site of Grace tables (generated from the game data) |
| `lang/` | menu translations, one `English<TAB>translation` file per language (`py tools/gen_lang.py` after editing) |
| `third_party/` | Dear ImGui and MinHook |

Game addresses are found by pattern scanning (`src/game.cpp`); if a game update breaks them, that's the place to look.

## License and credits

Free to use. You may share, modify or build on it **only if you credit "zzostoom" and link to this repository** (https://github.com/EneSuckSoi/zzostoom-cheat-sheet). Selling it is not allowed. See [LICENSE](LICENSE).

Made by **zzostoom**. Uses [Dear ImGui](https://github.com/ocornut/imgui) (MIT) and [MinHook](https://github.com/TsudaKageyu/minhook) (BSD 2-Clause), see [THIRD_PARTY_NOTICES.txt](THIRD_PARTY_NOTICES.txt). Enemy names in the Enemies tab come from the community project [Paramdex](https://github.com/soulsmods/Paramdex).
