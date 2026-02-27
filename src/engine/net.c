/*
 * net.c -- Network subsystem with loopback for single-player
 *
 * FAKK2 networking is based on Q3's UDP client/server model.
 * Original imported 17 functions from WSOCK32.DLL.
 *
 * For single-player, client and server run in the same process.
 * A loopback message queue passes data between them without sockets.
 *
 * The loopback system maintains two queues:
 *   - Client-to-server: client commands (usercmd, chat, etc.)
 *   - Server-to-client: snapshots, configstrings, game state
 */

#include "qcommon.h"
#include <string.h>

#ifdef PLATFORM_WINDOWS
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#endif

/* =========================================================================
 * Loopback message queue
 *
 * Two directions: client->server (index 0) and server->client (index 1).
 * Each direction is a ring buffer of messages.
 * ========================================================================= */

#define MAX_LOOPBACK_MSGS   16
#define MAX_LOOPBACK_SIZE   16384   /* max bytes per message */

typedef struct {
    byte    data[MAX_LOOPBACK_SIZE];
    int     dataLen;
} loopbackMsg_t;

typedef struct {
    loopbackMsg_t   msgs[MAX_LOOPBACK_MSGS];
    int             sendIndex;
    int             recvIndex;
} loopbackQueue_t;

/* Index 0 = client-to-server, 1 = server-to-client */
static loopbackQueue_t  loopback[2];

static qboolean net_initialized = qfalse;

/* =========================================================================
 * Loopback operations
 * ========================================================================= */

static void NET_SendLoopback(int queueIdx, const void *data, int length) {
    if (length <= 0 || length > MAX_LOOPBACK_SIZE) return;

    loopbackQueue_t *q = &loopback[queueIdx];
    int idx = q->sendIndex & (MAX_LOOPBACK_MSGS - 1);

    memcpy(q->msgs[idx].data, data, length);
    q->msgs[idx].dataLen = length;
    q->sendIndex++;
}

static qboolean NET_GetLoopback(int queueIdx, msg_t *msg) {
    loopbackQueue_t *q = &loopback[queueIdx];

    if (q->recvIndex >= q->sendIndex) {
        return qfalse; /* nothing pending */
    }

    int idx = q->recvIndex & (MAX_LOOPBACK_MSGS - 1);
    loopbackMsg_t *lm = &q->msgs[idx];

    /* Copy into the provided msg buffer */
    if (msg->maxsize < lm->dataLen) {
        q->recvIndex++;
        return qfalse; /* buffer too small */
    }

    memcpy(msg->data, lm->data, lm->dataLen);
    msg->cursize = lm->dataLen;
    msg->readcount = 0;
    msg->bit = 0;

    q->recvIndex++;
    return qtrue;
}

/* =========================================================================
 * Initialization
 * ========================================================================= */

void NET_Init(void) {
#ifdef PLATFORM_WINDOWS
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        Com_Printf("NET_Init: WSAStartup failed\n");
        /* Continue anyway -- loopback doesn't need sockets */
    }
#endif
    memset(loopback, 0, sizeof(loopback));
    net_initialized = qtrue;
    Com_Printf("Network initialized (loopback enabled)\n");
}

void NET_Shutdown(void) {
    if (!net_initialized) return;
#ifdef PLATFORM_WINDOWS
    WSACleanup();
#endif
    memset(loopback, 0, sizeof(loopback));
    net_initialized = qfalse;
    Com_Printf("Network shutdown\n");
}

/* =========================================================================
 * Public API
 * ========================================================================= */

void NET_SendPacket(int length, const void *data, netadr_t to) {
    if (to.type == NA_LOOPBACK) {
        /* Determine direction: port 0 = to server, port 1 = to client */
        NET_SendLoopback(to.port ? 1 : 0, data, length);
        return;
    }

    /* TODO: Real UDP sendto() for multiplayer */
    (void)length; (void)data; (void)to;
}

qboolean NET_GetPacket(netadr_t *from, msg_t *msg) {
    /* Try loopback first (server-to-client direction = queue 1) */
    if (NET_GetLoopback(1, msg)) {
        memset(from, 0, sizeof(*from));
        from->type = NA_LOOPBACK;
        return qtrue;
    }

    /* TODO: Real UDP recvfrom() for multiplayer */
    return qfalse;
}

/* Server-side receive: reads from client-to-server queue */
qboolean NET_GetServerPacket(netadr_t *from, msg_t *msg) {
    if (NET_GetLoopback(0, msg)) {
        memset(from, 0, sizeof(*from));
        from->type = NA_LOOPBACK;
        return qtrue;
    }
    return qfalse;
}

qboolean NET_StringToAdr(const char *s, netadr_t *a) {
    if (!s || !a) return qfalse;

    memset(a, 0, sizeof(*a));

    /* "localhost" maps to loopback */
    if (!Q_stricmp(s, "localhost") || !Q_stricmp(s, "loopback")) {
        a->type = NA_LOOPBACK;
        return qtrue;
    }

    /* TODO: DNS resolution for multiplayer */
    return qfalse;
}

char *NET_AdrToString(netadr_t a) {
    static char buf[64];
    if (a.type == NA_LOOPBACK) {
        Q_strncpyz(buf, "loopback", sizeof(buf));
    } else {
        snprintf(buf, sizeof(buf), "%d.%d.%d.%d:%d",
                 a.ip[0], a.ip[1], a.ip[2], a.ip[3], a.port);
    }
    return buf;
}

/* =========================================================================
 * Loopback helpers for direct server<->client communication
 *
 * In single-player, we can bypass the message serialization entirely
 * and directly share data. These functions provide that fast path.
 * ========================================================================= */

void NET_SendLoopbackToServer(const void *data, int length) {
    NET_SendLoopback(0, data, length);
}

void NET_SendLoopbackToClient(const void *data, int length) {
    NET_SendLoopback(1, data, length);
}
