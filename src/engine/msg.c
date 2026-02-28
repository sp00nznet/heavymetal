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

/*
 * Delta encoding uses a field table describing each field's offset and size.
 * A bitmask indicates which fields changed; only those are transmitted.
 * This matches the Q3/FAKK2 network protocol for snapshot compatibility.
 */

typedef struct {
    int     offset;
    int     bits;       /* 0 = float, positive = int bits, negative = signed int */
} netField_t;

#define ESF(x)  (int)(size_t)&((entityState_t *)0)->x

static const netField_t entityStateFields[] = {
    { ESF(pos.trType),          8 },
    { ESF(pos.trTime),          32 },
    { ESF(pos.trDuration),      32 },
    { ESF(pos.trBase[0]),       0 },
    { ESF(pos.trBase[1]),       0 },
    { ESF(pos.trBase[2]),       0 },
    { ESF(pos.trDelta[0]),      0 },
    { ESF(pos.trDelta[1]),      0 },
    { ESF(pos.trDelta[2]),      0 },
    { ESF(apos.trType),         8 },
    { ESF(apos.trTime),         32 },
    { ESF(apos.trDuration),     32 },
    { ESF(apos.trBase[0]),      0 },
    { ESF(apos.trBase[1]),      0 },
    { ESF(apos.trBase[2]),      0 },
    { ESF(apos.trDelta[0]),     0 },
    { ESF(apos.trDelta[1]),     0 },
    { ESF(apos.trDelta[2]),     0 },
    { ESF(time),                32 },
    { ESF(time2),               32 },
    { ESF(origin[0]),           0 },
    { ESF(origin[1]),           0 },
    { ESF(origin[2]),           0 },
    { ESF(origin2[0]),          0 },
    { ESF(origin2[1]),          0 },
    { ESF(origin2[2]),          0 },
    { ESF(angles[0]),           0 },
    { ESF(angles[1]),           0 },
    { ESF(angles[2]),           0 },
    { ESF(angles2[0]),          0 },
    { ESF(angles2[1]),          0 },
    { ESF(angles2[2]),          0 },
    { ESF(otherEntityNum),      10 },
    { ESF(otherEntityNum2),     10 },
    { ESF(groundEntityNum),     10 },
    { ESF(constantLight),       32 },
    { ESF(loopSound),           16 },
    { ESF(loopSoundVolume),     8 },
    { ESF(loopSoundMinDist),    16 },
    { ESF(loopSoundMaxDist),    0 },
    { ESF(loopSoundPitch),      0 },
    { ESF(loopSoundFlags),      8 },
    { ESF(parent),              10 },
    { ESF(tag_num),             8 },
    { ESF(attach_use_angles),   1 },
    { ESF(attach_offset[0]),    0 },
    { ESF(attach_offset[1]),    0 },
    { ESF(attach_offset[2]),    0 },
    { ESF(beam_entnum),         10 },
    { ESF(modelindex),          16 },
    { ESF(usageIndex),          16 },
    { ESF(skinNum),             8 },
    { ESF(wasframe),            16 },
    { ESF(actionWeight),        0 },
    { ESF(clientNum),           8 },
    { ESF(groundPlane),         1 },
    { ESF(solid),               24 },
    { ESF(scale),               0 },
    { ESF(alpha),               0 },
    { ESF(renderfx),            32 },
    { ESF(shader_data[0]),      0 },
    { ESF(shader_data[1]),      0 },
    { ESF(shader_time),         0 },
    { ESF(eType),               8 },
    { ESF(eFlags),              32 },
};

#define NUM_ENTITY_FIELDS (int)(sizeof(entityStateFields) / sizeof(entityStateFields[0]))

/* Write delta-encoded entity state. If 'from' is NULL, send full state.
 * If entity is being removed, write entity number + remove bit. */
