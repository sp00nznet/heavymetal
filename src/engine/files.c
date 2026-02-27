/*
 * files.c -- Filesystem: PK3 (ZIP) archive management
 *
 * FAKK2 uses the Q3 filesystem: PK3 files are standard ZIP archives
 * stored in the fakk/ game directory. Files are searched in reverse
 * PK3 order (pak4.pk3 overrides pak0.pk3) then loose files.
 *
 * Original shipped with:
 *   fakk/pak0.pk3  (340 MB -- primary game assets)
 *   fakk/pak1.pk3  (37 MB  -- additional assets)
 *   fakk/pak2.pk3  (10 KB  -- small patch data)
 *   fakk/pak3.pk3  (not on disc, likely from patch)
 *   fakk/pak4.pk3  (not on disc, likely from patch)
 */

#include "qcommon.h"
#include <string.h>
#include <stdio.h>

/* =========================================================================
 * TODO: Implement PK3 filesystem
 *
 * Required functionality:
 * - Mount PK3 (ZIP) files from game directory
 * - Virtual file path resolution (search PK3s then loose files)
 * - File reading from both PK3 and loose files
 * - File listing with extension filtering
 * - Pure server file validation
 *
 * The minizip library (or similar) will be needed for ZIP decompression.
 * ========================================================================= */

static char fs_gamedir[MAX_OSPATH];

void FS_Init(void) {
    Q_strncpyz(fs_gamedir, FAKK_GAME_DIR, sizeof(fs_gamedir));
    Com_Printf("Filesystem initialized: game directory '%s'\n", fs_gamedir);

    /* TODO: Scan for and mount PK3 files */
    /* TODO: Mount fakk/pak0.pk3 through pakN.pk3 */
}

void FS_Shutdown(void) {
    Com_Printf("Filesystem shutdown\n");
    /* TODO: Close all open PK3 handles */
}

int FS_FOpenFileRead(const char *filename, fileHandle_t *file, qboolean uniqueFILE) {
    (void)uniqueFILE;
    /* TODO: Search PK3 archives then loose files */
    Com_DPrintf("FS_FOpenFileRead: %s (stub)\n", filename);
    *file = 0;
    return -1;
}

int FS_FOpenFileWrite(const char *filename, fileHandle_t *file) {
    Com_DPrintf("FS_FOpenFileWrite: %s (stub)\n", filename);
    *file = 0;
    return -1;
}

void FS_FCloseFile(fileHandle_t f) {
    (void)f;
    /* TODO */
}

int FS_Read(void *buffer, int len, fileHandle_t f) {
    (void)buffer; (void)len; (void)f;
    return 0;
}

int FS_Write(const void *buffer, int len, fileHandle_t f) {
    (void)buffer; (void)len; (void)f;
    return 0;
}

int FS_Seek(fileHandle_t f, long offset, int origin) {
    (void)f; (void)offset; (void)origin;
    return -1;
}

int FS_FTell(fileHandle_t f) {
    (void)f;
    return 0;
}

void FS_Flush(fileHandle_t f) {
    (void)f;
}

char **FS_ListFiles(const char *directory, const char *extension, int *numfiles) {
    (void)directory; (void)extension;
    *numfiles = 0;
    return NULL;
}

void FS_FreeFileList(char **filelist) {
    (void)filelist;
}

int FS_GetFileList(const char *path, const char *extension, char *listbuf, int bufsize) {
    (void)path; (void)extension; (void)listbuf; (void)bufsize;
    return 0;
}

long FS_ReadFile(const char *qpath, void **buffer) {
    (void)qpath;
    *buffer = NULL;
    return -1;
}

void FS_FreeFile(void *buffer) {
    if (buffer) Z_Free(buffer);
}

void FS_WriteFile(const char *qpath, const void *buffer, int size) {
    (void)qpath; (void)buffer; (void)size;
}
