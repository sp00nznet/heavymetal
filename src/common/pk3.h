/*
 * pk3.h -- PK3 (ZIP) archive reader
 *
 * Self-contained ZIP reader for FAKK2's PK3 asset archives.
 * Supports STORE (uncompressed) and DEFLATE (compressed) entries.
 * Read-only -- we never write PK3 files.
 *
 * PK3 files are standard ZIP archives:
 *   - Local file headers + data at the start
 *   - Central directory at the end
 *   - End of central directory record (EOCD) at the very end
 */

#ifndef PK3_H
#define PK3_H

#include "fakk_types.h"
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Maximum files per PK3 and max path length */
#define PK3_MAX_FILES       65536
#define PK3_MAX_PATH        256

/* Compression methods */
#define PK3_METHOD_STORE    0
#define PK3_METHOD_DEFLATE  8

/* A single file entry within a PK3 */
typedef struct {
    char        name[PK3_MAX_PATH]; /* normalized path (lowercase, forward slashes) */
    dword       compressed_size;
    dword       uncompressed_size;
    dword       offset;             /* offset to local file header in archive */
    word        compression;        /* 0=store, 8=deflate */
    dword       crc32;
} pk3_entry_t;

/* A mounted PK3 archive */
typedef struct {
    char            filepath[256];  /* path to the .pk3 file on disk */
    FILE            *fp;            /* open file handle */
    int             num_entries;
    pk3_entry_t     *entries;       /* sorted array of entries */
    dword           hash_table[4096]; /* hash table for fast lookup (index+1, 0=empty) */
} pk3_archive_t;

/* ---- Archive operations ---- */

/* Open a PK3 file and read its central directory */
pk3_archive_t   *PK3_Open(const char *filepath);

/* Close a PK3 archive and free all resources */
void            PK3_Close(pk3_archive_t *pk3);

/* ---- File operations ---- */

/* Find a file entry by name (case-insensitive). Returns NULL if not found. */
const pk3_entry_t *PK3_FindFile(const pk3_archive_t *pk3, const char *name);

/* Read a file's contents into a newly allocated buffer (caller must free).
 * Returns the uncompressed size, or -1 on error. */
int             PK3_ReadFile(pk3_archive_t *pk3, const pk3_entry_t *entry,
                             void **out_buffer);

/* Read a file by name. Convenience wrapper around FindFile + ReadFile. */
int             PK3_ReadFileByName(pk3_archive_t *pk3, const char *name,
                                   void **out_buffer);

/* ---- Listing ---- */

/* Get number of files in the archive */
int             PK3_NumFiles(const pk3_archive_t *pk3);

/* Get entry by index */
const pk3_entry_t *PK3_GetEntry(const pk3_archive_t *pk3, int index);

/* List files matching a directory and/or extension filter.
 * Returns count of matching files. Fills names[] array up to max_names. */
int             PK3_ListFiles(const pk3_archive_t *pk3,
                              const char *directory, const char *extension,
                              const char **names, int max_names);

/* ---- Inflate (DEFLATE decompression) ---- */

/* Decompress DEFLATE data. Returns uncompressed size or -1 on error. */
int             PK3_Inflate(const byte *src, int src_len,
                            byte *dst, int dst_len);

#ifdef __cplusplus
}
#endif

#endif /* PK3_H */
