#include "client.h"
#include <nds.h>
#include <dswifi9.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <errno.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <unistd.h>

typedef enum { POLL_WAIT, POLL_CONNECT, POLL_SEND, POLL_RECEIVE } PollPhase;
static struct {
    bool enabled, running, durable, confirmed, registered;
    char mac[13], message[96];
    char linkStatus[32], linkDetail[128];
    unsigned revision;
    ClientTask task;
    uint32_t received, size;
    PollPhase phase;
    int socket;
    unsigned interval, sent, length, used;
    uint64_t due, deadline;
    char request[1400], response[2048];
} client = { .socket = -1, .interval = 3000, .linkStatus = "Not connected",
    .linkDetail = "Connect Wi-Fi to register with your PC." };

static uint64_t milliseconds(void) { return tickGetCount() * 1000 / TICK_FREQ; }

static bool transient(void) {
    return errno == EWOULDBLOCK || errno == EAGAIN || errno == EINPROGRESS || errno == EALREADY;
}

static void disconnect(void) {
    if (client.socket >= 0) forceclosesocket(client.socket);
    client.socket = -1; client.phase = POLL_WAIT;
}

static void linkStatus(bool registered, const char *status, const char *detail) {
    if (client.registered != registered || strcmp(client.linkStatus, status) || strcmp(client.linkDetail, detail)) client.revision++;
    client.registered = registered;
    snprintf(client.linkStatus, sizeof(client.linkStatus), "%s", status);
    snprintf(client.linkDetail, sizeof(client.linkDetail), "%s", detail);
}

bool clientRegistered(void) { return client.registered; }
const char *clientConnectionStatus(void) { return client.linkStatus; }
const char *clientConnectionDetail(void) { return client.linkDetail; }
const char *clientMac(void) { return *client.mac ? client.mac : "Unavailable"; }
unsigned clientRevision(void) { return client.revision; }

void clientClose(void) {
    disconnect(); client.enabled = false;
    linkStatus(false, "Not connected", "Connect Wi-Fi to register with your PC.");
}

static void failed(const char *detail) {
    linkStatus(false, "Retrying connection", detail);
    disconnect();
}

static void socketFailed(const char *operation) {
    char detail[128];
    snprintf(detail, sizeof(detail), "%s failed (%d). Check PC IP and server.", operation, errno);
    failed(detail);
}

static bool save(void) {
    if (!*client.task.task) {
        remove(CLIENT_PATH); remove(CLIENT_PATH ".tmp"); remove(CLIENT_PATH ".bak"); return true;
    }
    if (mkdir("/_nds", 0777) && errno != EEXIST) return false;
    FILE *file = fopen(CLIENT_PATH ".tmp", "w");
    if (!file) return false;
    bool ok = clientTaskWrite(file, &client.task, &settings, client.mac);
    if (fclose(file)) ok = false;
    if (!ok) return false;
    struct stat info;
    bool previous = stat(CLIENT_PATH, &info) == 0;
    if (previous) {
        remove(CLIENT_PATH ".bak");
        if (rename(CLIENT_PATH, CLIENT_PATH ".bak")) return false;
    }
    if (rename(CLIENT_PATH ".tmp", CLIENT_PATH)) {
        if (previous) rename(CLIENT_PATH ".bak", CLIENT_PATH);
        return false;
    }
    remove(CLIENT_PATH ".bak"); return true;
}

static bool identity(void) {
    const uint8_t *mac = g_envExtraInfo->wlmgr_macaddr;
    unsigned sum = 0;
    for (unsigned i = 0; i < 6; i++) sum |= mac[i];
    if (!sum || (mac[0] & 1)) {
        linkStatus(false, "MAC not ready", "Waiting for the Wi-Fi MAC. START in Settings reconnects.");
        return false;
    }
    for (unsigned i = 0; i < 6; i++) snprintf(client.mac + i * 2, 3, "%02x", mac[i]);
    static const char *paths[] = { CLIENT_PATH, CLIENT_PATH ".tmp", CLIENT_PATH ".bak" };
    for (unsigned i = 0; i < 3; i++) {
        FILE *file = fopen(paths[i], "r");
        if (!file) continue;
        bool ok = clientTaskRead(file, &client.task, &settings, client.mac);
        fclose(file);
        if (ok) break;
        memset(&client.task, 0, sizeof(client.task));
    }
    return true;
}

