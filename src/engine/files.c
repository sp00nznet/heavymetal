/*
 * files.c -- Filesystem: PK3 (ZIP) archive management
 *
 * FAKK2 uses the Q3 filesystem: PK3 files are standard ZIP archives
 * stored in the fakk/ game directory. Files are searched in reverse
 * PK3 order (pak4.pk3 overrides pak0.pk3) then loose files.
 *
 * Search order (highest priority first):
 *   1. Loose files in fs_gamedir/
 *   2. pakN.pk3 (highest N first)
 *   3. ...
 *   4. pak0.pk3
 *
 * Original shipped with:
 *   fakk/pak0.pk3  (340 MB -- primary game assets)
 *   fakk/pak1.pk3  (37 MB  -- additional assets)
 *   fakk/pak2.pk3  (10 KB  -- small patch data)
 */

#include "qcommon.h"
#include "../common/pk3.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* =========================================================================
 * Filesystem state
 * ========================================================================= */

#define MAX_SEARCH_PAKS     32
#define MAX_FILE_HANDLES    64

static char fs_basepath[MAX_OSPATH];    /* install directory */
static char fs_gamedir[MAX_OSPATH];     /* "fakk" */

/* Mounted PK3 archives, sorted by priority (highest last for easy override) */
static pk3_archive_t    *fs_paks[MAX_SEARCH_PAKS];
static int              fs_num_paks;

/* Open file handles */
typedef struct {
    qboolean    used;
    qboolean    is_pk3;         /* true if reading from PK3 */
    FILE        *fp;            /* for loose files or pk3 streaming */
    byte        *pk3_data;      /* for pk3 files: decompressed data in memory */
    int         pk3_size;       /* size of decompressed data */
    int         pk3_pos;        /* current read position */
} fs_handle_t;

static fs_handle_t fs_handles[MAX_FILE_HANDLES];

static cvar_t *fs_debug;
static cvar_t *fs_basepath_cvar;
static cvar_t *fs_game;

/* =========================================================================
 * File handle management
 * ========================================================================= */

static fileHandle_t FS_AllocHandle(void) {
    /* Handle 0 is reserved as invalid */
    for (int i = 1; i < MAX_FILE_HANDLES; i++) {
        if (!fs_handles[i].used) {
            memset(&fs_handles[i], 0, sizeof(fs_handle_t));
            fs_handles[i].used = qtrue;
            return (fileHandle_t)i;
        }
    }
    Com_Error(ERR_FATAL, "FS_AllocHandle: no free handles");
    return 0;
}

static fs_handle_t *FS_GetHandle(fileHandle_t f) {
    if (f <= 0 || f >= MAX_FILE_HANDLES) return NULL;
    if (!fs_handles[f].used) return NULL;
    return &fs_handles[f];
}

/* =========================================================================
 * Path construction
 * ========================================================================= */

static void FS_BuildOSPath(char *out, int outsize, const char *base,
                           const char *game, const char *qpath) {
    if (game && game[0]) {
        snprintf(out, outsize, "%s/%s/%s", base, game, qpath);
    } else {
        snprintf(out, outsize, "%s/%s", base, qpath);
    }
    /* Normalize separators */
    for (char *p = out; *p; p++) {
        if (*p == '\\') *p = '/';
    }
}

/* =========================================================================
 * PK3 mounting
 * ========================================================================= */

static void FS_MountPK3(const char *pakfile) {
    if (fs_num_paks >= MAX_SEARCH_PAKS) {
        Com_Printf("WARNING: FS_MountPK3: too many PK3 files\n");
        return;
    }

    pk3_archive_t *pk3 = PK3_Open(pakfile);
    if (!pk3) {
        Com_Printf("WARNING: FS_MountPK3: failed to open %s\n", pakfile);
        return;
    }

    fs_paks[fs_num_paks++] = pk3;
    Com_Printf("  Mounted: %s (%d files)\n", pakfile, PK3_NumFiles(pk3));
}

static int pak_sort_compare(const void *a, const void *b) {
    /* Sort pak filenames so pak0 < pak1 < pak2 etc. */
    return strcmp(*(const char **)a, *(const char **)b);
}

