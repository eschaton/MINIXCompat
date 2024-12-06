//
//  MINIXCompat_Filesystem.c
//  MINIXCompat
//
//  Created by Chris Hanson on 11/16/24.
//  Copyright © 2024 Christopher M. Hanson. See file LICENSE for details.
//

#include "MINIXCompat_Filesystem.h"

#include <assert.h>
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


/*! The number of open files MINIX can have at one time. */
#define MINIXCompat_fd_count 20

/*!
 The mapping between MINIX file descriptors and host file descriptors.

 This table is indexed by `minix_fd_t` with empty slots containing -1 and in-use slots containing zero or a positive number.
 */
static int MINIXCompat_fds_host[MINIXCompat_fd_count];


void MINIXCompat_CWD_Initialize(void);


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

    MINIXCompat_fds_host[0] = STDIN_FILENO;
    MINIXCompat_fds_host[1] = STDOUT_FILENO;
    MINIXCompat_fds_host[2] = STDERR_FILENO;

    for (int i = 3; i < MINIXCompat_fd_count; i++) {
        MINIXCompat_fds_host[i] = -1;
    }
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
    return (MINIXCompat_fds_host[minix_fd] != -1);
}

static bool MINIXCompat_fd_IsClosed(minix_fd_t minix_fd)
{
    return (MINIXCompat_fds_host[minix_fd] == -1);
}

static int MINIXCompat_fd_GetHostDescriptor(minix_fd_t minix_fd)
{
    assert(MINIXCompat_fd_IsInRange(minix_fd));

    return MINIXCompat_fds_host[minix_fd];
}

static void MINIXCompat_fd_SetHostDescriptor(minix_fd_t minix_fd, int host_fd)
{
    assert(MINIXCompat_fd_IsInRange(minix_fd));
    assert(MINIXCompat_fd_IsClosed(minix_fd));

    MINIXCompat_fds_host[minix_fd] = host_fd;
}

static void MINIXCompat_fd_ClearHostDescriptor(minix_fd_t minix_fd)
{
    assert(MINIXCompat_fd_IsInRange(minix_fd));
    assert(MINIXCompat_fd_IsOpen(minix_fd));

    MINIXCompat_fds_host[minix_fd] = -1;
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

        int host_fd = open(host_path, host_flags, host_mode);
        if (host_fd >= 0) {
            MINIXCompat_fd_SetHostDescriptor(minix_fd, host_fd);
        } else {
            minix_fd = -MINIXCompat_Errors_MINIXErrorForHostError(errno);
        }

        free(host_path);

        result = minix_fd;
    } else {
        result = -MINIXCompat_Errors_MINIXErrorForHostError(ENFILE);
    }

    return result;
}

int16_t MINIXCompat_File_Close(minix_fd_t minix_fd)
{
    int16_t result;

    assert(MINIXCompat_fd_IsInRange(minix_fd));

    int host_fd = MINIXCompat_fd_GetHostDescriptor(minix_fd);
    assert(host_fd != -1);

    int close_result = close(host_fd);
    if (close_result == -1) {
        result = -MINIXCompat_Errors_MINIXErrorForHostError(errno);
    }

    MINIXCompat_fd_ClearHostDescriptor(minix_fd);

    return result;
}

int16_t MINIXCompat_File_Read(minix_fd_t minix_fd, void *host_buf, int16_t host_buf_size)
{
    int16_t result;

    assert(MINIXCompat_fd_IsInRange(minix_fd));
    assert(MINIXCompat_fd_IsOpen(minix_fd));
    assert(host_buf != NULL);
    assert(host_buf_size > 0);

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

    return result;
}

int16_t MINIXCompat_File_Write(minix_fd_t minix_fd, void *host_buf, int16_t host_buf_size)
{
    int16_t result;

    assert(MINIXCompat_fd_IsInRange(minix_fd));
    assert(MINIXCompat_fd_IsOpen(minix_fd));
    assert(host_buf != NULL);
    assert(host_buf_size > 0);

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

    int host_fd = MINIXCompat_fd_GetHostDescriptor(minix_fd);
    off_t host_offset = minix_offset;
    int host_whence = minix_whence; //xxx translate

    off_t seek_result = lseek(host_fd, host_offset, host_whence);
    if (seek_result < 0) {
        result = -MINIXCompat_Errors_MINIXErrorForHostError(errno);
    } else {
        result = seek_result;
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

    if (host_mode & S_IFREG) minix_mode |= minix_S_IFREG;
    if (host_mode & S_IFBLK) minix_mode |= minix_S_IFBLK;
    if (host_mode & S_IFDIR) minix_mode |= minix_S_IFDIR;
    if (host_mode & S_IFCHR) minix_mode |= minix_S_IFCHR;
    if (host_mode & S_IFIFO) minix_mode |= minix_S_IFIFO;
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
    minix_stat_buf->st_ino = host_stat_buf->st_ino; //xxx translate?
    minix_stat_buf->st_mode = MINIXCompat_File_MINIXStatModeForHostStatMode(host_stat_buf->st_mode);
    minix_stat_buf->st_nlink = host_stat_buf->st_nlink;
    minix_stat_buf->st_uid = host_stat_buf->st_uid; //xxx translate?
    minix_stat_buf->st_gid = host_stat_buf->st_gid; //xxx translate?
    minix_stat_buf->st_rdev = host_stat_buf->st_rdev; //xxx translate?
    minix_stat_buf->st_size = MINIXCompat_File_MINIXStatSizeForHostStatSize(host_stat_buf->st_size);
    minix_stat_buf->minix_st_atime = (minix_time_t) host_stat_buf->st_atimespec.tv_sec;
    minix_stat_buf->minix_st_mtime = (minix_time_t) host_stat_buf->st_mtimespec.tv_sec;
    minix_stat_buf->minix_st_ctime = (minix_time_t) host_stat_buf->st_ctimespec.tv_sec;
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


MINIXCOMPAT_SOURCE_END