void MSG_WriteDeltaEntity(msg_t *msg, entityState_t *from, entityState_t *to, qboolean force) {
    entityState_t dummy;
    if (!from) {
        memset(&dummy, 0, sizeof(dummy));
        from = &dummy;
    }

    /* Check if entity is being removed */
    if (to->number < 0 || to->number >= MAX_GENTITIES) {
        /* Remove entity: write number with remove bit set */
        MSG_WriteBits(msg, to->number, 10);
        MSG_WriteBits(msg, 1, 1);   /* remove flag */
        return;
    }

    /* Determine which fields changed -- build a bitmask.
     * We use two 32-bit masks to cover all fields. */
    int lc = -1;  /* last changed field index */
    int i;

    for (i = 0; i < NUM_ENTITY_FIELDS; i++) {
        const netField_t *f = &entityStateFields[i];
        const int *fromVal = (const int *)((const byte *)from + f->offset);
        const int *toVal   = (const int *)((const byte *)to + f->offset);
        if (*fromVal != *toVal) {
            lc = i;
        }
    }

    if (lc == -1 && !force) {
        /* No changes and not forced -- don't write anything */
        return;
    }

    MSG_WriteBits(msg, to->number, 10);
    MSG_WriteBits(msg, 0, 1);   /* not removed */

    if (lc == -1) {
        /* No changes but forced -- write zero-change marker */
        MSG_WriteBits(msg, 0, 1);
        return;
    }

    MSG_WriteBits(msg, 1, 1);   /* has changes */
    MSG_WriteBits(msg, lc, 8);  /* last changed field index */

    for (i = 0; i <= lc; i++) {
        const netField_t *f = &entityStateFields[i];
        const int *fromVal = (const int *)((const byte *)from + f->offset);
        const int *toVal   = (const int *)((const byte *)to + f->offset);

        if (*fromVal == *toVal) {
            MSG_WriteBits(msg, 0, 1);  /* not changed */
        } else {
            MSG_WriteBits(msg, 1, 1);  /* changed */
            if (f->bits == 0) {
                /* Float field */
                float fv = *(const float *)toVal;
                int trunc = (int)fv;
                if (trunc == fv && trunc >= -4096 && trunc < 4096) {
                    /* Send as truncated integer (saves bits) */
                    MSG_WriteBits(msg, 0, 1);
                    MSG_WriteBits(msg, trunc + 4096, 13);
                } else {
                    /* Send as full float */
                    MSG_WriteBits(msg, 1, 1);
                    MSG_WriteBits(msg, *toVal, 32);
                }
            } else {
                /* Integer field */
                int bits = f->bits;
                if (bits < 0) bits = -bits;
                MSG_WriteBits(msg, *toVal, bits);
            }
        }
    }
}

/* Read delta-encoded entity state. Returns qtrue if entity was removed. */
void MSG_ReadDeltaEntity(msg_t *msg, entityState_t *from, entityState_t *to, int number) {
    if (!from) {
        memset(to, 0, sizeof(*to));
    } else if (to != from) {
        memcpy(to, from, sizeof(*to));
    }

    to->number = number;

    /* Check remove flag -- caller reads number and remove bit before calling */
    int hasChanges = MSG_ReadBits(msg, 1);
    if (!hasChanges) {
        /* No field changes -- entity state unchanged from 'from' */
        return;
    }

    int lc = MSG_ReadBits(msg, 8);  /* last changed field index */

    for (int i = 0; i <= lc && i < NUM_ENTITY_FIELDS; i++) {
        const netField_t *f = &entityStateFields[i];
        int *toVal = (int *)((byte *)to + f->offset);

        int changed = MSG_ReadBits(msg, 1);
        if (!changed) continue;

        if (f->bits == 0) {
            /* Float field */
            int fullFloat = MSG_ReadBits(msg, 1);
            if (fullFloat) {
                *toVal = MSG_ReadBits(msg, 32);
            } else {
                int trunc = MSG_ReadBits(msg, 13) - 4096;
                *(float *)toVal = (float)trunc;
            }
        } else {
            int bits = f->bits;
            if (bits < 0) bits = -bits;
            *toVal = MSG_ReadBits(msg, bits);
        }
    }
}

