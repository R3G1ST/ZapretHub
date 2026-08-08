#ifndef VETO_CAPTURE_H
#define VETO_CAPTURE_H

#include "veto.h"
#include "packet.h"
#include <windows.h>

typedef enum {
    WINDIVERT_LAYER_NETWORK = 0,
    WINDIVERT_LAYER_NETWORK_FORWARD = 1,
} WINDIVERT_LAYER_ENUM;

#pragma pack(push, 1)
typedef struct {
    INT64  Timestamp;
    UINT32 Layer;
    UINT32 Event;
    UINT32 Sniffed;
    UINT32 Outbound;
    UINT32 Loopback;
    UINT32 Impostor;
    UINT32 IPv6;
    UINT32 IPChecksum;
    UINT32 TCPChecksum;
    UINT32 UDPChecksum;
    UINT32 Reserved1;
    UINT32 Reserved2;
    UINT8  Data[64];
} WINDIVERT_ADDRESS_PACKED;
#pragma pack(pop)

typedef enum {
    CAPTURE_FILTER_TCP_80   = (1 << 0),
    CAPTURE_FILTER_TCP_443  = (1 << 1),
    CAPTURE_FILTER_UDP_443  = (1 << 2),
    CAPTURE_FILTER_ALL      = (CAPTURE_FILTER_TCP_80 | CAPTURE_FILTER_TCP_443 | CAPTURE_FILTER_UDP_443)
} veto_capture_filter;

typedef void (*veto_capture_callback)(veto_packet *pkt, const WINDIVERT_ADDRESS_PACKED *addr, void *user_ctx);

typedef struct {
    HMODULE windivert_dll;
    HANDLE windivert_handle;
    veto_capture_filter filter;
    veto_capture_callback callback;
    void *user_ctx;
    volatile bool running;
    uint64_t packets_captured;
    uint64_t packets_injected;
} veto_capture_ctx;

typedef HANDLE (WINAPI *PFN_WindivertOpen)(const char*, WINDIVERT_LAYER_ENUM, INT16, UINT64);
typedef BOOL   (WINAPI *PFN_WindivertClose)(HANDLE);
typedef BOOL   (WINAPI *PFN_WindivertRecv)(HANDLE, VOID*, UINT, UINT*, WINDIVERT_ADDRESS_PACKED*);
typedef BOOL   (WINAPI *PFN_WindivertSend)(HANDLE, const VOID*, UINT, UINT*, WINDIVERT_ADDRESS_PACKED*);

veto_capture_ctx *veto_capture_create(veto_capture_filter filter);
void veto_capture_destroy(veto_capture_ctx *ctx);

veto_status veto_capture_start(veto_capture_ctx *ctx, veto_capture_callback cb, void *user_ctx);
void veto_capture_stop(veto_capture_ctx *ctx);
void veto_capture_run(veto_capture_ctx *ctx);
veto_status veto_capture_inject(veto_capture_ctx *ctx, const uint8_t *data, size_t len, const WINDIVERT_ADDRESS_PACKED *addr);

#endif
