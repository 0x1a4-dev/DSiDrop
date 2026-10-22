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
void uiTitle(const char *text, int x, int y, int width, unsigned maxLines);
void uiChevron(int x, int y);
void uiText(bool mainDisplay, const char *text, int x, int y, int width, unsigned maxLines);
void uiBrand(bool includeLogo);
void uiStageMark(int x, int y, unsigned state);
void uiBusy(unsigned phase);
bool uiIntroBegin(void);
#define UI_INTRO_FRAMES 192
void uiIntroFrame(unsigned frame);
void uiIntroEnd(void);