static void FS_LoadPK3s(const char *directory) {
    char pattern[MAX_OSPATH];
    char *pak_files[MAX_SEARCH_PAKS];
    int num_found = 0;

    Com_Printf("Scanning for PK3 files in %s\n", directory);

    /* Find all .pk3 files in the directory */
    snprintf(pattern, sizeof(pattern), "%s", directory);

#ifdef PLATFORM_WINDOWS
    WIN32_FIND_DATAA fd;
    char search[MAX_OSPATH];
    snprintf(search, sizeof(search), "%s/*.pk3", directory);
    HANDLE hFind = FindFirstFileA(search, &fd);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                char *fullpath = (char *)malloc(MAX_OSPATH);
                snprintf(fullpath, MAX_OSPATH, "%s/%s", directory, fd.cFileName);
                pak_files[num_found++] = fullpath;
                if (num_found >= MAX_SEARCH_PAKS) break;
            }
        } while (FindNextFileA(hFind, &fd));
        FindClose(hFind);
    }
#else
    /* POSIX directory scanning */
    DIR *dir = opendir(directory);
    if (dir) {
        struct dirent *ent;
        while ((ent = readdir(dir)) != NULL && num_found < MAX_SEARCH_PAKS) {
            int len = strlen(ent->d_name);
            if (len > 4 && strcmp(ent->d_name + len - 4, ".pk3") == 0) {
                char *fullpath = (char *)malloc(MAX_OSPATH);
                snprintf(fullpath, MAX_OSPATH, "%s/%s", directory, ent->d_name);
                pak_files[num_found++] = fullpath;
            }
        }
        closedir(dir);
    }
#endif

    /* Sort alphabetically (pak0, pak1, pak2...) */
    if (num_found > 1) {
        qsort(pak_files, num_found, sizeof(char *), pak_sort_compare);
    }

    /* Mount each PK3 */
    for (int i = 0; i < num_found; i++) {
        FS_MountPK3(pak_files[i]);
        free(pak_files[i]);
    }
}

/* =========================================================================
 * Initialization
 * ========================================================================= */

void FS_Init(void) {
    char gamepath[MAX_OSPATH];

    fs_debug = Cvar_Get("fs_debug", "0", 0);
    fs_basepath_cvar = Cvar_Get("fs_basepath", Sys_DefaultBasePath(), CVAR_INIT);
    fs_game = Cvar_Get("fs_game", FAKK_GAME_DIR, CVAR_INIT | CVAR_SERVERINFO);

    Q_strncpyz(fs_basepath, fs_basepath_cvar->string, sizeof(fs_basepath));
    Q_strncpyz(fs_gamedir, fs_game->string, sizeof(fs_gamedir));

    Com_Printf("--- FS_Init ---\n");
    Com_Printf("  Base path: %s\n", fs_basepath);
    Com_Printf("  Game dir:  %s\n", fs_gamedir);

    /* Clear handles */
    memset(fs_handles, 0, sizeof(fs_handles));
    fs_num_paks = 0;

    /* Mount PK3 files from game directory */
    FS_BuildOSPath(gamepath, sizeof(gamepath), fs_basepath, fs_gamedir, "");
    /* Remove trailing slash */
    int len = (int)strlen(gamepath);
    if (len > 0 && gamepath[len - 1] == '/') gamepath[len - 1] = '\0';

    FS_LoadPK3s(gamepath);

    Com_Printf("  %d PK3 files mounted\n", fs_num_paks);
    Com_Printf("--- FS_Init complete ---\n");
}

void FS_Shutdown(void) {
    Com_Printf("--- FS_Shutdown ---\n");

    /* Close all open handles */
    for (int i = 1; i < MAX_FILE_HANDLES; i++) {
        if (fs_handles[i].used) {
            FS_FCloseFile((fileHandle_t)i);
        }
    }

    /* Close all PK3 archives */
    for (int i = 0; i < fs_num_paks; i++) {
        PK3_Close(fs_paks[i]);
        fs_paks[i] = NULL;
    }
    fs_num_paks = 0;
}

