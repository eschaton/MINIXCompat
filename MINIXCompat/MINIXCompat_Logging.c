//
//  MINIXCompat_Logging.c
//  MINIXCompat
//
//  Created by Chris Hanson on 12/19/24.
//  Copyright © 2024 Christopher M. Hanson. All rights reserved.
//

#include "MINIXCompat_Logging.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>


MINIXCOMPAT_SOURCE_BEGIN


#if DEBUG


/*! The directory in which MINIXCompat logs are written. */
static char *MINIXCOMPAT_LOG_DIR = NULL;

/*! Cached length of `MINIXCOMPAT_LOG_DIR`. */
static size_t MINIXCOMPAT_LOG_DIR_len = 0;

/*! The ID of the process that's logging. This is used to open a new log across a `fork(2)`. */
static pid_t MINIXCompat_Log_pid = 0;


/*! The path to the log. */
static char MINIXCompat_Log_Path[1024] = {0};


/*! The file being logged to. */
static int MINIXCompat_Log_File = -1;


/*! Create a new log file when needed. */
static void MINIXCompat_Log_New(void);


void MINIXCompat_Log_Initialize(void)
{
    MINIXCompat_Log_pid = getpid();
    MINIXCompat_Log_New();
}


void MINIXCompat_Log_New(void)
{
    // If there's already a log (say one that's been inherited across a fork(2), close it.

    if (MINIXCompat_Log_File != -1) {
        close(MINIXCompat_Log_File);
        MINIXCompat_Log_File = -1;
    }

    // Get the directory to log into, using /tmp if none is specified.

    if (MINIXCOMPAT_LOG_DIR == NULL) {
        MINIXCOMPAT_LOG_DIR = getenv("MINIXCOMPAT_LOG_DIR");
        if (MINIXCOMPAT_LOG_DIR == NULL) {
            MINIXCOMPAT_LOG_DIR = "/tmp";
        }
        MINIXCOMPAT_LOG_DIR_len = strlen(MINIXCOMPAT_LOG_DIR);
    }

    // Construct a path to the log.

    char name[64] = {0};
    snprintf(name, 64, "MINIXCompat.%d", MINIXCompat_Log_pid);

    MINIXCompat_Log_Path[0] = '\0';
    strncat(MINIXCompat_Log_Path, MINIXCOMPAT_LOG_DIR, 1024);
    if (MINIXCOMPAT_LOG_DIR[MINIXCOMPAT_LOG_DIR_len - 1] != '/') {
        strncat(MINIXCompat_Log_Path, "/", 1024);
    }
    strncat(MINIXCompat_Log_Path, name, 1024);

    // Open the log and crash if we can't.

    do {
        MINIXCompat_Log_File = open(MINIXCompat_Log_Path,
                                    O_WRONLY | O_CREAT|O_EXCL|O_CLOEXEC,
                                    S_IRUSR|S_IWUSR | S_IRGRP | S_IROTH);
    } while ((MINIXCompat_Log_File == -1) && (errno != EINTR));

    if (MINIXCompat_Log_File == -1) {
        int log_errno = errno;
        fprintf(stderr, "%d: failed to create log file at '%s', errno = %d" "\n", getpid(), MINIXCompat_Log_Path, log_errno);
        fflush(stderr);
    }
    assert(MINIXCompat_Log_File != -1);

    // Put a header on the log.

    MINIXCompat_Log("Opened log.");
}


static void MINIXCompat_Log_WriteBuffer(const void *buf, const size_t buf_size)
{
    ssize_t written;
    size_t remaining = buf_size;
    do {
        written = write(MINIXCompat_Log_File, &buf[buf_size - remaining], remaining);
        if (written > 0) {
            remaining -= written;
        }
    } while (((written == -1) && (errno == EINTR))
             && (remaining > 0));

    if (written == -1) {
        int saved_errno = errno;
        fprintf(stderr, "%d: Write to log failed: %d\n", getpid(), saved_errno);
    }

    assert(written != -1);
}

static void MINIXCompat_Log_WriteString(const char *str)
{
    const size_t str_len = strlen(str);
    MINIXCompat_Log_WriteBuffer(str, str_len);
}


void MINIXCompat_Log(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    MINIXCompat_Logv(fmt, ap);
    va_end(ap);
}

void MINIXCompat_Logv(const char *fmt, va_list args)
{
    pid_t curpid = getpid();
    if (MINIXCompat_Log_pid != curpid) {
        MINIXCompat_Log_pid = curpid;
        MINIXCompat_Log_New();
    }

    char logbuf[1024];
    const char * const newline = "\n";

    snprintf(logbuf, 1024, "%d: ", curpid);
    MINIXCompat_Log_WriteString(logbuf);

    vsnprintf(logbuf, 1024, fmt, args);
    MINIXCompat_Log_WriteString(logbuf);

    size_t fmt_len = strlen(fmt);
    if (fmt[fmt_len - 1] != '\n') {
        MINIXCompat_Log_WriteString(newline);
    }
}


#endif /* DEBUG */


MINIXCOMPAT_SOURCE_END
