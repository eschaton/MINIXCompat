//
//  MINIXCompat_Filesystem.c
//  MINIXCompat
//
//  Created by Chris Hanson on 11/16/24.
//  Copyright © 2024 Christopher M. Hanson. See file LICENSE for details.
//

#include "MINIXCompat_Filesystem.h"

#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <arpa/inet.h> /* for ntohs et al */
#include <sys/stat.h>

#include "MINIXCompat_Types.h"
#include "MINIXCompat_Errors.h"

#ifndef HTONS
#define HTONS(x) ((x) = htons(x))
#define HTONL(x) ((x) = htonl(x))
#endif

MINIXCOMPAT_SOURCE_BEGIN


/*! Default directory of MINIX installation. */
static const char * const MINIXCOMPAT_DIR_default = "/opt/minix";

/*! Actual directory of MINIX installation. */
static const char *MINIXCOMPAT_DIR = NULL;

/*! Cached length of ``MINIXCOMPAT_DIR``. */
static size_t MINIXCOMPAT_DIR_len = 0;

/*! MINIX current working directory. */
static char *MINIXCOMPAT_PWD = NULL;

/*! Cached length of ``MINIXCOMPAT_PWD``. */
static size_t MINIXCOMPAT_PWD_len = 0;

/*! MINIX current working directory as a host path. */
static char *MINIXCOMPAT_PWD_Host = NULL;

/*! Cached length of ``MINIXCOMPAT_PWD_Host``. */
static size_t MINIXCOMPAT_PWD_Host_len = 0;


/*!
 A MINIX directory entry within a directory file.

 Since MINIX (like UNIX V7) requires use of `open(2)` and `read(2)` to iterate a directory instead of directory-specific system calls, upon `open(2)` of a directory MINIXCompat needs to synthesize its contents. Note that `d_ino` needs swapping as well before being able to be passed back to MINIX.
 */
struct minix_dirent  {
    /*! The inode for this entry; will be 0 for a deleted file or other gap. */
    uint16_t d_ino;

    /*! The name of this entry. */
    char d_name[14];
} MINIXCOMPAT_PACK_STRUCT;
typedef struct minix_dirent minix_dirent_t;


/*! The number of open files MINIX can have at one time. */
#define MINIXCompat_fd_count 20

/*!
 A mapping between MINIX file descriptors and host file descriptors.

 - Note: MINIX 1.5 is unlikely to support >32767 files in a directory due to use of 16-bit `int` in so many places…
 */
typedef struct minix_fdmap {
    /*! The host file descriptor. */
    int host_fd;

    /*! The MINIX file descriptor (also currently used as the index. */
    minix_fd_t minix_fd;

    /*! Whether the fd represents a file or a directory (or hasn't been checked). */
    enum { f_unchecked, f_file, f_directory } f_type;

    /*! If this is a directory, the synthetic directory contents. */
    minix_dirent_t *dir_entries;

    /*! If this is a directory, the number of entries it contains, rounded up to the next multiple of 32; empty entries have a 0 inode. */
    int16_t dir_count;

    /*! If this is a directory, the current directory read offset. */
    minix_off_t dir_offset;
} minix_fdmap_t;

/*!
 The table that maps between MINIX file descriptors and host file descriptors.

 This table is indexed by `minix_fd_t` with empty slots containing a `minix_fd` member of `-1` and in-use slots containing zero or a positive number.

 - Warning: Don't try treat a `host_fd` of `-1` as closed, because opening a directory doesn't keep the host file descriptor around due to the fact that `fopendir(2)` sets the close-on-exec bit, and we don't necessarily want that since it would be a behavior change for MINIX.
 */
static minix_fdmap_t MINIXCompat_fd_table[MINIXCompat_fd_count];


// MARK: - Forward Declarations

static void MINIXCompat_CWD_Initialize(void);

static void MINIXCompat_fd_ClearDescriptorEntry(minix_fd_t minix_fd);

static void MINIXCompat_File_MINIXStatBufForHostStatBuf(minix_stat_t * _Nonnull minix_stat_buf, struct stat * _Nonnull host_stat_buf);
static minix_ino_t MINIXCompat_File_MINIXInodeForHostInode(ino_t host_inode);
static int MINIXCompat_File_HostWhenceForMINIXWhence(minix_whence_t minix_whence);

