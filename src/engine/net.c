/*
 * net.c -- Network subsystem
 *
 * FAKK2 networking is based on Q3's UDP client/server model.
 * Original imported 17 functions from WSOCK32.DLL.
 *
 * The recomp uses platform-native sockets (Winsock2 on Windows,
 * BSD sockets on Linux/macOS).
 */

#include "qcommon.h"

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

static qboolean net_initialized = qfalse;

void NET_Init(void) {
#ifdef PLATFORM_WINDOWS
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        Com_Printf("NET_Init: WSAStartup failed\n");
        return;
    }
#endif
    net_initialized = qtrue;
    Com_Printf("Network initialized\n");
}

void NET_Shutdown(void) {
    if (!net_initialized) return;
#ifdef PLATFORM_WINDOWS
    WSACleanup();
#endif
    net_initialized = qfalse;
    Com_Printf("Network shutdown\n");
}

void NET_SendPacket(int length, const void *data, netadr_t to) {
    (void)length; (void)data; (void)to;
    /* TODO: UDP sendto() */
}

qboolean NET_GetPacket(netadr_t *from, msg_t *msg) {
    (void)from; (void)msg;
    /* TODO: UDP recvfrom() */
    return qfalse;
}

qboolean NET_StringToAdr(const char *s, netadr_t *a) {
    (void)s; (void)a;
    /* TODO: DNS resolution */
    return qfalse;
}

char *NET_AdrToString(netadr_t a) {
    static char buf[64];
    snprintf(buf, sizeof(buf), "%d.%d.%d.%d:%d",
             a.ip[0], a.ip[1], a.ip[2], a.ip[3], a.port);
    return buf;
}
