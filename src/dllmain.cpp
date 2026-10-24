// zzostoom cheat sheet - Elden Ring in-game cheat menu loaded by Mod Engine 3 as a native DLL.
#include <windows.h>

#include <algorithm>
#include <string>

#include "config.h"
#include "game.h"
#include "hotkeys.h"
#include "i18n.h"
#include "log.h"
#include "memory.h"
#include "overlay.h"

namespace {

HMODULE g_module = nullptr;

std::wstring DllDirectory() {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(g_module, path, MAX_PATH);
    std::wstring dir(path);
    return dir.substr(0, dir.find_last_of(L"\\/"));
}

BOOL CALLBACK FindWindowProc(HWND hwnd, LPARAM param) {
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid != GetCurrentProcessId() || !IsWindowVisible(hwnd) || GetWindow(hwnd, GW_OWNER))
        return TRUE;
    RECT rc{};
    GetClientRect(hwnd, &rc);
    if (rc.right - rc.left < 320) return TRUE;
    *reinterpret_cast<HWND*>(param) = hwnd;
    return FALSE;
}

HWND FindGameWindow() {
    HWND found = nullptr;
    EnumWindows(FindWindowProc, reinterpret_cast<LPARAM>(&found));
    return found;
}

DWORD WINAPI MainThread(LPVOID) {
    const std::wstring dir = DllDirectory();
    zz::log::Init(dir);
    zz::mem::Init();
    ZZ_LOG("zzostoom cheat sheet starting");
    zz::config::Load(dir);
    zz::i18n::SetLanguage(std::max(0, zz::i18n::FromCode(zz::config::Get().language.c_str())));
    zz::i18n::SetEnglishItemNames(zz::config::Get().english_item_names);
    zz::hotkeys::Load();

    // The game unpacks its code and creates its D3D12 device before the window shows up.
    HWND window = nullptr;
    for (int i = 0; i < 6000 && !(window = FindGameWindow()); ++i) Sleep(100);
    if (!window) {
        ZZ_LOG("Game window not found, giving up");
        return 0;
    }
    Sleep(1500);

    if (!zz::game::Init())
        ZZ_LOG("Some critical game addresses are missing; cheats/items will be disabled");
    if (!zz::overlay::Install()) ZZ_LOG("Overlay could not be installed");
    // Memory scans that would hitch the render thread are done here, once in game.
    while (!zz::game::InGame()) Sleep(1000);
    zz::game::WarmUp();
    return 0;
}

}  // namespace

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_module = module;
        if (HANDLE thread = CreateThread(nullptr, 0, MainThread, nullptr, 0, nullptr))
            CloseHandle(thread);
    }
    return TRUE;
}
