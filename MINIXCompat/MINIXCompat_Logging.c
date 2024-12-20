//
//  MINIXCompat_Logging.c
//  MINIXCompat
//
//  Created by Chris Hanson on 12/19/24.
//  Copyright © 2024 Christopher M. Hanson. All rights reserved.
//

#include "MINIXCompat_Logging.h"

#include <assert.h>
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
static FILE *MINIXCompat_Log_File = NULL;


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

    if (MINIXCompat_Log_File != NULL) {
        fclose(MINIXCompat_Log_File);
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

    MINIXCompat_Log_File = fopen(MINIXCompat_Log_Path, "w");
    assert(MINIXCompat_Log_File != NULL);
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

    fprintf(MINIXCompat_Log_File, "%d: ", curpid);
    vfprintf(MINIXCompat_Log_File, fmt, args);
    size_t fmt_len = strlen(fmt);
    if (fmt[fmt_len - 1] != '\n') {
        fprintf(MINIXCompat_Log_File, "\n");
    }
}


#endif /* DEBUG */


MINIXCOMPAT_SOURCE_END
