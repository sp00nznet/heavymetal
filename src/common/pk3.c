/*
 * pk3.c -- PK3 (ZIP) archive reader implementation
 *
 * Self-contained: includes a minimal RFC 1951 DEFLATE inflater.
 * No external dependencies beyond the C standard library.
 */

#include "pk3.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* =========================================================================
 * ZIP format constants
 * ========================================================================= */

#define ZIP_LOCAL_HEADER_SIG    0x04034b50
#define ZIP_CENTRAL_DIR_SIG     0x02014b50
#define ZIP_EOCD_SIG            0x06054b50
#define ZIP_EOCD_MIN_SIZE       22
#define ZIP_EOCD_MAX_COMMENT    65535

/* =========================================================================
 * Utility: read little-endian values from buffer
 * ========================================================================= */

static word read_u16(const byte *p) {
    return (word)p[0] | ((word)p[1] << 8);
}

static dword read_u32(const byte *p) {
    return (dword)p[0] | ((dword)p[1] << 8) |
           ((dword)p[2] << 16) | ((dword)p[3] << 24);
}

/* =========================================================================
 * Hashing for fast file lookup
 * ========================================================================= */

static dword pk3_hash_name(const char *name) {
    dword hash = 5381;
    while (*name) {
        char c = *name++;
        if (c == '\\') c = '/';
        if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
        hash = ((hash << 5) + hash) + (unsigned char)c;
    }
    return hash;
}

static void pk3_normalize_path(char *dst, const char *src, int maxlen) {
    int i;
    for (i = 0; i < maxlen - 1 && src[i]; i++) {
        char c = src[i];
        if (c == '\\') c = '/';
        dst[i] = (char)tolower((unsigned char)c);
    }
    dst[i] = '\0';
}

/* =========================================================================
 * RFC 1951 DEFLATE Inflater
 *
 * Minimal but complete implementation of the inflate algorithm.
 * Supports fixed and dynamic Huffman codes.
 * ========================================================================= */

/* Huffman decoder state */
typedef struct {
    word    counts[16];     /* number of codes of each length */
    word    symbols[288];   /* symbols sorted by code */
} huff_t;

/* Bit reader state */
typedef struct {
    const byte  *src;
    int         src_len;
    int         pos;        /* byte position */
    dword       bits;       /* bit accumulator */
    int         nbits;      /* number of valid bits */
} bitreader_t;

static void br_init(bitreader_t *br, const byte *src, int len) {
    br->src = src;
    br->src_len = len;
    br->pos = 0;
    br->bits = 0;
    br->nbits = 0;
}

static void br_fill(bitreader_t *br) {
    while (br->nbits <= 24 && br->pos < br->src_len) {
        br->bits |= (dword)br->src[br->pos++] << br->nbits;
        br->nbits += 8;
    }
}

static dword br_read(bitreader_t *br, int n) {
    br_fill(br);
    dword val = br->bits & ((1u << n) - 1);
    br->bits >>= n;
    br->nbits -= n;
    return val;
}

static int huff_build(huff_t *h, const byte *lengths, int num) {
    int i;
    word offsets[16];

    memset(h->counts, 0, sizeof(h->counts));
    for (i = 0; i < num; i++) {
        if (lengths[i]) h->counts[lengths[i]]++;
    }

    offsets[0] = 0;
    offsets[1] = 0;
    for (i = 1; i < 15; i++) {
        offsets[i + 1] = offsets[i] + h->counts[i];
    }

    for (i = 0; i < num; i++) {
        if (lengths[i]) {
            h->symbols[offsets[lengths[i]]++] = (word)i;
        }
    }
    return 0;
}

static int huff_decode(bitreader_t *br, const huff_t *h) {
    int code = 0, first = 0, index = 0;
    br_fill(br);

    for (int len = 1; len <= 15; len++) {
        code |= (br->bits & 1);
        br->bits >>= 1;
        br->nbits--;

        int count = h->counts[len];
        if (code - count < first) {
            return h->symbols[index + (code - first)];
        }
        index += count;
        first = (first + count) << 1;
        code <<= 1;
    }
    return -1; /* invalid code */
}

