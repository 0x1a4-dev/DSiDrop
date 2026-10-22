#include "app.h"
#include <wfc.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

static bool dsi, stopFails, cancelYield, keepStopping, startWorks, deriveWorks, loadWorks;
static WlMgrState managerState;
static WfcStatus connectionState;
static unsigned clears, nvramReads, slots, slotLoads, derivations, starts, stops, yields, savedSlots;
static WfcConnSlotEx loadedSlot;
static char derivedSsid[33], derivedPassword[65];

bool isDSiMode(void) { return dsi; }
WlMgrState wlmgrGetState(void) { return managerState; }
WfcStatus wfcGetStatus(void) { return connectionState; }
bool wlmgrLastCmdFailed(void) { return stopFails; }
void wlmgrStop(void) { stops++; managerState = WlMgrState_Stopping; }
bool appYield(void) {
    yields++;
    if (!keepStopping && yields >= 2) { managerState = WlMgrState_Stopped; connectionState = WfcStatus_Disconnected; }
    return !cancelYield;
}
void wfcClearConnSlots(void) { assert(managerState == WlMgrState_Stopped); clears++; slots = 0; }
void wfcLoadFromNvram(void) { nvramReads++; slots = savedSlots; }
unsigned wfcGetNumSlots(void) { return slots; }
bool wfcLoadSlotEx(const WfcConnSlotEx *slot) {
    slotLoads++; loadedSlot = *slot;
    if (loadWorks) slots++;
    return loadWorks;
}
bool wfcDeriveWpaKey(WlanAuthData *out, const char *ssid, unsigned ssidLength, const char *key, unsigned keyLength) {
    assert(ssidLength <= 32 && keyLength <= 63);
    memcpy(derivedSsid, ssid, ssidLength); derivedSsid[ssidLength] = 0;
    memcpy(derivedPassword, key, keyLength); derivedPassword[keyLength] = 0;
    for (unsigned i = 0; i < 32; i++) out->wpa_psk[i] = (uint8_t)(i + 20);
    derivations++;
    return deriveWorks;
}
bool wfcBeginAutoConnect(void) { starts++; return startWorks; }

static void reset(void) {
    dsi = startWorks = deriveWorks = loadWorks = true;
    stopFails = cancelYield = keepStopping = false;
    clears = nvramReads = slots = slotLoads = derivations = starts = stops = yields = 0;
    managerState = WlMgrState_Stopped; connectionState = WfcStatus_Disconnected;
    savedSlots = 2;
    memset(&loadedSlot, 0, sizeof(loadedSlot));
    memset(derivedSsid, 0, sizeof(derivedSsid)); memset(derivedPassword, 0, sizeof(derivedPassword));
}

static bool readConfig(const char *text, Settings *settings) {
    FILE *file = tmpfile();
    assert(file);
    fputs(text, file); rewind(file);
    char error[192] = "";
    bool result = settingsRead(file, settings, error, sizeof(error));
    fclose(file);
    return result;
}

int main(void) {
    Settings options = { .host = DEFAULT_HOST, .port = DEFAULT_PORT, .wifiMode = WIFI_WPA2,
        .ssid = "My WiFi #= ", .password = " pass#word= " };
    char error[192] = "";
    assert(wifiSettingsValid(&options, error, sizeof(error)));

    FILE *file = tmpfile();
    assert(file && settingsWrite(file, &options)); rewind(file);
    Settings parsed = {0};
    assert(settingsRead(file, &parsed, error, sizeof(error))); fclose(file);
    assert(!strcmp(parsed.ssid, options.ssid) && !strcmp(parsed.password, options.password));
    assert(!strcmp(parsed.host, options.host) && parsed.port == options.port && parsed.wifiMode == WIFI_WPA2);
    assert(readConfig("\xef\xbb\xbfhost=192.168.1.8\r\nport=8080\r\nwifi_mode=wpa2\r\nwifi_ssid=Test\r\nwifi_password=Secret#= \r\n", &parsed));
    assert(!strcmp(parsed.password, "Secret#= ") && parsed.port == 8080);
    assert(!readConfig("wifi_mode=wep\n", &parsed));
    assert(!readConfig("port=65536\n", &parsed));
    char overlong[100] = "wifi_password=";
    memset(overlong + 14, 'a', 65); overlong[79] = '\n'; overlong[80] = 0;
    assert(!readConfig(overlong, &parsed));
    memset(options.ssid, 's', 32); options.ssid[32] = 0;
    memset(options.password, 'p', 63); options.password[63] = 0;
    reset();
    assert(wifiBegin(&options, error, sizeof(error)));
    assert(clears == 1 && nvramReads == 0 && slots == 1 && starts == 1 && slotLoads == 1);
    assert(derivations == 1 && strlen(derivedSsid) == 32 && strlen(derivedPassword) == 63);
    assert(!memcmp(loadedSlot.base.ssid, options.ssid, 32) && loadedSlot.base.ssid_len == 32);
    assert(loadedSlot.base.conn_type == WfcConnType_WpaNormal && loadedSlot.wpa_mode == WfcWpaMode_WPA2_PSK_AES);
    assert(loadedSlot.base.ipv4_addr == 0 && loadedSlot.base.ipv4_gateway == 0 && loadedSlot.base.ipv4_dns[0] == 0);
    assert(!loadedSlot.wpa_psk[0]);
    for (unsigned i = 0; i < 32; i++) assert(loadedSlot.wpa_pmk[i] == i + 20);

    strcpy(options.password, "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f");
    reset();
    assert(wifiBegin(&options, error, sizeof(error)) && derivations == 0);
    for (unsigned i = 0; i < 32; i++) assert(loadedSlot.wpa_pmk[i] == i);
    options.password[0] = 'G';
    reset();
    assert(!wifiBegin(&options, error, sizeof(error)) && !clears && !nvramReads && !starts);
    strcpy(options.password, "12345678");
    reset(); dsi = false;
    assert(!wifiBegin(&options, error, sizeof(error)) && !clears && !starts);
    reset(); managerState = WlMgrState_Associated; connectionState = WfcStatus_Connected;
    assert(wifiBegin(&options, error, sizeof(error)) && stops == 1 && yields == 2 && clears == 1);
    reset(); managerState = WlMgrState_Scanning; keepStopping = true;
    assert(!wifiBegin(&options, error, sizeof(error)) && yields == 300 && !clears);
    reset(); managerState = WlMgrState_Associated; cancelYield = true;
    assert(!wifiBegin(&options, error, sizeof(error)) && !clears && !starts);
    reset(); deriveWorks = false;
    assert(!wifiBegin(&options, error, sizeof(error)) && !starts && !nvramReads);
    reset(); loadWorks = false;
    assert(!wifiBegin(&options, error, sizeof(error)) && !starts && !nvramReads);
    reset(); startWorks = false;
    assert(!wifiBegin(&options, error, sizeof(error)) && !nvramReads);

    options.wifiMode = WIFI_FIRMWARE;
    reset();
    assert(wifiBegin(&options, error, sizeof(error)) && nvramReads == 1 && starts == 1 && !derivations && !slotLoads);
    reset(); savedSlots = 0;
    assert(!wifiBegin(&options, error, sizeof(error)) && !starts);
    puts("WPA2 config: INI round-trip, SSID/key boundaries, AES/DHCP profile, raw PSK, DS-mode rejection, reconnect/cancel/timeout and explicit firmware mode passed");
    return 0;
}
