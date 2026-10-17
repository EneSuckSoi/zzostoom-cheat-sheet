#pragma once

// Menu languages. UI text comes from lang/<code>.tsv (tools/gen_lang.py -> generated/ui_text.cpp);
// item, Site of Grace and game-term names come from the game's own text in that language
// (tools/extract_items.py -> generated/game_text.cpp).
namespace zz::i18n {

enum Lang {
    kEnglish, kGerman, kFrench, kSpanish, kPortuguese, kRussian,
    kJapanese, kKorean, kChinese, kTurkish, kLangCount
};

struct LangInfo {
    const char* code;       // ini value and lang/<code>.tsv
    const char* name;       // shown in the language list, in that language
    const char* game_text;  // game msg folder (deude...), nullptr = English item names
};

const LangInfo& Info(int lang);
int Language();
void SetLanguage(int lang);
int FromCode(const char* code);  // -1 if unknown

// English text -> current language (the English text itself if there is no translation).
// Format strings keep their %d / %s in the same order.
const char* T(const char* english);

// Item / grace names in the current language (English when the game has none, or when
// "English item names" is on).
void SetEnglishItemNames(bool on);
const char* ItemName(int item);
const char* ItemInfo(int item);
const char* GraceName(int grace);

// Generated tables -------------------------------------------------------------------------

struct GameText {
    const char* lang;
    const char* const* items;
    const char* const* infos;
    const char* const* graces;
    const char* const* terms;  // translations of kGameTermKeys
};
extern const char* const kGameTermKeys[];
extern const int kGameTermCount;
extern const GameText kGameText[];
extern const int kGameTextCount;

extern const char* const kUiText[][kLangCount];  // [row][lang], [row][0] = English
extern const int kUiTextCount;

}  // namespace zz::i18n
