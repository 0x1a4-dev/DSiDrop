#pragma once
#include "app.h"

#define CLIENT_PATH "/_nds/dsidrop.client"

typedef enum { CLIENT_DOWNLOADING, CLIENT_DONE, CLIENT_ERROR, CLIENT_CANCELED } ClientStatus;
typedef struct {
    char task[33];
    Game game;
    ClientStatus status;
} ClientTask;
typedef struct { unsigned interval; ClientTask task; } ClientReply;

bool clientParse(char *text, ClientReply *reply);
bool clientTaskWrite(FILE *file, const ClientTask *task, const Settings *options, const char *mac);
bool clientTaskRead(FILE *file, ClientTask *task, const Settings *options, const char *mac);

void clientConnect(void);

bool clientRegister(void);
bool clientRegistered(void);
const char *clientConnectionStatus(void);
const char *clientConnectionDetail(void);
const char *clientMac(void);
unsigned clientRevision(void);
void clientTick(bool mayClaim);
bool clientTake(Game *game);
bool clientActive(void);
bool clientPending(void);
void clientProgress(uint32_t received, uint32_t size, const char *message);
void clientFinish(bool success, bool canceled, const char *message);
void clientClose(void);