static int16_t MINIXCompat_Dir_Precache(const char * _Nullable host_path, minix_fd_t minix_fd);
static int16_t MINIXCompat_Dir_CheckIfDirAndCache(const char * _Nonnull host_path, minix_fd_t minix_fd);
static int16_t MINIXCompat_Dir_Read(minix_fd_t minix_fd, void *host_buf, int16_t host_buf_size);
static int16_t MINIXCompat_Dir_Seek(minix_fd_t minix_fd, minix_off_t minix_offset, minix_whence_t minix_whence);


// MARK: - Initialization

void MINIXCompat_Filesystem_Initialize(void)
{
    assert(MINIXCOMPAT_DIR == NULL);

    // Establish the MINIX root directory.

    MINIXCOMPAT_DIR = getenv("MINIXCOMPAT_DIR");
    if (MINIXCOMPAT_DIR == NULL) {
        MINIXCOMPAT_DIR = MINIXCOMPAT_DIR_default;
        setenv("MINIXCOMPAT_DIR", MINIXCOMPAT_DIR, 1);
    }

    // Also cache length of MINIXCOMPAT_DIR to avoid recomputing it.

    MINIXCOMPAT_DIR_len = strlen(MINIXCOMPAT_DIR);

    // Set up the CWD for MINIX and this process.

    MINIXCompat_CWD_Initialize();

    // Set up the decriptor mapping table.

    for (minix_fd_t i = 0; i < MINIXCompat_fd_count; i++) {
        MINIXCompat_fd_ClearDescriptorEntry(i);
    }

    // Set up stdio to match the host.

    MINIXCompat_fd_table[0].host_fd = STDIN_FILENO;
    MINIXCompat_fd_table[0].minix_fd = 0;
    MINIXCompat_fd_table[0].f_type = f_file;

    MINIXCompat_fd_table[1].host_fd = STDOUT_FILENO;
    MINIXCompat_fd_table[1].minix_fd = 1;
    MINIXCompat_fd_table[1].f_type = f_file;

    MINIXCompat_fd_table[2].host_fd = STDERR_FILENO;
    MINIXCompat_fd_table[2].minix_fd = 2;
    MINIXCompat_fd_table[2].f_type = f_file;
}


// MARK: - Conversion to Host Paths

char *MINIXCompat_Filesystem_CopyHostPathForPath(const char *path)
{
    assert(MINIXCOMPAT_DIR != NULL);

    const size_t path_len = strlen(path);
    bool path_is_absolute = ((path_len > 0) && (path[0] == '/'));

    const char *base = path_is_absolute ? MINIXCOMPAT_DIR : MINIXCOMPAT_PWD_Host;
    const size_t base_len = path_is_absolute ? MINIXCOMPAT_DIR_len : MINIXCOMPAT_PWD_Host_len;

    const size_t out_path_len = base_len + 1 /* trailing slash */ + path_len;
    char *out_path = calloc(out_path_len + 1 /* trailing NUL */, sizeof(char));

    strncat(out_path, base, base_len);

    if (!path_is_absolute) {
        strncat(out_path, "/", 1);
    }

    strncat(out_path, path, path_len);

    return out_path;
}


// MARK: - Working Directory

const char *MINIXCompat_Filesystem_CopyWorkingDirectory(void)
{
    assert(MINIXCOMPAT_DIR != NULL);

    return strdup(MINIXCOMPAT_PWD);
}

const char *MINIXCompat_CopyHostWorkingDirectory(void)
{
    assert(MINIXCOMPAT_DIR != NULL);

    return strdup(MINIXCOMPAT_PWD_Host);
}

void MINIXCompat_Filesystem_SetWorkingDirectory(const char *mwd)
{
    assert(MINIXCOMPAT_DIR != NULL);

    char *old_pwd = MINIXCOMPAT_PWD;
    MINIXCOMPAT_PWD = strdup(mwd);
    free(old_pwd);
    MINIXCOMPAT_PWD_len = strlen(MINIXCOMPAT_PWD);

    char *old_host_pwd = MINIXCOMPAT_PWD_Host;
    MINIXCOMPAT_PWD_Host = MINIXCompat_Filesystem_CopyHostPathForPath(mwd);
    free(old_host_pwd);
    MINIXCOMPAT_PWD_Host_len = strlen(MINIXCOMPAT_PWD_Host);

    (void) chdir(MINIXCOMPAT_PWD_Host);
}

static bool MINIXCompat_PathContains(const char *path, const char *subpath)
{
    const size_t path_len = (path == MINIXCOMPAT_DIR) ? MINIXCOMPAT_DIR_len : strlen(path);
    const size_t subpath_len = strlen(subpath);
    if (path_len <= subpath_len) {
        return (strncmp(path, subpath, path_len) == 0);
    } else {
        return false;
    }
}

