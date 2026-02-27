/*
 * msg.c -- Network message I/O
 *
 * Handles reading and writing of network messages between client and server.
 * FAKK2 uses the same message format as Quake III: bit-stream based packets
 * with Huffman compression for bandwidth efficiency.
 *
 * Message format:
 *   - Variable-length bit fields for common small values
 *   - Huffman-compressed strings
 *   - Delta-compressed entity states (only changed fields are transmitted)
 *
 * For the single-player recomp, messages pass through loopback (no actual
 * network traffic), but the format must match the original for save/demo
 * compatibility.
 */

#include "qcommon.h"
#include <string.h>

/* =========================================================================
 * Huffman coding tables
 *
 * Q3/FAKK2 uses adaptive Huffman compression on message data.
 * For the recomp, we start without compression (raw byte mode).
 * The bit-level I/O still works for field packing.
 * ========================================================================= */

/* TODO: Implement Huffman coding for full network compatibility */

/* =========================================================================
 * Message initialization
 * ========================================================================= */

void MSG_Init(msg_t *buf, byte *data, int length) {
    memset(buf, 0, sizeof(*buf));
    buf->data = data;
    buf->maxsize = length;
}

void MSG_Clear(msg_t *buf) {
    buf->cursize = 0;
    buf->overflowed = qfalse;
    buf->bit = 0;
    buf->readcount = 0;
}

/* =========================================================================
 * Bit-level writing
 *
 * Messages are composed of variable-width bit fields.
 * Common small values use fewer bits for bandwidth efficiency.
 * ========================================================================= */

static void MSG_WriteByte_raw(msg_t *msg, int b) {
    if (msg->cursize + 1 > msg->maxsize) {
        if (!msg->allowoverflow) {
            Com_Error(ERR_FATAL, "MSG_WriteByte: overflow (max %d)", msg->maxsize);
        }
        msg->overflowed = qtrue;
        return;
    }
    msg->data[msg->cursize++] = (byte)(b & 0xFF);
}

void MSG_WriteBits(msg_t *msg, int value, int bits) {
    if (bits == 0 || bits < -31 || bits > 32) {
        Com_Error(ERR_FATAL, "MSG_WriteBits: bad bits %d", bits);
        return;
    }

    /* Negative bits means unsigned value of abs(bits) width */
    if (bits < 0) bits = -bits;

    if (msg->oob) {
        /* Out-of-band messages use raw byte writing */
        if (bits == 8) {
            MSG_WriteByte_raw(msg, value);
            return;
        } else if (bits == 16) {
            MSG_WriteByte_raw(msg, value & 0xFF);
            MSG_WriteByte_raw(msg, (value >> 8) & 0xFF);
            return;
        } else if (bits == 32) {
            MSG_WriteByte_raw(msg, value & 0xFF);
            MSG_WriteByte_raw(msg, (value >> 8) & 0xFF);
            MSG_WriteByte_raw(msg, (value >> 16) & 0xFF);
            MSG_WriteByte_raw(msg, (value >> 24) & 0xFF);
            return;
        }
    }

    /* Bit-level writing */
    for (int i = 0; i < bits; i++) {
        int byteIdx = msg->bit >> 3;
        int bitIdx = msg->bit & 7;

        if (byteIdx >= msg->maxsize) {
            if (!msg->allowoverflow) {
                Com_Error(ERR_FATAL, "MSG_WriteBits: overflow");
            }
            msg->overflowed = qtrue;
            return;
        }

        if (bitIdx == 0) {
            msg->data[byteIdx] = 0;
        }

        if (value & (1 << i)) {
            msg->data[byteIdx] |= (1 << bitIdx);
        }

        msg->bit++;
    }

    /* Update cursize to reflect bytes used */
    msg->cursize = (msg->bit + 7) >> 3;
}

/* =========================================================================
 * Bit-level reading
 * ========================================================================= */