/* Fixed Huffman tables (RFC 1951 section 3.2.6) */
static huff_t fixed_litlen;
static huff_t fixed_dist;
static int fixed_tables_built = 0;

static void build_fixed_tables(void) {
    byte lengths[288];
    int i;

    /* Literal/length codes */
    for (i = 0; i <= 143; i++) lengths[i] = 8;
    for (i = 144; i <= 255; i++) lengths[i] = 9;
    for (i = 256; i <= 279; i++) lengths[i] = 7;
    for (i = 280; i <= 287; i++) lengths[i] = 8;
    huff_build(&fixed_litlen, lengths, 288);

    /* Distance codes */
    for (i = 0; i < 30; i++) lengths[i] = 5;
    huff_build(&fixed_dist, lengths, 30);

    fixed_tables_built = 1;
}

/* Length and distance base values and extra bits */
static const word len_base[29] = {
    3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,
    35,43,51,59,67,83,99,115,131,163,195,227,258
};
static const byte len_extra[29] = {
    0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,
    3,3,3,3,4,4,4,4,5,5,5,5,0
};
static const word dist_base[30] = {
    1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,
    257,385,513,769,1025,1537,2049,3073,4097,6145,
    8193,12289,16385,24577
};
static const byte dist_extra[30] = {
    0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,
    7,7,8,8,9,9,10,10,11,11,12,12,13,13
};

/* Code length order for dynamic tables */
static const byte clorder[19] = {
    16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15
};

static int inflate_block(bitreader_t *br, const huff_t *litlen,
                         const huff_t *dist, byte *dst, int dst_len, int *out_pos) {
    int pos = *out_pos;

    for (;;) {
        int sym = huff_decode(br, litlen);
        if (sym < 0) return -1;

        if (sym < 256) {
            /* Literal byte */
            if (pos >= dst_len) return -1;
            dst[pos++] = (byte)sym;
        } else if (sym == 256) {
            /* End of block */
            break;
        } else {
            /* Length/distance pair */
            sym -= 257;
            if (sym >= 29) return -1;

            int length = len_base[sym] + (int)br_read(br, len_extra[sym]);

            int dsym = huff_decode(br, dist);
            if (dsym < 0 || dsym >= 30) return -1;

            int distance = dist_base[dsym] + (int)br_read(br, dist_extra[dsym]);

            if (pos - distance < 0) return -1;
            if (pos + length > dst_len) return -1;

            /* Copy from back-reference (byte-by-byte for overlapping) */
            for (int i = 0; i < length; i++) {
                dst[pos] = dst[pos - distance];
                pos++;
            }
        }
    }

    *out_pos = pos;
    return 0;
}