void MINIXCompat_CWD_Initialize(void)
{
    char *MINIXCOMPAT_PWD = getenv("MINIXCOMPAT_PWD");
    if (MINIXCOMPAT_PWD) {
        // If there's a MINIXCOMPAT_PWD set, use that as it's assumed to be relative to MINIXCOMPAT_DIR.
        // FIXME: Validate MINIXCOMPAT_PWD is inside MINIXCOMPAT_DIR
        MINIXCompat_Filesystem_SetWorkingDirectory(MINIXCOMPAT_PWD);
    } else {
        // If there's no MINIXCOMPAT_PWD, use the host's working directory as long as it's in MINIXCOMPAT_DIR, otherwise fall back to root.
        char *cwd = getcwd(NULL, 0);
        if (MINIXCompat_PathContains(MINIXCOMPAT_DIR, cwd)) {
            // Strip the MINIXCOMPAT_DIR prefix from cwd.
            MINIXCompat_Filesystem_SetWorkingDirectory(&cwd[MINIXCOMPAT_DIR_len]);
        } else {
            // cwd is not inside MINIXCOMPAT_DIR, set it to root.
            MINIXCompat_Filesystem_SetWorkingDirectory("/");
        }
        free(cwd);
    }
}


// MARK: - File Descriptors

static bool MINIXCompat_fd_IsInRange(minix_fd_t minix_fd)
{
    return ((minix_fd >= 0) && (minix_fd < MINIXCompat_fd_count));
}

static bool MINIXCompat_fd_IsOpen(minix_fd_t minix_fd)
{
    return (MINIXCompat_fd_table[minix_fd].minix_fd != -1);
}

static bool MINIXCompat_fd_IsClosed(minix_fd_t minix_fd)
{
    return (MINIXCompat_fd_table[minix_fd].minix_fd == -1);
}

static int MINIXCompat_fd_GetHostDescriptor(minix_fd_t minix_fd)
{
    assert(MINIXCompat_fd_IsInRange(minix_fd));

    return MINIXCompat_fd_table[minix_fd].host_fd;
}

static void MINIXCompat_fd_SetHostDescriptor(minix_fd_t minix_fd, int host_fd)
{
    assert(MINIXCompat_fd_IsInRange(minix_fd));

    MINIXCompat_fd_table[minix_fd].host_fd = host_fd;
    MINIXCompat_fd_table[minix_fd].minix_fd = minix_fd;
    MINIXCompat_fd_table[minix_fd].f_type = f_unchecked;
}

static void MINIXCompat_fd_ClearDescriptorEntry(minix_fd_t minix_fd)
{
    assert(MINIXCompat_fd_IsInRange(minix_fd));

    minix_fdmap_t *entry = &MINIXCompat_fd_table[minix_fd];

    entry->host_fd = -1;
    entry->minix_fd = -1;
    entry->f_type = f_unchecked;
    free(entry->dir_entries);
    entry->dir_entries = NULL;
    entry->dir_count = -1;
    entry->dir_offset = -1;
}

static bool MINIXCompat_fd_IsDirectory(minix_fd_t minix_fd)
{
    assert(MINIXCompat_fd_IsInRange(minix_fd));
    assert(MINIXCompat_fd_IsOpen(minix_fd));

    return (MINIXCompat_fd_table[minix_fd].f_type == f_directory);
}

static minix_fd_t MINIXCompat_fd_FindNextAvailable(void)
{
    for (int minix_fd = 0; minix_fd < MINIXCompat_fd_count; minix_fd++) {
        if (MINIXCompat_fd_GetHostDescriptor(minix_fd) == -1) {
            return minix_fd;
        }
    }

    return -ENFILE;
}


// MARK: - Files

/*! Convert MINIX open flags to host open flags. */
static int MINIXCompat_File_HostOpenFlagsForMINIXOpenFlags(minix_open_flags_t minix_flags)
{
    int host_flags = 0;

    if (minix_flags & minix_O_CREAT)    host_flags |= O_CREAT;
    if (minix_flags & minix_O_EXCL)     host_flags |= O_EXCL;
    if (minix_flags & minix_O_NOCTTY)   host_flags |= O_NOCTTY;
    if (minix_flags & minix_O_TRUNC)    host_flags |= O_TRUNC;
    if (minix_flags & minix_O_APPEND)   host_flags |= O_APPEND;
    if (minix_flags & minix_O_NONBLOCK) host_flags |= O_NONBLOCK;
#if O_RDONLY != 0
    if (minix_flags & minix_O_RDONLY)   host_flags |= O_RDONLY;
#endif
    if (minix_flags & minix_O_WRONLY)   host_flags |= O_WRONLY;
    if (minix_flags & minix_O_RDWR)     host_flags |= O_RDWR;

    return host_flags;
}