int MSG_ReadBits(msg_t *msg, int bits) {
    int value = 0;
    qboolean sign = qfalse;

    if (bits < 0) {
        bits = -bits;
        sign = qtrue;
    }

    if (msg->oob) {
        if (bits == 8) {
            if (msg->readcount >= msg->cursize) return -1;
            return msg->data[msg->readcount++];
        } else if (bits == 16) {
            if (msg->readcount + 2 > msg->cursize) return -1;
            int v = msg->data[msg->readcount] | (msg->data[msg->readcount + 1] << 8);
            msg->readcount += 2;
            return v;
        } else if (bits == 32) {
            if (msg->readcount + 4 > msg->cursize) return -1;
            int v = msg->data[msg->readcount]
                  | (msg->data[msg->readcount + 1] << 8)
                  | (msg->data[msg->readcount + 2] << 16)
                  | (msg->data[msg->readcount + 3] << 24);
            msg->readcount += 4;
            return v;
        }
    }

    /* Bit-level reading */
    for (int i = 0; i < bits; i++) {
        int byteIdx = msg->bit >> 3;
        int bitIdx = msg->bit & 7;

        if (byteIdx >= msg->cursize) {
            return -1;
        }

        if (msg->data[byteIdx] & (1 << bitIdx)) {
            value |= (1 << i);
        }

        msg->bit++;
    }

    msg->readcount = (msg->bit + 7) >> 3;

    if (sign) {
        /* Sign-extend */
        if (value & (1 << (bits - 1))) {
            value |= ~((1 << bits) - 1);
        }
    }

    return value;
}

/* =========================================================================
 * Typed write functions
 * ========================================================================= */

void MSG_WriteByte(msg_t *sb, int c) {
    MSG_WriteBits(sb, c, 8);
}

void MSG_WriteShort(msg_t *sb, int c) {
    MSG_WriteBits(sb, c, 16);
}

void MSG_WriteLong(msg_t *sb, int c) {
    MSG_WriteBits(sb, c, 32);
}

void MSG_WriteFloat(msg_t *sb, float f) {
    union {
        float   f;
        int     i;
    } dat;
    dat.f = f;
    MSG_WriteBits(sb, dat.i, 32);
}

void MSG_WriteString(msg_t *sb, const char *s) {
    if (!s) {
        MSG_WriteByte(sb, 0);
        return;
    }

    int len = (int)strlen(s);
    if (len >= MAX_STRING_CHARS) {
        Com_Printf("MSG_WriteString: MAX_STRING_CHARS exceeded\n");
        MSG_WriteByte(sb, 0);
        return;
    }

    for (int i = 0; i <= len; i++) {
        MSG_WriteByte(sb, s[i]);
    }
}

void MSG_WriteAngle(msg_t *sb, float f) {
    MSG_WriteByte(sb, (int)(f * 256.0f / 360.0f) & 0xFF);
}

void MSG_WriteAngle16(msg_t *sb, float f) {
    MSG_WriteShort(sb, (int)(f * 65536.0f / 360.0f) & 0xFFFF);
}

void MSG_WriteCoord(msg_t *sb, float f) {
    /* 1/8 unit precision -- standard Q3 coordinate encoding */
    MSG_WriteShort(sb, (int)(f * 8.0f));
}

void MSG_WriteDir(msg_t *sb, const vec3_t dir) {
    /* Encode direction as a single byte index into a precomputed table */
    /* Q3 uses 162-entry direction table, FAKK2 likely the same */
    /* For now, encode as 3 bytes */
    MSG_WriteByte(sb, (int)((dir[0] + 1.0f) * 127.5f));
    MSG_WriteByte(sb, (int)((dir[1] + 1.0f) * 127.5f));
    MSG_WriteByte(sb, (int)((dir[2] + 1.0f) * 127.5f));
}

