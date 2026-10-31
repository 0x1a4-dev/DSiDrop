#pragma once
#include <nds.h>

void uiInit(PrintConsole *top, PrintConsole *bottom, Keyboard *keyboard);
void uiKeyboardSync(void);
void uiClear(bool mainDisplay);
void uiPanel(bool mainDisplay, int x, int y, int width, int height, unsigned color);
void uiIcon(bool mainDisplay, int x, int y);
void uiMeter(unsigned percent);

void uiInvalidate(bool mainDisplay);
void uiPresent(void);
void uiButton(int x, int y, int width, int height, unsigned color);
bool uiTouchFeedback(int x, int y);
void uiCover(const u16 *pixels);
void uiTopCover(const u16 *pixels);
void uiTopNeighbours(const u16 *left, const u16 *right);
void uiCoverTransition(int direction);
void uiAnimate(void);
typedef enum {
    UI_CATALOG, UI_SD, UI_TRANSFER, UI_SETTINGS, UI_SAVE, UI_SEARCH, UI_RANDOM, UI_FILTER, UI_BACK,
    UI_WIFI, UI_SERVER, UI_UPDATE, UI_REFRESH, UI_DELETE, UI_CHECK, UI_CLOSE, UI_DOWNLOAD,
    UI_ADD, UI_PREVIEW, UI_FAVORITE, UI_PAUSE, UI_PLAY, UI_INFO, UI_RESTORE, UI_AUTO, UI_SYMBOL_COUNT
} UiSymbol;
void uiSymbol(bool mainDisplay, int x, int y, UiSymbol symbol, unsigned color);
void uiFeatureBackdrop(void);
void uiHeading(bool mainDisplay, const char *text, int x, int y, int width);
void uiCaption(bool mainDisplay, const char *text, int x, int y, int width, unsigned color);
void uiCenteredCaption(bool mainDisplay, const char *text, int x, int y, int width, unsigned color);
void uiCenteredText(bool mainDisplay, const char *text, int x, int y, int width, unsigned maxLines);
void uiListRow(int y, const char *text, unsigned number, bool selected);
bool uiThemePreview(const u16 *pixels);
void uiThemePreviewEnd(void);
void uiTitle(const char *text, int x, int y, int width, unsigned maxLines);
void uiChevron(int x, int y);
void uiText(bool mainDisplay, const char *text, int x, int y, int width, unsigned maxLines);
unsigned uiTextWidth(const char *text);
void uiBrand(bool includeLogo);
void uiStageMark(int x, int y, unsigned state);
void uiBusy(unsigned phase);
bool uiIntroBegin(void);
#define UI_INTRO_FRAMES 192
void uiIntroFrame(unsigned frame);
void uiIntroEnd(void);
