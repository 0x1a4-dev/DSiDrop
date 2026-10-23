#pragma once
#include <stdbool.h>
#include <stdint.h>
#define TICK_FREQ 1000000U
bool isDSiMode(void);
uint64_t tickGetCount(void);
void threadSleep(unsigned usec);
