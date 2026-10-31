#pragma once
#include "app.h"

#define COVER_SIZE 112
#define COVER_PIXELS (COVER_SIZE * COVER_SIZE)
const uint16_t *coverFind(const Game *game, bool *known);
uint32_t coverRevision(const uint16_t *pixels);

bool coverLoad(const Game *game);

void coverPage(const Game *games, unsigned count, unsigned selected);

bool coverPrefetchTick(unsigned selected);

void coverPausePrefetch(void);
#ifdef ROMSFINDER_UI_TEST
void coverTestFixture(const Game *game);
void coverTestMissingFixture(const Game *game);
#endif
