#include "app.h"
#include <string.h>
#include <stdlib.h>
#include <errno.h>

static bool copyValue(char *dest, size_t capacity, const char *value) {
    if (strlen(value) >= capacity) return false;
    strcpy(dest, value);
    return true;
}

bool settingsRead(FILE *file, Settings *options, char *error, size_t capacity) {
    Settings parsed = *options;
    char line[256];
    bool firstLine = true;
    while (fgets(line, sizeof(line), file)) {
        if (!strchr(line, '\n') && !feof(file)) {
            int c;
            while ((c = fgetc(file)) != '\n' && c != EOF) {}
            snprintf(error, capacity, "Configuration line is too long");
            return false;
        }
        line[strcspn(line, "\r\n")] = 0;
        char *key = line;
        if (firstLine && !strncmp(key, "\xef\xbb\xbf", 3)) key += 3;
        firstLine = false;
        if (!*key || *key == '#' || *key == ';') continue;
        char *value = strchr(key, '=');
        if (!value) continue;
        *value++ = 0;
        bool ok = true;
        if (!strcmp(key, "host")) ok = *value && copyValue(parsed.host, sizeof(parsed.host), value);
        else if (!strcmp(key, "port")) {
            char *end;
            errno = 0;
            unsigned long port = strtoul(value, &end, 10);
            ok = *value && !*end && !errno && port > 0 && port <= 65535;
            if (ok) parsed.port = port;
        } else if (!strcmp(key, "wifi_mode")) {
            if (!strcmp(value, "wpa2")) parsed.wifiMode = WIFI_WPA2;
            else if (!strcmp(value, "firmware")) parsed.wifiMode = WIFI_FIRMWARE;
            else ok = false;
        } else if (!strcmp(key, "wifi_ssid")) ok = copyValue(parsed.ssid, sizeof(parsed.ssid), value);
        else if (!strcmp(key, "wifi_password")) ok = copyValue(parsed.password, sizeof(parsed.password), value);
        if (!ok) {

            snprintf(error, capacity, "Invalid INI field: %.40s", key);
            return false;
        }
    }
    if (ferror(file)) { snprintf(error, capacity, "Could not read SD configuration"); return false; }
    *options = parsed;
    return true;
}

bool settingsWrite(FILE *file, const Settings *options) {

    return fprintf(file, "# " APP_NAME " " APP_VERSION "\nhost=%s\nport=%u\n"
        "wifi_mode=%s\nwifi_ssid=%s\nwifi_password=%s\n", options->host, options->port,
        options->wifiMode == WIFI_WPA2 ? "wpa2" : "firmware", options->ssid, options->password) > 0;
}

bool wifiSettingsValid(const Settings *options, char *error, size_t capacity) {
    if (options->wifiMode == WIFI_FIRMWARE) return true;
    if (options->wifiMode != WIFI_WPA2) {
        snprintf(error, capacity, "Invalid Wi-Fi mode"); return false;
    }
    size_t ssidLength = strlen(options->ssid), keyLength = strlen(options->password);
    if (!ssidLength || ssidLength > 32) {
        snprintf(error, capacity, "Network name (SSID): 1 to 32 bytes"); return false;
    }
    for (size_t i = 0; i < ssidLength; i++) {
        unsigned char c = (unsigned char)options->ssid[i];
        if (c < 32 || c == 127) { snprintf(error, capacity, "SSID contains control characters"); return false; }
    }
    if (keyLength == 64 && strspn(options->password, "0123456789abcdefABCDEF") == 64) return true;
    if (keyLength < 8 || keyLength > 63) {
        snprintf(error, capacity, "WPA2: 8-63 ASCII or 64 hex characters"); return false;
    }
    for (size_t i = 0; i < keyLength; i++) {
        unsigned char c = (unsigned char)options->password[i];
        if (c < 32 || c > 126) { snprintf(error, capacity, "WPA2 password: printable ASCII only"); return false; }
    }
    return true;
}
