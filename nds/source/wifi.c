#include "app.h"
#include <nds.h>
#include <wfc.h>
#include <string.h>

static unsigned hexDigit(char c) {
    if (c >= '0' && c <= '9') return (unsigned)(c - '0');
    if (c >= 'a' && c <= 'f') return (unsigned)(c - 'a' + 10);
    return (unsigned)(c - 'A' + 10);
}

bool wifiBegin(const Settings *options, char *error, size_t capacity) {
    if (!isDSiMode()) {
        snprintf(error, capacity, "WPA2 requires DSi mode in TWiLight Menu++"); return false;
    }
    if (!wifiSettingsValid(options, error, capacity)) return false;

    if (wlmgrGetState() != WlMgrState_Stopped) {
        wlmgrStop();
        unsigned frames = 0;
        do {
            if (!appYield()) { snprintf(error, capacity, "Wi-Fi connection cancelled"); return false; }
            if (++frames >= 300 || wlmgrLastCmdFailed()) {
                snprintf(error, capacity, "Wi-Fi not ready. SELECT > START reconnects."); return false;
            }
        } while (wlmgrGetState() != WlMgrState_Stopped || wfcGetStatus() != WfcStatus_Disconnected);
    }
    wfcClearConnSlots();
    if (options->wifiMode == WIFI_FIRMWARE) {
        wfcLoadFromNvram();
        if (!wfcGetNumSlots()) {
            snprintf(error, capacity, "No DSi Wi-Fi profile. SELECT: set up your WPA2 network."); return false;
        }
    } else {
        WfcConnSlotEx slot = {0};
        WlanAuthData auth = {0};
        size_t ssidLength = strlen(options->ssid), keyLength = strlen(options->password);
        if (keyLength == 64) {

            for (unsigned i = 0; i < 32; i++)
                auth.wpa_psk[i] = (hexDigit(options->password[i * 2]) << 4) | hexDigit(options->password[i * 2 + 1]);
        } else if (!wfcDeriveWpaKey(&auth, options->ssid, ssidLength, options->password, keyLength)) {
            snprintf(error, capacity, "Could not calculate the WPA2 key"); return false;
        }
        slot.base.conn_type = WfcConnType_WpaNormal;
        slot.base.ssid_len = ssidLength;
        slot.base.mtu = 1400;
        memcpy(slot.base.ssid, options->ssid, ssidLength);
        slot.wpa_mode = WfcWpaMode_WPA2_PSK_AES;
        memcpy(slot.wpa_pmk, auth.wpa_psk, sizeof(slot.wpa_pmk));

        bool loaded = wfcLoadSlotEx(&slot);
        memset(&slot, 0, sizeof(slot));
        memset(&auth, 0, sizeof(auth));
        if (!loaded) { snprintf(error, capacity, "Could not load your WPA2 profile"); return false; }
    }

    if (!wfcBeginAutoConnect()) {
        snprintf(error, capacity, "Could not start the Wi-Fi connection"); return false;
    }
    return true;
}