static mode_t MINIXCompat_File_HostOpenModeForMINIXOpenMode(minix_mode_t minix_mode)
{
    mode_t host_mode = 0;

    if (minix_mode & minix_S_IFREG) host_mode |= S_IFREG;
    if (minix_mode & minix_S_IFBLK) host_mode |= S_IFBLK;
    if (minix_mode & minix_S_IFDIR) host_mode |= S_IFDIR;
    if (minix_mode & minix_S_IFCHR) host_mode |= S_IFCHR;
    if (minix_mode & minix_S_IFIFO) host_mode |= S_IFIFO;
    if (minix_mode & minix_S_ISUID) host_mode |= S_ISUID;
    if (minix_mode & minix_S_ISGID) host_mode |= S_ISGID;

    if (minix_mode & minix_S_ISVTX) host_mode |= S_ISVTX;

    if (minix_mode & minix_S_IRUSR) host_mode |= S_IRUSR;
    if (minix_mode & minix_S_IWUSR) host_mode |= S_IWUSR;
    if (minix_mode & minix_S_IXUSR) host_mode |= S_IXUSR;

    if (minix_mode & minix_S_IRGRP) host_mode |= S_IRGRP;
    if (minix_mode & minix_S_IWGRP) host_mode |= S_IWGRP;
    if (minix_mode & minix_S_IXGRP) host_mode |= S_IXGRP;

    if (minix_mode & minix_S_IROTH) host_mode |= S_IROTH;
    if (minix_mode & minix_S_IWOTH) host_mode |= S_IWOTH;
    if (minix_mode & minix_S_IXOTH) host_mode |= S_IXOTH;

    return host_mode;
}

static minix_ino_t MINIXCompat_File_MINIXInodeForHostInode(ino_t host_inode)
{
    // If this inode doesn't exist, pass that through unmodified.
    if (host_inode == 0) return 0;

    // Just let truncation happen and hope that it works.
    minix_ino_t minix_inode = host_inode;

    // If truncation results in a 0 minix_ino_t, make something up in a way that's both deterministic and unlikely to collide.
    if (minix_inode == 0) {
        // n.b. the below should only bother generating one of the branches
        if (sizeof(ino_t) == sizeof(uint32_t)) {
            // Add the two 16-bit words.
            uint32_t temp = (  ((host_inode & 0xffff0000) >> 16)
                             + ((host_inode & 0x0000ffff) >> 0));
            minix_inode = (temp & 0x0000ffff);
        } else if (sizeof(ino_t) == sizeof(uint64_t)) {
            // Add the four 16-bit words.
            uint64_t temp = (  ((host_inode & 0xffff000000000000) >> 48)
                             + ((host_inode & 0x0000ffff00000000) >> 32)
                             + ((host_inode & 0x00000000ffff0000) >> 16)
                             + ((host_inode & 0x000000000000ffff) >> 0));
            minix_inode = (temp & 0x000000000000ffff);
        } else {
            // We don't support non-16/32/64-bit inodes yet.
            assert(minix_inode != 0);
        }
    }

    return minix_inode;
}

static int MINIXCompat_File_HostWhenceForMINIXWhence(minix_whence_t minix_whence)
{
    switch (minix_whence) {
        case minix_SEEK_SET: return SEEK_SET;
        case minix_SEEK_CUR: return SEEK_CUR;
        case minix_SEEK_END: return SEEK_END;
    }
}

minix_fd_t MINIXCompat_File_Create(const char *minix_path, minix_mode_t minix_mode)
{
    return MINIXCompat_File_Open(minix_path, minix_O_CREAT | minix_O_TRUNC | minix_O_WRONLY, minix_mode);
}

