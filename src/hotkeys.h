#pragma once
#include <string>

#include "cheats.h"
#include "game.h"
#include "pad.h"

// Shortcuts that toggle a cheat or run an action without opening the menu.
// Each action can have a keyboard key and/or a controller combo (L3/R3 held + button).
namespace zz::hotkeys {

struct Action {
    const char* id;     // ini key
    const char* label;  // shown in the menu and in the on-screen notification
    bool cheats::State::*toggle;  // toggles this flag, or
    void (*run)();                // runs this
};

struct Binding {
    int key = 0;  // virtual key, 0 = none
    pad::Combo combo;
};

int Count();
const Action& GetAction(int index);
Binding& GetBinding(int index);

void Load();  // from the [hotkeys] section of zzostoom_cheat_sheet.ini (defaults if missing)
void Save();
void ResetDefaults();

// Input from the window procedure (any thread) and the controller (render thread).
void QueueKey(int vk);
void OnCombo(const pad::Combo& combo);
// Runs queued keyboard shortcuts; call once per frame on the render thread.
void ProcessQueued();

// Assigning a new shortcut from the menu.
void BeginCapture(int action, bool controller);
void CancelCapture();
int CaptureAction();  // -1 when not capturing
bool CaptureController();
bool CaptureKey(int vk);                  // true if the key was used by the capture
bool CaptureCombo(const pad::Combo& combo);

std::string KeyLabel(int vk);

// Saved position, shared by the movement tab and the shortcuts.
bool SavePosition();
bool LoadPosition();
bool HasSavedPosition();
const game::Transform& SavedPosition();

}  // namespace zz::hotkeys