int PK3_Inflate(const byte *src, int src_len, byte *dst, int dst_len) {
    bitreader_t br;
    int out_pos = 0;
    int bfinal;

    if (!fixed_tables_built) build_fixed_tables();

    br_init(&br, src, src_len);

    do {
        bfinal = (int)br_read(&br, 1);
        int btype = (int)br_read(&br, 2);

        if (btype == 0) {
            /* Stored (no compression) */
            br.bits = 0;
            br.nbits = 0;
            if (br.pos + 4 > br.src_len) return -1;
            word len = read_u16(br.src + br.pos);
            br.pos += 4; /* skip len and ~len */
            if (br.pos + len > br.src_len) return -1;
            if (out_pos + len > dst_len) return -1;
            memcpy(dst + out_pos, br.src + br.pos, len);
            br.pos += len;
            out_pos += len;
        } else if (btype == 1) {
            /* Fixed Huffman */
            if (inflate_block(&br, &fixed_litlen, &fixed_dist, dst, dst_len, &out_pos) < 0)
                return -1;
        } else if (btype == 2) {
            /* Dynamic Huffman */
            int hlit = (int)br_read(&br, 5) + 257;
            int hdist = (int)br_read(&br, 5) + 1;
            int hclen = (int)br_read(&br, 4) + 4;

            byte cl_lengths[19];
            memset(cl_lengths, 0, sizeof(cl_lengths));
            for (int i = 0; i < hclen; i++) {
                cl_lengths[clorder[i]] = (byte)br_read(&br, 3);
            }

            huff_t cl_huff;
            huff_build(&cl_huff, cl_lengths, 19);

            /* Decode literal/length + distance code lengths */
            byte lengths[288 + 32];
            int total = hlit + hdist;
            int idx = 0;

            while (idx < total) {
                int s = huff_decode(&br, &cl_huff);
                if (s < 0) return -1;

                if (s < 16) {
                    lengths[idx++] = (byte)s;
                } else if (s == 16) {
                    int rep = (int)br_read(&br, 2) + 3;
                    byte prev = idx > 0 ? lengths[idx - 1] : 0;
                    while (rep-- && idx < total) lengths[idx++] = prev;
                } else if (s == 17) {
                    int rep = (int)br_read(&br, 3) + 3;
                    while (rep-- && idx < total) lengths[idx++] = 0;
                } else { /* s == 18 */
                    int rep = (int)br_read(&br, 7) + 11;
                    while (rep-- && idx < total) lengths[idx++] = 0;
                }
            }

            huff_t dyn_litlen, dyn_dist;
            huff_build(&dyn_litlen, lengths, hlit);
            huff_build(&dyn_dist, lengths + hlit, hdist);

            if (inflate_block(&br, &dyn_litlen, &dyn_dist, dst, dst_len, &out_pos) < 0)
                return -1;
        } else {
            return -1; /* invalid block type */
        }
    } while (!bfinal);

    return out_pos;
}

/* =========================================================================
 * CRC-32 (for file integrity verification)
 * ========================================================================= */

static dword crc32_table[256];
static int crc32_table_built = 0;

static void build_crc32_table(void) {
    for (int i = 0; i < 256; i++) {
        dword c = (dword)i;
        for (int j = 0; j < 8; j++) {
            if (c & 1) c = 0xEDB88320 ^ (c >> 1);
            else c >>= 1;
        }
        crc32_table[i] = c;
    }
    crc32_table_built = 1;
}