minix_fd_t MINIXCompat_File_Open(const char *minix_path, int16_t minix_flags, minix_mode_t minix_mode)
{
    int16_t result;

    assert(minix_path != NULL);
    int host_flags = MINIXCompat_File_HostOpenFlagsForMINIXOpenFlags(minix_flags);
    int host_mode = MINIXCompat_File_HostOpenModeForMINIXOpenMode(minix_mode);

    minix_fd_t minix_fd = MINIXCompat_fd_FindNextAvailable();
    if (minix_fd >= 0) {
        char *host_path = MINIXCompat_Filesystem_CopyHostPathForPath(minix_path);

        // Open the file.

        int host_fd = open(host_path, host_flags, host_mode);
        if (host_fd >= 0) {
            // Save the association.

            MINIXCompat_fd_SetHostDescriptor(minix_fd, host_fd);

            // Check and record whether the newly-opened file is a directory, and do any necessary bookkeeping if so.
            // That will only fail if the open itself should fail.

            int16_t diropen_result = MINIXCompat_Dir_CheckIfDirAndCache(host_path, minix_fd);
            if (diropen_result < 0) {
                minix_fd = diropen_result;
                (void) close(host_fd);
                MINIXCompat_fd_ClearDescriptorEntry(minix_fd);
            }
            result = minix_fd;
        } else {
            result = -MINIXCompat_Errors_MINIXErrorForHostError(errno);
        }

        free(host_path);
    } else {
        result = -MINIXCompat_Errors_MINIXErrorForHostError(ENFILE);
    }

    return result;
}

int16_t MINIXCompat_File_Close(minix_fd_t minix_fd)
{
    int16_t result;

    assert(MINIXCompat_fd_IsInRange(minix_fd));
    assert(MINIXCompat_fd_IsOpen(minix_fd));

    int host_fd = MINIXCompat_fd_GetHostDescriptor(minix_fd);

    int close_result = close(host_fd);
    if (close_result == -1) {
        result = -MINIXCompat_Errors_MINIXErrorForHostError(errno);
    } else {
        result = close_result;
    }

    MINIXCompat_fd_ClearDescriptorEntry(minix_fd);

    return result;
}

int16_t MINIXCompat_File_Read(minix_fd_t minix_fd, void *host_buf, int16_t host_buf_size)
{
    int16_t result;

    assert(MINIXCompat_fd_IsInRange(minix_fd));
    assert(MINIXCompat_fd_IsOpen(minix_fd));
    assert(host_buf != NULL);
    assert(host_buf_size > 0);

    if (MINIXCompat_fd_IsDirectory(minix_fd)) {
        // Handle directories specially, since readdir et al are userspace on MINIX.

        result = MINIXCompat_Dir_Read(minix_fd, host_buf, host_buf_size);
    } else {
        int host_fd = MINIXCompat_fd_GetHostDescriptor(minix_fd);
        if (host_fd >= 0) {
            ssize_t bytesread = read(host_fd, host_buf, host_buf_size);
            if (bytesread < 0) {
                result = -MINIXCompat_Errors_MINIXErrorForHostError(errno);
            } else {
                result = bytesread;
            }
        } else {
            result = -MINIXCompat_Errors_MINIXErrorForHostError(ENFILE);
        }
    }

    return result;
}

int16_t MINIXCompat_File_Write(minix_fd_t minix_fd, void *host_buf, int16_t host_buf_size)
{
    int16_t result;

    assert(MINIXCompat_fd_IsInRange(minix_fd));
    assert(MINIXCompat_fd_IsOpen(minix_fd));
    assert(!MINIXCompat_fd_IsDirectory(minix_fd));
    assert(host_buf != NULL);
    assert(host_buf_size >= 0);

    int host_fd = MINIXCompat_fd_GetHostDescriptor(minix_fd);
    if (host_fd >= 0) {
        ssize_t byteswritten = write(host_fd, host_buf, host_buf_size);
        if (byteswritten < 0) {
            result = -MINIXCompat_Errors_MINIXErrorForHostError(errno);
        } else {
            result = byteswritten;
        }
    } else {
        result = -MINIXCompat_Errors_MINIXErrorForHostError(ENFILE);
    }

    return result;
}

int16_t MINIXCompat_File_Seek(minix_fd_t minix_fd, minix_off_t minix_offset, int16_t minix_whence)
{
    int16_t result;

    assert(MINIXCompat_fd_IsInRange(minix_fd));
    assert(MINIXCompat_fd_IsOpen(minix_fd));

    if (MINIXCompat_fd_IsDirectory(minix_fd)) {
        // Handle directories specially, since readdir et al are userspace on MINIX.

        return MINIXCompat_Dir_Seek(minix_fd, minix_offset, minix_whence);
    } else {
        int host_fd = MINIXCompat_fd_GetHostDescriptor(minix_fd);
        off_t host_offset = minix_offset;
        int host_whence = MINIXCompat_File_HostWhenceForMINIXWhence(minix_whence);

        off_t seek_result = lseek(host_fd, host_offset, host_whence);
        if (seek_result < 0) {
            result = -MINIXCompat_Errors_MINIXErrorForHostError(errno);
        } else {
            result = seek_result;
        }
    }

    return result;
}

