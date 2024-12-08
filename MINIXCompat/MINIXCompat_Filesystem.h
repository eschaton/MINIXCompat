//
//  MINIXCompat_Filesystem.h
//  MINIXCompat
//
//  Created by Chris Hanson on 11/16/24.
//  Copyright © 2024 Christopher M. Hanson. See file LICENSE for details.
//

#ifndef MINIXCompat_Filesystem_h
#define MINIXCompat_Filesystem_h

#include "MINIXCompat_Types.h"


MINIXCOMPAT_HEADER_BEGIN


/*! Initialize the "filesystem" subsystem. */
MINIXCOMPAT_EXTERN void MINIXCompat_Filesystem_Initialize(void);

/*!
 Construct a full host path for the given MINIX path.

 - Note: Always returns an absolute path, using  the MINIX current working directory into account if \a path is not absolute.
 */
MINIXCOMPAT_EXTERN char *MINIXCompat_Filesystem_CopyHostPathForPath(const char *path);

/*! Copy the current MINIX working directory. */
MINIXCOMPAT_EXTERN const char *MINIXCompat_Filesystem_CopyWorkingDirectory(void);

/*! Copy the current MINIX working directory as a host path. */

/*! Set the current MINIX working directory. */
MINIXCOMPAT_EXTERN void MINIXCompat_Filesystem_SetWorkingDirectory(const char *mwd);


/*! A MINIX file descriptor, which must always be positive; a negative value represents `-errno`. */
typedef int16_t minix_fd_t;


/*! MINIX-side file open flags */
typedef enum minix_open_flags : uint16_t {
    minix_O_CREAT    = 00100,
    minix_O_EXCL     = 00200,
    minix_O_NOCTTY   = 00400,
    minix_O_TRUNC    = 01000,
    minix_O_APPEND   = 02000,
    minix_O_NONBLOCK = 04000,
    minix_O_RDONLY   = 00000,
    minix_O_WRONLY   = 00001,
    minix_O_RDWR     = 00002,
} minix_open_flags_t;


/*! MINIX-side stat mode flags */
typedef enum minix_mode : uint16_t {
    minix_S_IFMT  = 0170000,
    minix_S_IFREG = 0100000,
    minix_S_IFBLK = 0060000,
    minix_S_IFDIR = 0040000,
    minix_S_IFCHR = 0020000,
    minix_S_IFIFO = 0010000,
    minix_S_ISUID = 0004000,
    minix_S_ISGID = 0002000,

    minix_S_ISVTX =   01000,

    minix_S_IRWXU =   00700,
    minix_S_IRUSR =   00400,
    minix_S_IWUSR =   00200,
    minix_S_IXUSR =   00100,

    minix_S_IRWXG =   00070,
    minix_S_IRGRP =   00040,
    minix_S_IWGRP =   00020,
    minix_S_IXGRP =   00010,

    minix_S_IRWXO =   00007,
    minix_S_IROTH =   00004,
    minix_S_IWOTH =   00002,
    minix_S_IXOTH =   00001,
} minix_mode_t;


typedef uint16_t minix_dev_t;
typedef uint16_t minix_ino_t;
typedef int16_t minix_uid_t;
typedef int16_t minix_gid_t;
typedef int32_t minix_off_t;
typedef int32_t minix_time_t;


/*! A MINIX stat structure, in either host or MINIX byte order.. */
typedef struct minix_stat {
    minix_dev_t st_dev;
    minix_ino_t st_ino;
    minix_mode_t st_mode;
    int16_t st_nlink;
    minix_uid_t st_uid;
    minix_gid_t st_gid;
    minix_dev_t st_rdev;
    minix_off_t st_size;
    minix_time_t minix_st_atime; // Prefixed to avoid macros on Darwin.
    minix_time_t minix_st_mtime; // Prefixed to avoid macros on Darwin.
    minix_time_t minix_st_ctime; // Prefixed to avoid macros on Darwin.
} MINIXCOMPAT_PACK_STRUCT minix_stat_t;


/*! The position from which to seek. */
typedef enum minix_whence : int16_t {
    minix_SEEK_SET = 0,
    minix_SEEK_CUR = 1,
    minix_SEEK_END = 2,
} minix_whence_t;


/*!
 Create the file at the given MINIX relative or absolute path, with the given MINIX mode.

 - Returns: A MINIX file descriptor, or `-errno` upon error.
 */
MINIXCOMPAT_EXTERN minix_fd_t MINIXCompat_File_Create(const char *minix_path, minix_mode_t minix_mode);

/*!
 Open the file at the given MINIX relative or absolute path, with the given MINIX flags (and mode, if appropriate).

 - Returns: A MINIX file descriptor, or `-errno` upon error.
 */
MINIXCOMPAT_EXTERN minix_fd_t MINIXCompat_File_Open(const char *minix_path, int16_t minix_flags, minix_mode_t minix_mode);

/*! Close the file with the given MINIX file descriptor. */
MINIXCOMPAT_EXTERN int16_t MINIXCompat_File_Close(minix_fd_t fd);

/*! Reads the specified amount of data from the given file descriptor into the given host-side buffer. Returns the number of bytes read or `-errno` on error. */
MINIXCOMPAT_EXTERN int16_t MINIXCompat_File_Read(minix_fd_t fd, void * _Nonnull buf, int16_t buf_size);

MINIXCOMPAT_EXTERN int16_t MINIXCompat_File_Write(minix_fd_t fd, void * _Nonnull buf, int16_t buf_size);

MINIXCOMPAT_EXTERN int16_t MINIXCompat_File_Seek(minix_fd_t fd, minix_off_t offset, minix_whence_t minix_whence);

MINIXCOMPAT_EXTERN void MINIXCompat_File_StatSwap(minix_stat_t * _Nonnull minix_stat_buf);

MINIXCOMPAT_EXTERN int16_t MINIXCompat_File_Stat(const char * _Nonnull minix_path, minix_stat_t * _Nonnull minix_stat_buf);

MINIXCOMPAT_EXTERN int16_t MINIXCompat_File_StatOpen(minix_fd_t minix_fd, minix_stat_t * _Nonnull minix_stat_buf);

MINIXCOMPAT_EXTERN int16_t MINIXCompat_File_Unlink(const char *minix_path);

MINIXCOMPAT_EXTERN minix_fd_t MINIXCompat_File_Access(const char *minix_path, minix_mode_t minix_mode);


MINIXCOMPAT_HEADER_END


#endif /* MINIXCompat_Filesystem_h */
