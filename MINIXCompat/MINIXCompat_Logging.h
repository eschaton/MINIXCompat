//
//  MINIXCompat_Logging.h
//  MINIXCompat
//
//  Created by Chris Hanson on 12/19/24.
//  Copyright © 2024 Christopher M. Hanson. All rights reserved.
//

#ifndef MINIXCompat_Logging_h
#define MINIXCompat_Logging_h

#include "MINIXCompat_Types.h"


MINIXCOMPAT_HEADER_BEGIN


#if DEBUG


/*! Initialize the logging subsystem. */
MINIXCOMPAT_EXTERN void MINIXCompat_Log_Initialize(void);


/*! Log some information to the per-process log file. */
MINIXCOMPAT_EXTERN void MINIXCompat_Log(const char *fmt, ...) __printflike(1, 2);;

/*! Log some information to the per-process log file (callable). */
MINIXCOMPAT_EXTERN void MINIXCompat_Logv(const char *fmt, va_list args);


#else


#define MINIXCompat_Log_Initialize()
#define MINIXCompat_Log(fmt, ...)
#define MINIXCompat_Logv(fmt, args)


#endif


MINIXCOMPAT_HEADER_END


#endif /* MINIXCompat_Logging_h */