void MINIXCompat_File_StatSwap(minix_stat_t * _Nonnull minix_stat_buf)
{
    HTONS(minix_stat_buf->st_dev);
    HTONS(minix_stat_buf->st_ino);
    HTONS(minix_stat_buf->st_mode);
    HTONS(minix_stat_buf->st_nlink);
    HTONS(minix_stat_buf->st_uid);
    HTONS(minix_stat_buf->st_gid);
    HTONS(minix_stat_buf->st_rdev);
    HTONL(minix_stat_buf->st_size);
    HTONL(minix_stat_buf->minix_st_atime);
    HTONL(minix_stat_buf->minix_st_mtime);
    HTONL(minix_stat_buf->minix_st_ctime);
}

static minix_mode_t MINIXCompat_File_MINIXStatModeForHostStatMode(mode_t host_mode)
{
    minix_mode_t minix_mode = 0;

    if ((host_mode & S_IFREG) == S_IFREG) minix_mode |= minix_S_IFREG;
    if ((host_mode & S_IFREG) == S_IFBLK) minix_mode |= minix_S_IFBLK;
    if ((host_mode & S_IFDIR) == S_IFDIR) minix_mode |= minix_S_IFDIR;
    if ((host_mode & S_IFCHR) == S_IFCHR) minix_mode |= minix_S_IFCHR;
    if ((host_mode & S_IFIFO) == S_IFIFO) minix_mode |= minix_S_IFIFO;

    if (host_mode & S_ISUID) minix_mode |= minix_S_ISUID;
    if (host_mode & S_ISGID) minix_mode |= minix_S_ISGID;

    if (host_mode & S_ISVTX) minix_mode |= minix_S_ISVTX;

    if (host_mode & S_IRUSR) minix_mode |= minix_S_IRUSR;
    if (host_mode & S_IWUSR) minix_mode |= minix_S_IWUSR;
    if (host_mode & S_IXUSR) minix_mode |= minix_S_IXUSR;

    if (host_mode & S_IRGRP) minix_mode |= minix_S_IRGRP;
    if (host_mode & S_IWGRP) minix_mode |= minix_S_IWGRP;
    if (host_mode & S_IXGRP) minix_mode |= minix_S_IXGRP;

    if (host_mode & S_IROTH) minix_mode |= minix_S_IROTH;
    if (host_mode & S_IWOTH) minix_mode |= minix_S_IWOTH;
    if (host_mode & S_IXOTH) minix_mode |= minix_S_IXOTH;

    return minix_mode;
}

static minix_off_t MINIXCompat_File_MINIXStatSizeForHostStatSize(off_t host_size)
{
    minix_off_t minix_size;
    if (host_size >= 0x7FFFFFFF) {
        minix_size = 0x7FFFFFFF; // clamp to 2GB - 1
    } else {
        minix_size = (minix_off_t) host_size;
    }
    return minix_size;
}

static void MINIXCompat_File_MINIXStatBufForHostStatBuf(minix_stat_t * _Nonnull minix_stat_buf, struct stat * _Nonnull host_stat_buf)
{
    minix_stat_buf->st_dev = host_stat_buf->st_dev; //xxx translate?
    minix_stat_buf->st_ino = MINIXCompat_File_MINIXInodeForHostInode(host_stat_buf->st_ino);
    minix_stat_buf->st_mode = MINIXCompat_File_MINIXStatModeForHostStatMode(host_stat_buf->st_mode);
    minix_stat_buf->st_nlink = host_stat_buf->st_nlink;
    minix_stat_buf->st_uid = host_stat_buf->st_uid; //xxx translate?
    minix_stat_buf->st_gid = host_stat_buf->st_gid; //xxx translate?
    minix_stat_buf->st_rdev = host_stat_buf->st_rdev; //xxx translate?
    minix_stat_buf->st_size = MINIXCompat_File_MINIXStatSizeForHostStatSize(host_stat_buf->st_size);
    minix_stat_buf->minix_st_atime = (minix_time_t) host_stat_buf->st_atime;
    minix_stat_buf->minix_st_mtime = (minix_time_t) host_stat_buf->st_mtime;
    minix_stat_buf->minix_st_ctime = (minix_time_t) host_stat_buf->st_ctime;
}