void clientConnect(void) {
    clientClose();
    memset(&client.task, 0, sizeof(client.task));
    client.running = client.confirmed = false; client.durable = true; client.received = client.size = 0; client.message[0] = 0;
    client.mac[0] = 0; client.enabled = true; client.due = 0;
    linkStatus(false, "Registering...", "Sending this DSi's MAC to your PC.");
    identity();
}

static bool makePath(bool mayClaim, char *path, size_t capacity) {
    struct in_addr ip = { .s_addr = Wifi_GetIP() };
    char address[20], encoded[288];
    snprintf(address, sizeof(address), "%s", inet_ntoa(ip));
    urlEncode(client.message, encoded, sizeof(encoded));
    static const char *states[] = { "downloading", "done", "error", "canceled" };
    const char *state = *client.task.task ? states[client.task.status] : mayClaim ? "idle" : "busy";
    int n = snprintf(path, capacity, "/api/dsi/client/poll?mac=%s&ip=%s&version=" APP_VERSION
        "&task=%s&state=%s&received=%lu&size=%lu&message=%s", client.mac, address,
        *client.task.task ? client.task.task : "-", state, (unsigned long)client.received, (unsigned long)client.size, encoded);
    return n > 0 && n < (int)capacity;
}

static bool start(bool mayClaim, uint64_t now) {
    char path[640];
    if (!makePath(mayClaim, path, sizeof(path))) { failed("Registration request is too large."); return false; }
    int n = snprintf(client.request, sizeof(client.request), "POST %s HTTP/1.1\r\nHost: %s:%u\r\nUser-Agent: " APP_NAME "/" APP_VERSION
        "\r\nAccept-Encoding: identity\r\nConnection: close\r\nContent-Length: 0\r\n\r\n", path, settings.host, settings.port);
    if (n < 0 || n >= (int)sizeof(client.request)) { failed("Registration request is too large."); return false; }
    struct sockaddr_in server = { .sin_family = AF_INET, .sin_port = htons(settings.port) };
    if (!inet_aton(settings.host, &server.sin_addr)) { failed("Invalid PC IPv4. Change it in Settings (X)."); return false; }
    client.socket = socket(AF_INET, SOCK_STREAM, 0);
    if (client.socket < 0) { socketFailed("Socket"); return false; }
    unsigned long nonblock = 1;
    if (ioctl(client.socket, FIONBIO, &nonblock) < 0) { socketFailed("Socket setup"); return false; }
    int result = connect(client.socket, (struct sockaddr *)&server, sizeof(server));
    if (result < 0 && !transient()) { socketFailed("PC connection"); return false; }
    client.length = n; client.sent = client.used = 0;
    client.phase = POLL_CONNECT; client.deadline = now + 15000; return true;
}

static int responseFrame(void) {
    char *end = strstr(client.response, "\r\n\r\n");
    if (!end) return 0;
    int status;
    if (sscanf(client.response, "HTTP/%*u.%*u %d", &status) != 1) return -1;
    if (status != 200) {
        char detail[128];
        snprintf(detail, sizeof(detail), "HTTP %d. %s", status, status == 404 || status == 405 ?
            "Update/restart the PC server, then reconnect." : "Check the PC server, then reconnect.");
        failed(detail); return -1;
    }
    bool hasLength = false; uint32_t length = 0;
    char *line = strstr(client.response, "\r\n");
    while (line && line < end) {
        line += 2; char *next = strstr(line, "\r\n");
        if (!next || next > end) return -1;
        if (!strncasecmp(line, "Content-Length:", 15)) {
            char *numberEnd; errno = 0; unsigned long n = strtoul(line + 15, &numberEnd, 10);
            while (*numberEnd == ' ' || *numberEnd == '\t') numberEnd++;
            if (hasLength || errno || numberEnd != next || n > 512 || !n) return -1;
            length = n; hasLength = true;
        }
        if (!strncasecmp(line, "Transfer-Encoding:", 18) || !strncasecmp(line, "Content-Encoding:", 17)) return -1;
        line = next;
    }
    if (!hasLength) return -1;
    unsigned body = end + 4 - client.response;
    if (client.used < body + length) return 0;
    return client.used == body + length ? (int)body : -1;
}

