// i18n.h — minimal zero-dependency internationalization (zh / en).
//
// UI strings are looked up by key through tr(); the active language is a
// process-global switch (toggle via the 语言 / Language menu, effective
// immediately — every tr() call re-reads the current language).
//
// Adding a string: add a key to enum Str (in order), then add BOTH languages
// in i18n.cpp at the same index. A static_assert keeps the tables aligned.
//
#pragma once

#include <string>
#include <vector>

namespace pxb {

enum class Lang { zh, en };

// UI string keys. Keep in sync with the tables in i18n.cpp.
enum class Str {
    // ---- menus ----
    MenuFile, MenuOpen, MenuQuit, MenuHelp, MenuAbout, MenuLang,
    LangZh, LangEn,
    // ---- file association (File menu) ----
    AssocRegistered, AssocUnregister, AssocNotRegistered, AssocRegister,
    AssocUserChoiceHint, AssocWinOnly, AssocSystemLevelLeftover,
    AssocHklmLeftoverDetail,
    AssocRegNotEffective, AssocThumbAdminNote,
    AssocUnregNotEffective, AssocHklmStillLingers, AssocHklmNeedsAdmin,
    AssocRegisterTitle, AssocUnregisterTitle,
    AssocRegisterOk, AssocUnregisterOk, AssocFailPrefix, AssocNoElevation,
    // ---- panels ----
    PanelInfo, PanelFrames, PanelLayers,
    // ---- preview empty states / hints ----
    EmptyNoFile1, EmptyNoFile2, EmptyNoFile3, EmptyNoPreview,
    ZoomFit,
    // ---- playback ----
    Play, Pause, Stop, FrameSliderFmt, FilmstripTooltip,
    TooltipExplicit, TooltipDefault, EmptyNoFrames,
    // ---- metadata labels ----
    MetaTitle, MetaGenerator, MetaVersion, MetaContentType, MetaCreatedAt,
    MetaUpdatedAt, MetaPalette, MetaPaletteVersion, MetaDimension,
    MetaCanvasFmt, MetaColorDepth, MetaCoordinate, MetaContainerFmt,
    MetaThumbIndexFmt, MetaFramesLayersFmt,
    // ---- layers ----
    LayersNone, LayerHidden, LayersNote, LayersNoteLive, LayerNoPreview,
    // ---- file list ----
    ListEmpty, SearchPlaceholder,
    // ---- status bar ----
    StatusReady, StatusNotPxb, StatusFailed, StatusLoadedFmt, StatusUnknownErr,
    StatusBarCacheFmt,
    // ---- about window ----
    AboutTitle, AboutName, AboutVersionFmt, AboutHomepage, AboutOpenHomepage,
    AboutCopyright, AboutAuthor, AboutLicense, LicenseValue,
    AboutPowered, AboutBuildFmt,
    AboutClose,
    // ---- open-file dialog ----
    DlgPxbFiles, DlgAllFiles,
    // ---- view menu / layout toggles ----
    MenuView, MenuFileList, MenuInfoLayers, MenuMinimap, PanelMinimap,
    // ---- theme ----
    MenuTheme, ThemeSystem, ThemeDark, ThemeLight,
    // ---- placeholder (no document) ----
    Count_
};

// Current language / switch.
Lang current_lang();
void set_lang(Lang l);

// Look up the string for the active language. Never returns nullptr.
const char* tr(Str s);

// All Simplified-Chinese strings (for the font loader: every UI string the
// language can produce must exist in the font atlas).
std::vector<std::string> zh_strings();

// Wide variant — only needed for the native file dialog filter on Windows.
const wchar_t* trw(Str s);

// App identity (used by the About window and the OS window title).
inline constexpr const char* kAppName    = "PXB Preview";
inline constexpr const char* kAppVersion = "0.0.2";
inline constexpr const char* kHomepage   = "https://px.mzb.one";

// About-window fields that may need user confirmation before shipping.
// (The license value is localized via Str::LicenseValue, not a constant —
// it must follow the active UI language.)
inline constexpr const char* kAppCopyright = "Copyright (c) 2026 https://px.mzb.one";
inline constexpr const char* kAppAuthor    = "MZB";

} // namespace pxb
