#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef enum { WlMgrState_Stopped, WlMgrState_Stopping, WlMgrState_Idle,
    WlMgrState_Scanning, WlMgrState_Associating, WlMgrState_Associated } WlMgrState;
typedef enum { WfcStatus_Disconnected, WfcStatus_Scanning, WfcStatus_Connecting,
    WfcStatus_AcquiringIP, WfcStatus_Connected } WfcStatus;
enum { WfcConnType_WpaNormal = 0x10, WfcWpaMode_WPA2_PSK_AES = 7 };
typedef union { uint8_t wpa_psk[32]; } WlanAuthData;
typedef struct {
    char ssid[32];
    uint32_t ipv4_addr, ipv4_gateway, ipv4_dns[2];
    uint16_t ssid_len, mtu;
    uint8_t conn_type;
} WfcConnSlot;
typedef struct { WfcConnSlot base; uint8_t wpa_pmk[32]; char wpa_psk[64]; uint8_t wpa_mode; } WfcConnSlotEx;

WlMgrState wlmgrGetState(void);
void wlmgrStop(void);
bool wlmgrLastCmdFailed(void);
WfcStatus wfcGetStatus(void);
void wfcClearConnSlots(void);
void wfcLoadFromNvram(void);
unsigned wfcGetNumSlots(void);
bool wfcDeriveWpaKey(WlanAuthData *out, const char *ssid, unsigned ssidLength, const char *key, unsigned keyLength);
bool wfcLoadSlotEx(const WfcConnSlotEx *slot);
bool wfcBeginAutoConnect(void);