static dword pk3_crc32(const byte *data, int len) {
    if (!crc32_table_built) build_crc32_table();
    dword crc = 0xFFFFFFFF;
    for (int i = 0; i < len; i++) {
        crc = crc32_table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFF;
}

/* =========================================================================
 * PK3 Archive Operations
 * ========================================================================= */

pk3_archive_t *PK3_Open(const char *filepath) {
    FILE *fp = fopen(filepath, "rb");
    if (!fp) return NULL;

    /* Find the End of Central Directory record */
    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);

    if (file_size < ZIP_EOCD_MIN_SIZE) {
        fclose(fp);
        return NULL;
    }

    /* Search backwards for EOCD signature */
    long search_start = file_size - ZIP_EOCD_MIN_SIZE;
    long search_limit = file_size - ZIP_EOCD_MIN_SIZE - ZIP_EOCD_MAX_COMMENT;
    if (search_limit < 0) search_limit = 0;

    byte eocd_buf[ZIP_EOCD_MIN_SIZE];
    long eocd_pos = -1;

    for (long pos = search_start; pos >= search_limit; pos--) {
        fseek(fp, pos, SEEK_SET);
        if (fread(eocd_buf, 1, 4, fp) != 4) continue;
        if (read_u32(eocd_buf) == ZIP_EOCD_SIG) {
            eocd_pos = pos;
            break;
        }
    }

    if (eocd_pos < 0) {
        fclose(fp);
        return NULL;
    }

    /* Read EOCD */
    fseek(fp, eocd_pos, SEEK_SET);
    byte eocd[ZIP_EOCD_MIN_SIZE];
    if (fread(eocd, 1, ZIP_EOCD_MIN_SIZE, fp) != ZIP_EOCD_MIN_SIZE) {
        fclose(fp);
        return NULL;
    }

    int num_entries = read_u16(eocd + 10);  /* total entries */
    dword cd_size = read_u32(eocd + 12);    /* central directory size */
    dword cd_offset = read_u32(eocd + 16);  /* central directory offset */

    if (num_entries == 0 || num_entries > PK3_MAX_FILES) {
        fclose(fp);
        return NULL;
    }

    /* Allocate archive */
    pk3_archive_t *pk3 = (pk3_archive_t *)calloc(1, sizeof(pk3_archive_t));
    if (!pk3) { fclose(fp); return NULL; }

    strncpy(pk3->filepath, filepath, sizeof(pk3->filepath) - 1);
    pk3->fp = fp;
    pk3->num_entries = num_entries;
    pk3->entries = (pk3_entry_t *)calloc(num_entries, sizeof(pk3_entry_t));
    if (!pk3->entries) {
        free(pk3);
        fclose(fp);
        return NULL;
    }

    /* Read central directory */
    byte *cd_data = (byte *)malloc(cd_size);
    if (!cd_data) {
        free(pk3->entries);
        free(pk3);
        fclose(fp);
        return NULL;
    }

    fseek(fp, cd_offset, SEEK_SET);
    if (fread(cd_data, 1, cd_size, fp) != cd_size) {
        free(cd_data);
        free(pk3->entries);
        free(pk3);
        fclose(fp);
        return NULL;
    }

    /* Parse central directory entries */
    byte *ptr = cd_data;
    byte *cd_end = cd_data + cd_size;

    for (int i = 0; i < num_entries && ptr + 46 <= cd_end; i++) {
        if (read_u32(ptr) != ZIP_CENTRAL_DIR_SIG) break;

        word compression = read_u16(ptr + 10);
        dword crc = read_u32(ptr + 16);
        dword comp_size = read_u32(ptr + 20);
        dword uncomp_size = read_u32(ptr + 24);
        word name_len = read_u16(ptr + 28);
        word extra_len = read_u16(ptr + 30);
        word comment_len = read_u16(ptr + 32);
        dword local_offset = read_u32(ptr + 42);

        if (ptr + 46 + name_len > cd_end) break;

        pk3_entry_t *entry = &pk3->entries[i];
        entry->compression = compression;
        entry->crc32 = crc;
        entry->compressed_size = comp_size;
        entry->uncompressed_size = uncomp_size;
        entry->offset = local_offset;

        /* Normalize the filename */
        int copylen = name_len;
        if (copylen >= PK3_MAX_PATH) copylen = PK3_MAX_PATH - 1;
        pk3_normalize_path(entry->name, (const char *)(ptr + 46), copylen + 1);

        /* Build hash table entry */
        dword hash = pk3_hash_name(entry->name) & 4095;
        /* Simple linear probing -- store index+1 (0 = empty) */
        while (pk3->hash_table[hash]) {
            hash = (hash + 1) & 4095;
        }
        pk3->hash_table[hash] = (dword)(i + 1);

        ptr += 46 + name_len + extra_len + comment_len;
    }

    free(cd_data);
    return pk3;
}

void PK3_Close(pk3_archive_t *pk3) {
    if (!pk3) return;
    if (pk3->fp) fclose(pk3->fp);
    if (pk3->entries) free(pk3->entries);
    free(pk3);
}

const pk3_entry_t *PK3_FindFile(const pk3_archive_t *pk3, const char *name) {
    char normalized[PK3_MAX_PATH];
    pk3_normalize_path(normalized, name, PK3_MAX_PATH);

    dword hash = pk3_hash_name(normalized) & 4095;

    for (int probe = 0; probe < 4096; probe++) {
        dword idx = pk3->hash_table[(hash + probe) & 4095];
        if (!idx) return NULL;  /* empty slot = not found */
        idx--;  /* convert from 1-based to 0-based */
        if (strcmp(pk3->entries[idx].name, normalized) == 0) {
            return &pk3->entries[idx];
        }
    }
    return NULL;
}