/* =========================================================================
 * Delta player state encoding/decoding
 *
 * Same approach as entity state: bitmask of changed fields.
 * ========================================================================= */

#define PSF(x)  (int)(size_t)&((playerState_t *)0)->x

static const netField_t playerStateFields[] = {
    { PSF(commandTime),         32 },
    { PSF(pm_type),             8 },
    { PSF(pm_flags),            16 },
    { PSF(pm_time),             16 },
    { PSF(bobCycle),            8 },
    { PSF(origin[0]),           0 },
    { PSF(origin[1]),           0 },
    { PSF(origin[2]),           0 },
    { PSF(velocity[0]),         0 },
    { PSF(velocity[1]),         0 },
    { PSF(velocity[2]),         0 },
    { PSF(gravity),             16 },
    { PSF(speed),               16 },
    { PSF(delta_angles[0]),     16 },
    { PSF(delta_angles[1]),     16 },
    { PSF(delta_angles[2]),     16 },
    { PSF(groundEntityNum),     10 },
    { PSF(legsTimer),           16 },
    { PSF(legsAnim),            16 },
    { PSF(torsoTimer),          16 },
    { PSF(torsoAnim),           16 },
    { PSF(movementDir),         8 },
    { PSF(grapplePoint[0]),     0 },
    { PSF(grapplePoint[1]),     0 },
    { PSF(grapplePoint[2]),     0 },
    { PSF(clientNum),           8 },
    { PSF(viewangles[0]),       0 },
    { PSF(viewangles[1]),       0 },
    { PSF(viewangles[2]),       0 },
    { PSF(viewheight),          -8 },
    { PSF(fLeanAngle),          0 },
    { PSF(current_music_mood),  8 },
    { PSF(fallback_music_mood), 8 },
    { PSF(music_volume),        0 },
    { PSF(music_volume_fade_time), 0 },
    { PSF(reverb_type),         8 },
    { PSF(reverb_level),        0 },
    { PSF(blend[0]),            0 },
    { PSF(blend[1]),            0 },
    { PSF(blend[2]),            0 },
    { PSF(blend[3]),            0 },
    { PSF(fov),                 0 },
    { PSF(camera_origin[0]),    0 },
    { PSF(camera_origin[1]),    0 },
    { PSF(camera_origin[2]),    0 },
    { PSF(camera_angles[0]),    0 },
    { PSF(camera_angles[1]),    0 },
    { PSF(camera_angles[2]),    0 },
    { PSF(camera_flags),        16 },
    { PSF(camera_offset),       0 },
    { PSF(camera_posofs[0]),    0 },
    { PSF(camera_posofs[1]),    0 },
    { PSF(camera_posofs[2]),    0 },
    { PSF(voted),               2 },
};

#define NUM_PS_FIELDS (int)(sizeof(playerStateFields) / sizeof(playerStateFields[0]))