/* =========================================================================
 * File opening
 * ========================================================================= */

int FS_FOpenFileRead(const char *filename, fileHandle_t *file, qboolean uniqueFILE) {
    char ospath[MAX_OSPATH];
    (void)uniqueFILE;

    if (!filename || !file) return -1;
    *file = 0;

    if (fs_debug && fs_debug->integer) {
        Com_Printf("FS_FOpenFileRead: %s\n", filename);
    }

    /* First: try loose file on disk */
    FS_BuildOSPath(ospath, sizeof(ospath), fs_basepath, fs_gamedir, filename);
    FILE *fp = fopen(ospath, "rb");
    if (fp) {
        fseek(fp, 0, SEEK_END);
        long size = ftell(fp);
        fseek(fp, 0, SEEK_SET);

        fileHandle_t h = FS_AllocHandle();
        fs_handles[h].fp = fp;
        fs_handles[h].is_pk3 = qfalse;
        *file = h;

        if (fs_debug && fs_debug->integer) {
            Com_Printf("  Found loose: %s (%ld bytes)\n", ospath, size);
        }
        return (int)size;
    }

    /* Second: search PK3 archives (reverse order = highest priority first) */
    for (int i = fs_num_paks - 1; i >= 0; i--) {
        const pk3_entry_t *entry = PK3_FindFile(fs_paks[i], filename);
        if (entry) {
            void *data = NULL;
            int size = PK3_ReadFile(fs_paks[i], entry, &data);
            if (size >= 0 && data) {
                fileHandle_t h = FS_AllocHandle();
                fs_handles[h].is_pk3 = qtrue;
                fs_handles[h].pk3_data = (byte *)data;
                fs_handles[h].pk3_size = size;
                fs_handles[h].pk3_pos = 0;
                *file = h;

                if (fs_debug && fs_debug->integer) {
                    Com_Printf("  Found in %s (%d bytes)\n",
                               fs_paks[i]->filepath, size);
                }
                return size;
            }
        }
    }

    if (fs_debug && fs_debug->integer) {
        Com_Printf("  NOT FOUND: %s\n", filename);
    }
    return -1;
}

int FS_FOpenFileWrite(const char *filename, fileHandle_t *file) {
    char ospath[MAX_OSPATH];

    if (!filename || !file) return -1;
    *file = 0;

    FS_BuildOSPath(ospath, sizeof(ospath), fs_basepath, fs_gamedir, filename);

    FILE *fp = fopen(ospath, "wb");
    if (!fp) {
        Com_Printf("FS_FOpenFileWrite: failed to open %s\n", ospath);
        return -1;
    }

    fileHandle_t h = FS_AllocHandle();
    fs_handles[h].fp = fp;
    fs_handles[h].is_pk3 = qfalse;
    *file = h;
    return 0;
}

void FS_FCloseFile(fileHandle_t f) {
    fs_handle_t *fh = FS_GetHandle(f);
    if (!fh) return;

    if (fh->is_pk3) {
        if (fh->pk3_data) free(fh->pk3_data);
    } else {
        if (fh->fp) fclose(fh->fp);
    }

    memset(fh, 0, sizeof(fs_handle_t));
}

/* =========================================================================
 * File reading/writing
 * ========================================================================= */

int FS_Read(void *buffer, int len, fileHandle_t f) {
    fs_handle_t *fh = FS_GetHandle(f);
    if (!fh || !buffer || len <= 0) return 0;

    if (fh->is_pk3) {
        int remaining = fh->pk3_size - fh->pk3_pos;
        if (len > remaining) len = remaining;
        if (len <= 0) return 0;
        memcpy(buffer, fh->pk3_data + fh->pk3_pos, len);
        fh->pk3_pos += len;
        return len;
    } else {
        return (int)fread(buffer, 1, len, fh->fp);
    }
}

int FS_Write(const void *buffer, int len, fileHandle_t f) {
    fs_handle_t *fh = FS_GetHandle(f);
    if (!fh || !buffer || len <= 0) return 0;

    if (fh->is_pk3) return 0; /* can't write to PK3 */
    return (int)fwrite(buffer, 1, len, fh->fp);
}