int PK3_ReadFile(pk3_archive_t *pk3, const pk3_entry_t *entry, void **out_buffer) {
    if (!pk3 || !entry || !out_buffer) return -1;

    *out_buffer = NULL;

    /* Read local file header to get actual data offset */
    fseek(pk3->fp, entry->offset, SEEK_SET);
    byte local_hdr[30];
    if (fread(local_hdr, 1, 30, pk3->fp) != 30) return -1;
    if (read_u32(local_hdr) != ZIP_LOCAL_HEADER_SIG) return -1;

    word name_len = read_u16(local_hdr + 26);
    word extra_len = read_u16(local_hdr + 28);
    long data_offset = entry->offset + 30 + name_len + extra_len;

    /* Read compressed data */
    byte *comp_data = (byte *)malloc(entry->compressed_size);
    if (!comp_data) return -1;

    fseek(pk3->fp, data_offset, SEEK_SET);
    if (fread(comp_data, 1, entry->compressed_size, pk3->fp) != entry->compressed_size) {
        free(comp_data);
        return -1;
    }

    /* Allocate output buffer (extra byte for null terminator) */
    byte *out = (byte *)malloc(entry->uncompressed_size + 1);
    if (!out) {
        free(comp_data);
        return -1;
    }

    if (entry->compression == PK3_METHOD_STORE) {
        /* Uncompressed -- just copy */
        memcpy(out, comp_data, entry->uncompressed_size);
    } else if (entry->compression == PK3_METHOD_DEFLATE) {
        /* Decompress */
        int result = PK3_Inflate(comp_data, (int)entry->compressed_size,
                                 out, (int)entry->uncompressed_size);
        if (result < 0 || (dword)result != entry->uncompressed_size) {
            free(out);
            free(comp_data);
            return -1;
        }
    } else {
        /* Unsupported compression method */
        free(out);
        free(comp_data);
        return -1;
    }

    /* Null-terminate for convenience (text files) */
    out[entry->uncompressed_size] = '\0';

    /* Verify CRC */
    dword crc = pk3_crc32(out, (int)entry->uncompressed_size);
    if (crc != entry->crc32) {
        /* CRC mismatch -- data corruption */
        free(out);
        free(comp_data);
        return -1;
    }

    free(comp_data);
    *out_buffer = out;
    return (int)entry->uncompressed_size;
}

int PK3_ReadFileByName(pk3_archive_t *pk3, const char *name, void **out_buffer) {
    const pk3_entry_t *entry = PK3_FindFile(pk3, name);
    if (!entry) return -1;
    return PK3_ReadFile(pk3, entry, out_buffer);
}

int PK3_NumFiles(const pk3_archive_t *pk3) {
    return pk3 ? pk3->num_entries : 0;
}

const pk3_entry_t *PK3_GetEntry(const pk3_archive_t *pk3, int index) {
    if (!pk3 || index < 0 || index >= pk3->num_entries) return NULL;
    return &pk3->entries[index];
}

int PK3_ListFiles(const pk3_archive_t *pk3, const char *directory,
                  const char *extension, const char **names, int max_names) {
    if (!pk3) return 0;

    char norm_dir[PK3_MAX_PATH] = "";
    int dir_len = 0;
    if (directory && directory[0]) {
        pk3_normalize_path(norm_dir, directory, PK3_MAX_PATH);
        dir_len = (int)strlen(norm_dir);
        /* Ensure trailing slash */
        if (dir_len > 0 && norm_dir[dir_len - 1] != '/') {
            norm_dir[dir_len++] = '/';
            norm_dir[dir_len] = '\0';
        }
    }

    char norm_ext[32] = "";
    int ext_len = 0;
    if (extension && extension[0]) {
        pk3_normalize_path(norm_ext, extension, sizeof(norm_ext));
        ext_len = (int)strlen(norm_ext);
    }

    int count = 0;
    for (int i = 0; i < pk3->num_entries && count < max_names; i++) {
        const char *name = pk3->entries[i].name;

        /* Skip directories */
        int name_len = (int)strlen(name);
        if (name_len > 0 && name[name_len - 1] == '/') continue;

        /* Check directory prefix */
        if (dir_len > 0) {
            if (strncmp(name, norm_dir, dir_len) != 0) continue;
        }

        /* Check extension */
        if (ext_len > 0) {
            if (name_len < ext_len) continue;
            if (strcmp(name + name_len - ext_len, norm_ext) != 0) continue;
        }

        if (names) names[count] = name;
        count++;
    }

    return count;
}