void MSG_WriteDeltaPlayerstate(msg_t *msg, playerState_t *from, playerState_t *to) {
    playerState_t dummy;
    if (!from) {
        memset(&dummy, 0, sizeof(dummy));
        from = &dummy;
    }

    /* Find last changed field */
    int lc = -1;
    for (int i = 0; i < NUM_PS_FIELDS; i++) {
        const netField_t *f = &playerStateFields[i];
        const int *fromVal = (const int *)((const byte *)from + f->offset);
        const int *toVal   = (const int *)((const byte *)to + f->offset);
        if (*fromVal != *toVal) lc = i;
    }

    if (lc == -1) {
        MSG_WriteBits(msg, 0, 1);   /* no changes */
        goto write_arrays;
    }

    MSG_WriteBits(msg, 1, 1);       /* has changes */
    MSG_WriteBits(msg, lc, 8);

    for (int i = 0; i <= lc; i++) {
        const netField_t *f = &playerStateFields[i];
        const int *fromVal = (const int *)((const byte *)from + f->offset);
        const int *toVal   = (const int *)((const byte *)to + f->offset);

        if (*fromVal == *toVal) {
            MSG_WriteBits(msg, 0, 1);
        } else {
            MSG_WriteBits(msg, 1, 1);
            if (f->bits == 0) {
                float fv = *(const float *)toVal;
                int trunc = (int)fv;
                if (trunc == fv && trunc >= -4096 && trunc < 4096) {
                    MSG_WriteBits(msg, 0, 1);
                    MSG_WriteBits(msg, trunc + 4096, 13);
                } else {
                    MSG_WriteBits(msg, 1, 1);
                    MSG_WriteBits(msg, *toVal, 32);
                }
            } else {
                int bits = f->bits;
                if (bits < 0) bits = -bits;
                MSG_WriteBits(msg, *toVal, bits);
            }
        }
    }

write_arrays:
    /* Delta-encode stat/ammo arrays */
    {
        int statsBits = 0;
        for (int i = 0; i < 32; i++) {
            if (to->stats[i] != from->stats[i]) statsBits |= (1 << i);
        }
        if (statsBits) {
            MSG_WriteBits(msg, 1, 1);
            MSG_WriteLong(msg, statsBits);
            for (int i = 0; i < 32; i++) {
                if (statsBits & (1 << i)) MSG_WriteShort(msg, to->stats[i]);
            }
        } else {
            MSG_WriteBits(msg, 0, 1);
        }

        int ammoBits = 0;
        for (int i = 0; i < 16; i++) {
            if (to->ammo_amount[i] != from->ammo_amount[i]) ammoBits |= (1 << i);
        }
        if (ammoBits) {
            MSG_WriteBits(msg, 1, 1);
            MSG_WriteShort(msg, ammoBits);
            for (int i = 0; i < 16; i++) {
                if (ammoBits & (1 << i)) MSG_WriteShort(msg, to->ammo_amount[i]);
            }
        } else {
            MSG_WriteBits(msg, 0, 1);
        }
    }
}

void MSG_ReadDeltaPlayerstate(msg_t *msg, playerState_t *from, playerState_t *to) {
    playerState_t dummy;
    if (!from) {
        memset(&dummy, 0, sizeof(dummy));
        from = &dummy;
    }
    memcpy(to, from, sizeof(*to));

    int hasChanges = MSG_ReadBits(msg, 1);
    if (hasChanges) {
        int lc = MSG_ReadBits(msg, 8);
        for (int i = 0; i <= lc && i < NUM_PS_FIELDS; i++) {
            const netField_t *f = &playerStateFields[i];
            int *toVal = (int *)((byte *)to + f->offset);

            if (!MSG_ReadBits(msg, 1)) continue;

            if (f->bits == 0) {
                if (MSG_ReadBits(msg, 1)) {
                    *toVal = MSG_ReadBits(msg, 32);
                } else {
                    *(float *)toVal = (float)(MSG_ReadBits(msg, 13) - 4096);
                }
            } else {
                int bits = f->bits;
                if (bits < 0) bits = -bits;
                *toVal = MSG_ReadBits(msg, bits);
            }
        }
    }

    /* Read stat/ammo arrays */
    if (MSG_ReadBits(msg, 1)) {
        int statsBits = MSG_ReadLong(msg);
        for (int i = 0; i < 32; i++) {
            if (statsBits & (1 << i)) to->stats[i] = MSG_ReadShort(msg);
        }
    }
    if (MSG_ReadBits(msg, 1)) {
        int ammoBits = (unsigned short)MSG_ReadShort(msg);
        for (int i = 0; i < 16; i++) {
            if (ammoBits & (1 << i)) to->ammo_amount[i] = MSG_ReadShort(msg);
        }
    }
}