int16_t MINIXCompat_File_Stat(const char * _Nonnull minix_path, minix_stat_t * _Nonnull minix_stat_buf)
{
    int16_t result;

    assert(minix_path != NULL);
    assert(minix_stat_buf != NULL);

    char *host_path = MINIXCompat_Filesystem_CopyHostPathForPath(minix_path);

    struct stat host_stat_buf;
    int stat_err = stat(host_path, &host_stat_buf);
    if (stat_err == 0) {
        MINIXCompat_File_MINIXStatBufForHostStatBuf(minix_stat_buf, &host_stat_buf);

        // Swap for passing back to MINIX.
        MINIXCompat_File_StatSwap(minix_stat_buf);
        result = 0;
    } else {
        result = -MINIXCompat_Errors_MINIXErrorForHostError(errno);
    }

    return result;
}

int16_t MINIXCompat_File_StatOpen(minix_fd_t minix_fd, minix_stat_t * _Nonnull minix_stat_buf)
{
    int16_t result;

    assert(MINIXCompat_fd_IsInRange(minix_fd));
    assert(MINIXCompat_fd_IsOpen(minix_fd));
    assert(minix_stat_buf != NULL);

    int host_fd = MINIXCompat_fd_GetHostDescriptor(minix_fd);

    struct stat host_stat_buf;
    int stat_err = fstat(host_fd, &host_stat_buf);
    if (stat_err == 0) {
        MINIXCompat_File_MINIXStatBufForHostStatBuf(minix_stat_buf, &host_stat_buf);

        // Swap for passing back to MINIX.
        MINIXCompat_File_StatSwap(minix_stat_buf);
        result = 0;
    } else {
        result = -MINIXCompat_Errors_MINIXErrorForHostError(errno);
    }

    return result;
}

int16_t MINIXCompat_File_Unlink(const char *minix_path)
{
    int16_t result;

    assert(minix_path != NULL);

    char *host_path = MINIXCompat_Filesystem_CopyHostPathForPath(minix_path);

    int unlink_err = unlink(host_path);
    if (unlink_err == 0) {
        result = 0;
    } else {
        result = -MINIXCompat_Errors_MINIXErrorForHostError(errno);
    }

    free(host_path);

    return result;
}

minix_fd_t MINIXCompat_File_Access(const char *minix_path, minix_mode_t minix_mode)
{
    int16_t result;

    assert(minix_path != NULL);

    char *host_path = MINIXCompat_Filesystem_CopyHostPathForPath(minix_path);
    mode_t host_mode = MINIXCompat_File_HostOpenModeForMINIXOpenMode(minix_mode);

    int access_err = access(host_path, host_mode);
    if (access_err == -1) {
        result = -MINIXCompat_Errors_MINIXErrorForHostError(errno);
    } else {
        result = 0;
    }

    free(host_path);

    return result;
}


// MARK: - Directories

/*! Pre-cache a directory for reading. */
static int16_t MINIXCompat_Dir_Precache(const char * _Nullable host_path, minix_fd_t minix_fd)
{
    // Open the directory for iteration.

    DIR *dir = opendir(host_path);
    if (dir == NULL) {
        return -MINIXCompat_Errors_MINIXErrorForHostError(errno);
    }

    // Create a minix_dirent_t for every corresponding struct dirent, resizing our table as needed.

    bool done_reading = false;
    size_t entry_count = 0;
    size_t dircache_count = 32; // start at 32 since most directories have fewer entries, and 32 are one MINIX block

    minix_dirent_t *dircache = calloc(dircache_count, sizeof(minix_dirent_t));

    do {
        errno = 0;  // errno will be unchanged for end-of-directory
        struct dirent *entry = readdir(dir);
        if (entry == NULL) {
            int host_errno = errno;
            if (host_errno != 0) { // errno is unchanged for end-of-directory
                return -MINIXCompat_Errors_MINIXErrorForHostError(host_errno);
            } else {
                // Read has finished successfully. Close the directory.
                closedir(dir);
                dir = NULL;
            }
            done_reading = true;
        } else {
            // Expand the cache if needed.

            if (entry_count >= dircache_count) {
                // Always increase size by one block's worth so it doesn't grow too quickly.
                size_t new_dircache_count = (dircache_count + 32);
                dircache = realloc(dircache, new_dircache_count * sizeof(minix_dirent_t));

                // Zero-fill new entries.
                memset(&dircache[dircache_count], 0, 32);

                // Update number of available entries.
                dircache_count = new_dircache_count;
            }

            // MINIX just wants inode and 14-character name.

            dircache[entry_count].d_ino = htons(MINIXCompat_File_MINIXInodeForHostInode(entry->d_ino));
            strncpy(dircache[entry_count].d_name, entry->d_name, 14);

            entry_count += 1;
        }
    } while (!done_reading);

    // Save the MINIX directory entries in the descriptor map.

    MINIXCompat_fd_table[minix_fd].dir_entries = dircache;
    MINIXCompat_fd_table[minix_fd].dir_count = dircache_count;
    MINIXCompat_fd_table[minix_fd].dir_offset = 0;

    return 0;
}