int FS_Seek(fileHandle_t f, long offset, int origin) {
    fs_handle_t *fh = FS_GetHandle(f);
    if (!fh) return -1;

    if (fh->is_pk3) {
        int newpos;
        switch (origin) {
            case FS_SEEK_SET: newpos = (int)offset; break;
            case FS_SEEK_CUR: newpos = fh->pk3_pos + (int)offset; break;
            case FS_SEEK_END: newpos = fh->pk3_size + (int)offset; break;
            default: return -1;
        }
        if (newpos < 0 || newpos > fh->pk3_size) return -1;
        fh->pk3_pos = newpos;
        return 0;
    } else {
        int whence = (origin == FS_SEEK_SET) ? SEEK_SET :
                     (origin == FS_SEEK_CUR) ? SEEK_CUR : SEEK_END;
        return fseek(fh->fp, offset, whence);
    }
}

int FS_FTell(fileHandle_t f) {
    fs_handle_t *fh = FS_GetHandle(f);
    if (!fh) return 0;
    return fh->is_pk3 ? fh->pk3_pos : (int)ftell(fh->fp);
}

void FS_Flush(fileHandle_t f) {
    fs_handle_t *fh = FS_GetHandle(f);
    if (fh && !fh->is_pk3 && fh->fp) fflush(fh->fp);
}

/* =========================================================================
 * Convenience: read entire file into buffer
 * ========================================================================= */

long FS_ReadFile(const char *qpath, void **buffer) {
    fileHandle_t f;
    int len;

    if (!buffer) {
        /* Just checking if file exists */
        len = FS_FOpenFileRead(qpath, &f, qfalse);
        if (f) FS_FCloseFile(f);
        return len;
    }

    *buffer = NULL;

    len = FS_FOpenFileRead(qpath, &f, qfalse);
    if (len < 0 || !f) return -1;

    /* Allocate buffer with extra byte for null terminator */
    byte *buf = (byte *)Z_Malloc(len + 1);
    FS_Read(buf, len, f);
    buf[len] = '\0';
    FS_FCloseFile(f);

    *buffer = buf;
    return len;
}

void FS_FreeFile(void *buffer) {
    if (buffer) Z_Free(buffer);
}

void FS_WriteFile(const char *qpath, const void *buffer, int size) {
    fileHandle_t f;
    if (FS_FOpenFileWrite(qpath, &f) < 0) return;
    FS_Write(buffer, size, f);
    FS_FCloseFile(f);
}

/* =========================================================================
 * File listing
 * ========================================================================= */

char **FS_ListFiles(const char *directory, const char *extension, int *numfiles) {
    *numfiles = 0;

    /* Count matching files across all PK3s */
    const char *names[8192];
    int total = 0;

    for (int i = 0; i < fs_num_paks && total < 8192; i++) {
        total += PK3_ListFiles(fs_paks[i], directory, extension,
                               names + total, 8192 - total);
    }

    if (total == 0) return NULL;

    /* Allocate array of string pointers + the strings themselves */
    char **list = (char **)Z_Malloc((total + 1) * sizeof(char *));
    for (int i = 0; i < total; i++) {
        list[i] = (char *)Z_Malloc((int)strlen(names[i]) + 1);
        strcpy(list[i], names[i]);
    }
    list[total] = NULL;

    *numfiles = total;
    return list;
}

void FS_FreeFileList(char **filelist) {
    if (!filelist) return;
    for (int i = 0; filelist[i]; i++) {
        Z_Free(filelist[i]);
    }
    Z_Free(filelist);
}

int FS_GetFileList(const char *path, const char *extension, char *listbuf, int bufsize) {
    int numfiles;
    char **filelist = FS_ListFiles(path, extension, &numfiles);
    if (!filelist) return 0;

    int count = 0;
    int pos = 0;
    for (int i = 0; i < numfiles; i++) {
        int len = (int)strlen(filelist[i]);
        if (pos + len + 1 < bufsize) {
            strcpy(listbuf + pos, filelist[i]);
            pos += len + 1;
            count++;
        }
    }

    FS_FreeFileList(filelist);
    return count;
}