void MSG_WriteData(msg_t *sb, const void *data, int length) {
    const byte *src = (const byte *)data;
    for (int i = 0; i < length; i++) {
        MSG_WriteByte(sb, src[i]);
    }
}

/* =========================================================================
 * Typed read functions
 * ========================================================================= */

int MSG_ReadByte(msg_t *msg) {
    int c = MSG_ReadBits(msg, 8);
    if (c == -1) return -1;
    return c & 0xFF;
}

int MSG_ReadShort(msg_t *msg) {
    int c = MSG_ReadBits(msg, 16);
    if (c == -1) return -1;
    /* Sign-extend 16-bit value */
    return (short)(c & 0xFFFF);
}

int MSG_ReadLong(msg_t *msg) {
    return MSG_ReadBits(msg, 32);
}

float MSG_ReadFloat(msg_t *msg) {
    union {
        float   f;
        int     i;
    } dat;
    dat.i = MSG_ReadBits(msg, 32);
    return dat.f;
}

char *MSG_ReadString(msg_t *msg) {
    static char string[MAX_STRING_CHARS];
    int i = 0;

    while (1) {
        int c = MSG_ReadByte(msg);
        if (c <= 0 || c == -1) break;
        if (i < MAX_STRING_CHARS - 1) {
            string[i++] = (char)c;
        }
    }
    string[i] = '\0';
    return string;
}

char *MSG_ReadStringLine(msg_t *msg) {
    static char string[MAX_STRING_CHARS];
    int i = 0;

    while (1) {
        int c = MSG_ReadByte(msg);
        if (c <= 0 || c == '\n' || c == -1) break;
        if (i < MAX_STRING_CHARS - 1) {
            string[i++] = (char)c;
        }
    }
    string[i] = '\0';
    return string;
}

float MSG_ReadAngle(msg_t *msg) {
    return ((float)MSG_ReadByte(msg)) * (360.0f / 256.0f);
}

float MSG_ReadAngle16(msg_t *msg) {
    return ((float)(unsigned short)MSG_ReadShort(msg)) * (360.0f / 65536.0f);
}

float MSG_ReadCoord(msg_t *msg) {
    return MSG_ReadShort(msg) * (1.0f / 8.0f);
}

void MSG_ReadDir(msg_t *msg, vec3_t dir) {
    dir[0] = (MSG_ReadByte(msg) / 127.5f) - 1.0f;
    dir[1] = (MSG_ReadByte(msg) / 127.5f) - 1.0f;
    dir[2] = (MSG_ReadByte(msg) / 127.5f) - 1.0f;
    VectorNormalize(dir);
}

void MSG_ReadData(msg_t *msg, void *data, int length) {
    byte *dst = (byte *)data;
    for (int i = 0; i < length; i++) {
        dst[i] = (byte)MSG_ReadByte(msg);
    }
}

/* =========================================================================
 * Delta entity state encoding/decoding
 *
 * For bandwidth efficiency, entity state updates only include fields
 * that have changed since the last acknowledged frame. A bitmask
 * indicates which fields are present in the message.
 * ========================================================================= */

void MSG_WriteDeltaEntity(msg_t *msg, entityState_t *from, entityState_t *to, qboolean force) {
    /* TODO: Full delta encoding implementation.
     * For loopback (single-player), we can send the full state initially. */
    (void)msg; (void)from; (void)to; (void)force;
}

void MSG_ReadDeltaEntity(msg_t *msg, entityState_t *from, entityState_t *to, int number) {
    /* TODO: Full delta decoding. */
    (void)msg; (void)from; (void)to; (void)number;
}

void MSG_WriteDeltaPlayerstate(msg_t *msg, playerState_t *from, playerState_t *to) {
    (void)msg; (void)from; (void)to;
    /* TODO: Delta-encoded player state */
}

void MSG_ReadDeltaPlayerstate(msg_t *msg, playerState_t *from, playerState_t *to) {
    (void)msg; (void)from; (void)to;
}