static bool acceptBody(char *body) {
    ClientReply reply;
    if (!clientParse(body, &reply)) { failed("Invalid PC reply. Update/restart the PC server."); return false; }
    client.interval = reply.interval;
    linkStatus(true, "Registered", "This DSi is linked. Open Devices in the web app.");
    if (client.running && *client.task.task && client.task.status == CLIENT_DOWNLOADING) {

        return *reply.task.task && !strcmp(client.task.task, reply.task.task) &&
            client.task.game.system == reply.task.game.system && !strcmp(client.task.game.id, reply.task.game.id);
    }
    client.task = reply.task;
    client.confirmed = true;
    client.received = client.size = 0; client.message[0] = 0; client.running = false;
    client.durable = save();
    if (!client.durable) linkStatus(false, "SD checkpoint error", "Could not save the remote task. Check free SD space.");
    return client.durable;
}

bool clientRegister(void) {
    if (!client.enabled || (!*client.mac && !identity())) return false;
    disconnect();

    client.enabled = false;
    char path[640];
    bool ok = makePath(false, path, sizeof(path)) && httpText("POST", path, client.response, sizeof(client.response));
    if (ok) ok = acceptBody(client.response);
    else linkStatus(false, "Registration failed", *lastError ? lastError : "Check the PC IP and restart the server.");
    client.enabled = true; client.due = milliseconds() + client.interval;
    return ok;
}

void clientTick(bool mayClaim) {
    if (!client.enabled) return;
    uint64_t now = milliseconds();
    if (Wifi_AssocStatus() != ASSOCSTATUS_ASSOCIATED) {
        failed("Wi-Fi disconnected. START in Settings reconnects."); client.due = now + 1000; return;
    }
    if (!*client.mac) {
        if (now < client.due) return;
        client.due = now + 1000;
        if (!identity()) return;
    }
    if (!client.durable) {
        client.durable = save();
        if (!client.durable) { linkStatus(false, "SD checkpoint error", "Could not save the remote task. Check free SD space."); return; }
    }
    if (client.phase == POLL_WAIT) {
        if (now < client.due) return;
        client.due = now + client.interval;
        if (!start(mayClaim, now)) disconnect();
        return;
    }
    if (now > client.deadline) { failed("PC response timed out. Check IP, server and firewall."); return; }
    if (client.phase == POLL_CONNECT) {
        struct sockaddr_in peer; socklen_t length = sizeof(peer);
        if (!getpeername(client.socket, (struct sockaddr *)&peer, &length)) { client.phase = POLL_SEND; client.deadline = now + 15000; }
        return;
    }
    if (client.phase == POLL_SEND) {
        int n = send(client.socket, client.request + client.sent, client.length - client.sent, 0);
        if (n > 0) { client.sent += n; client.deadline = now + 15000; if (client.sent == client.length) client.phase = POLL_RECEIVE; }
        else if (!n) failed("PC closed the registration request. Retrying.");
        else if (!transient()) socketFailed("Sending registration");
        return;
    }
    int n = recv(client.socket, client.response + client.used, sizeof(client.response) - client.used - 1, 0);
    if (n > 0) {
        client.used += n; client.response[client.used] = 0;
        client.deadline = now + 15000;
        int body = responseFrame();
        if (body > 0) { acceptBody(client.response + body); disconnect(); }
        else if (body < 0) { if (client.phase != POLL_WAIT) failed("Invalid HTTP reply. Update/restart the PC server."); }
        else if (client.used == sizeof(client.response) - 1) failed("PC response is too large. Update the PC server.");
    } else if (!n) failed("PC closed an incomplete reply. Retrying.");
    else if (!transient()) socketFailed("Receiving registration");
}

bool clientTake(Game *game) {
    if (!clientPending()) return false;
    *game = client.task.game; client.running = true; return true;
}

bool clientActive(void) { return client.running && *client.task.task && client.task.status == CLIENT_DOWNLOADING; }
bool clientPending(void) { return client.enabled && client.durable && client.confirmed && !client.running && *client.task.task && client.task.status == CLIENT_DOWNLOADING; }

void clientProgress(uint32_t received, uint32_t size, const char *message) {
    if (!clientActive()) return;
    client.received = received; client.size = size;
    snprintf(client.message, sizeof(client.message), "%.95s", message);
}

void clientFinish(bool success, bool canceled, const char *message) {
    if (!clientActive()) return;
    disconnect();
    client.task.status = success ? CLIENT_DONE : canceled ? CLIENT_CANCELED : CLIENT_ERROR;
    client.running = false;
    snprintf(client.message, sizeof(client.message), "%.95s", message);
    client.durable = save(); client.due = 0;
}