/*! A version of `stat(2)` that handles `EINTR` and returns `-errno` instead of just `-1` on error. */
static int stat_without_EINTR(const char * _Nonnull path, struct stat * _Nonnull sbuf)
{
    int result;
    bool stat_done = false;

    do {
        result = stat(path, sbuf);
        if (result == -1) {
            if (errno != EINTR) {
                result = -EINTR;
                stat_done = true;
            } else {
                // Just loop until success or a real failure. Thanks, UNIX.
            }
        } else {
            stat_done = true;
        }
    } while (!stat_done);

    return result;
}

static int16_t MINIXCompat_Dir_CheckIfDirAndCache(const char * _Nonnull host_path, minix_fd_t minix_fd)
{
    int16_t result;

    bool is_directory;
    struct stat sbuf;
    int stat_result = stat_without_EINTR(host_path, &sbuf);
    if (stat_result == 0) {
        // Indicate whether the fd corresponds to a directory.
        is_directory = S_ISDIR(sbuf.st_mode);
        MINIXCompat_fd_table[minix_fd].f_type = is_directory ? f_directory : f_file;
        result = 0;
    } else {
        is_directory = false;
        result = MINIXCompat_Errors_MINIXErrorForHostError(-stat_result);
    }

    // If that was successful, and the file is a directory, also pre-cache its entries at open(2) time.
    // NOTE: Since we're in the middle of opening, don't use IsOpen, IsDirectory, etc.

    if ((result == 0) && is_directory) {
        // If the fd is a directory, pre-cache its entries, failing the open if that fails.
        int16_t precache_result = MINIXCompat_Dir_Precache(host_path, minix_fd);
        result = precache_result;
    }

    return result;
}

/*! Read and return many entries from the directory into \a host_buf as are appropriate for \a host_buf_size. */
static int16_t MINIXCompat_Dir_Read(minix_fd_t minix_fd, void *host_buf, int16_t host_buf_size)
{
    int16_t result;
    minix_fdmap_t *entry = &MINIXCompat_fd_table[minix_fd];

    const minix_off_t max_off_plus_one = entry->dir_count * sizeof(minix_dirent_t);
    const minix_off_t cur_off = entry->dir_offset;

    if ((cur_off + host_buf_size) <= max_off_plus_one) {
        uint8_t *raw_dir_entries = (uint8_t *)entry->dir_entries;
        memcpy(host_buf, raw_dir_entries + cur_off, host_buf_size);
        entry->dir_offset += host_buf_size;
        result = host_buf_size;
    } else {
        result = MINIXCompat_Errors_MINIXErrorForHostError(EIO);
    }

    return result;
}

/*! Seek within a directory. */
static int16_t MINIXCompat_Dir_Seek(minix_fd_t minix_fd, minix_off_t minix_offset, minix_whence_t minix_whence)
{
    int16_t result;

    assert(MINIXCompat_fd_IsInRange(minix_fd));
    assert(MINIXCompat_fd_IsOpen(minix_fd));
    assert(MINIXCompat_fd_IsDirectory(minix_fd));

    minix_fdmap_t *entry = &MINIXCompat_fd_table[minix_fd];

    const minix_off_t min_off = 0;
    const minix_off_t max_off = entry->dir_count * sizeof(minix_dirent_t) - 1;
    minix_off_t new_off;

    switch (minix_whence) {
        case minix_SEEK_SET: {
            new_off = min_off + minix_offset;
        } break;

        case minix_SEEK_CUR: {
            new_off = entry->dir_offset + minix_offset;
        } break;

        case minix_SEEK_END: {
            new_off = max_off + minix_offset;
        } break;
    }

    if ((new_off < 0) || (new_off > max_off)) {
        result = -minix_EINVAL;
    } else {
        entry->dir_offset = new_off;
        result = 0;
    }

    return result;
}


MINIXCOMPAT_SOURCE_END
