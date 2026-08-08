#include "capture.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static PFN_WindivertOpen pWinDivertOpen = NULL;
static PFN_WindivertClose pWinDivertClose = NULL;
static PFN_WindivertRecv pWinDivertRecv = NULL;
static PFN_WindivertSend pWinDivertSend = NULL;

veto_capture_ctx *veto_capture_create(veto_capture_filter filter) {
    veto_capture_ctx *ctx = calloc(1, sizeof(veto_capture_ctx));
    if (!ctx) return NULL;
    ctx->filter = filter;
    ctx->windivert_dll = NULL;
    ctx->windivert_handle = INVALID_HANDLE_VALUE;
    return ctx;
}

void veto_capture_destroy(veto_capture_ctx *ctx) {
    if (!ctx) return;
    if (ctx->windivert_handle != INVALID_HANDLE_VALUE) {
        if (pWinDivertClose) pWinDivertClose(ctx->windivert_handle);
    }
    if (ctx->windivert_dll) {
        FreeLibrary(ctx->windivert_dll);
    }
    free(ctx);
}

static bool load_windivert(veto_capture_ctx *ctx) {
    if (ctx->windivert_dll) return true;

    ctx->windivert_dll = LoadLibraryA("WinDivert.dll");
    if (!ctx->windivert_dll) {
        fprintf(stderr, "[ERROR] Cannot load WinDivert.dll (error %lu)\n", GetLastError());
        fprintf(stderr, "[HINT]  Ensure WinDivert.dll is in the program directory\n");
        return false;
    }

    pWinDivertOpen = (PFN_WindivertOpen)GetProcAddress(ctx->windivert_dll, "WinDivertOpen");
    pWinDivertClose = (PFN_WindivertClose)GetProcAddress(ctx->windivert_dll, "WinDivertClose");
    pWinDivertRecv = (PFN_WindivertRecv)GetProcAddress(ctx->windivert_dll, "WinDivertRecv");
    pWinDivertSend = (PFN_WindivertSend)GetProcAddress(ctx->windivert_dll, "WinDivertSend");

    if (!pWinDivertOpen || !pWinDivertClose || !pWinDivertRecv || !pWinDivertSend) {
        fprintf(stderr, "[ERROR] WinDivert API functions not found\n");
        FreeLibrary(ctx->windivert_dll);
        ctx->windivert_dll = NULL;
        return false;
    }

    return true;
}

static HANDLE open_windivert(veto_capture_ctx *ctx) {
    char filter_str[256] = {0};

    if (ctx->filter & CAPTURE_FILTER_TCP_80) {
        strcat(filter_str, "tcp.DstPort == 80 || tcp.SrcPort == 80");
    }
    if (ctx->filter & CAPTURE_FILTER_TCP_443) {
        if (filter_str[0]) strcat(filter_str, " || ");
        strcat(filter_str, "tcp.DstPort == 443 || tcp.SrcPort == 443");
    }
    if (ctx->filter & CAPTURE_FILTER_UDP_443) {
        if (filter_str[0]) strcat(filter_str, " || ");
        strcat(filter_str, "udp.DstPort == 443 || udp.SrcPort == 443");
    }

    if (filter_str[0] == 0) return INVALID_HANDLE_VALUE;

    HANDLE h = pWinDivertOpen(filter_str, WINDIVERT_LAYER_NETWORK, 0, 0);
    if (h == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        fprintf(stderr, "[ERROR] WinDivertOpen failed (error %lu)\n", err);
        if (err == 5) {
            fprintf(stderr, "[HINT]  Access denied - run as Administrator\n");
        }
    }
    return h;
}

veto_status veto_capture_start(veto_capture_ctx *ctx, veto_capture_callback cb, void *user_ctx) {
    if (!ctx || !cb) return VETO_ERR_INVALID;

    if (!load_windivert(ctx)) return VETO_ERR_IO;

    ctx->windivert_handle = open_windivert(ctx);
    if (ctx->windivert_handle == INVALID_HANDLE_VALUE) {
        return VETO_ERR_IO;
    }

    ctx->callback = cb;
    ctx->user_ctx = user_ctx;
    ctx->running = true;

    return VETO_OK;
}

void veto_capture_stop(veto_capture_ctx *ctx) {
    if (!ctx) return;
    ctx->running = false;
    if (ctx->windivert_handle != INVALID_HANDLE_VALUE && pWinDivertClose) {
        pWinDivertClose(ctx->windivert_handle);
        ctx->windivert_handle = INVALID_HANDLE_VALUE;
    }
}

veto_status veto_capture_inject(veto_capture_ctx *ctx, const uint8_t *data, size_t len, const WINDIVERT_ADDRESS_PACKED *addr) {
    if (!ctx || !data || len == 0) return VETO_ERR_INVALID;
    if (ctx->windivert_handle == INVALID_HANDLE_VALUE) return VETO_ERR_IO;

    WINDIVERT_ADDRESS_PACKED inject_addr;
    if (addr) {
        inject_addr = *addr;
    } else {
        memset(&inject_addr, 0, sizeof(inject_addr));
    }

    if (pWinDivertSend(ctx->windivert_handle, (PVOID)data, (UINT)len, NULL, &inject_addr)) {
        ctx->packets_injected++;
        return VETO_OK;
    }
    return VETO_ERR_IO;
}

void veto_capture_run(veto_capture_ctx *ctx) {
    if (!ctx || !ctx->callback || !pWinDivertRecv) return;

    uint8_t packet[65535];
    UINT packet_len;
    WINDIVERT_ADDRESS_PACKED addr;
    DWORD last_stats = GetTickCount();

    while (ctx->running) {
        packet_len = sizeof(packet);
        if (!pWinDivertRecv(ctx->windivert_handle, packet, sizeof(packet),
                           &packet_len, &addr)) {
            continue;
        }
        ctx->packets_captured++;

        veto_packet *pkt = veto_packet_parse(packet, packet_len);
        if (pkt) {
            if (addr.Outbound) {
                pkt->meta.dir = DIR_OUTGOING;
            } else {
                pkt->meta.dir = DIR_INCOMING;
            }
            ctx->callback(pkt, &addr, ctx->user_ctx);
        } else {
            pWinDivertSend(ctx->windivert_handle, (PVOID)packet, (UINT)packet_len, NULL, &addr);
            ctx->packets_injected++;
        }

        DWORD now = GetTickCount();
        if (now - last_stats >= 3000) {
            printf("\r  Captured: %llu  Injected: %llu   ",
                   (unsigned long long)ctx->packets_captured,
                   (unsigned long long)ctx->packets_injected);
            fflush(stdout);
            last_stats = now;
        }
    }
}
