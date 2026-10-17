#pragma once

// DirectX 12 overlay: hooks the game's swap chain to draw the ImGui menu on top of the game,
// and the window procedure / DirectInput to feed and isolate input while the menu is open.
namespace zz::overlay {

bool Install();
bool MenuOpen();
void SetMenuOpen(bool open);

}  // namespace zz::overlay
