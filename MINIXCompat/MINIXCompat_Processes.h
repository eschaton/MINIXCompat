//
//  MINIXCompat_Processes.h
//  MINIXCompat
//
//  Created by Chris Hanson on 11/30/24.
//  Copyright © 2024 Christopher M. Hanson. See file LICENSE for details.
//

#ifndef MINIXCompat_Processes_h
#define MINIXCompat_Processes_h

#include <stdint.h>

#include "MINIXCompat_Types.h"


MINIXCOMPAT_HEADER_BEGIN


/*! The type of a MINIX process ID. */
typedef int16_t minix_pid_t;


/*! Initialize the Processes subsystem. */
MINIXCOMPAT_EXTERN void MINIXCompat_Processes_Initialize(void);


/*! Get the process ID and parent process ID for the running MINIX process. */
MINIXCOMPAT_EXTERN void MINIXCompat_Processes_GetProcessIDs(minix_pid_t * _Nonnull minix_pid, minix_pid_t * _Nonnull minix_ppid);

/*!
 Fork the current process.

 All of this process's state is copied to the new process.

 - Returns: On success, 0 to the new process and the pid of the new process to the parent process.
            On failure, `-errno` to the parent process (no new process is started).
 */
MINIXCOMPAT_EXTERN minix_pid_t MINIXCompat_Processes_fork(void);

/*!
 Wait for a child process to exit and record its status.
 */
MINIXCOMPAT_EXTERN minix_pid_t MINIXCompat_Processes_wait(int16_t * _Nonnull minix_stat_loc);


/*! The type of a MINIX signal. */
typedef enum minix_signal: int16_t {
    minix_SIGHUP    =  1,
    minix_SIGINT    =  2,
    minix_SIGQUIT   =  3,
    minix_SIGILL    =  4,
    minix_SIGTRAP   =  5,
    minix_SIGABRT   =  6,
    minix_SIGIOT    =  6,
    minix_SIGUNUSED =  7,
    minix_SIGFPE    =  8,
    minix_SIGKILL   =  9,
    minix_SIGUSR1   = 10,
    minix_SIGSEGV   = 11,
    minix_SIGUSR2   = 12,
    minix_SIGPIPE   = 13,
    minix_SIGALRM   = 14,
    minix_SIGTERM   = 15,
    minix_SIGSTKFLT = 16,
} minix_signal_t;


/*! The type of a MINIX signal handler. (Really a 68K-side pointer to a function that takes an `int16_t`. */
typedef m68k_address_t minix_sighandler_t;

/*! The default MINIX signal handler. */
MINIXCOMPAT_EXTERN minix_sighandler_t minix_SIG_DFL;

/*! The ignoring MINIX signal handler. */
MINIXCOMPAT_EXTERN minix_sighandler_t minix_SIG_IGN;

/*! The error MINIX signal handler. */
MINIXCOMPAT_EXTERN minix_sighandler_t minix_SIG_ERR;


/*!
 Set up a signal handler for the current process.
 */
MINIXCOMPAT_EXTERN minix_sighandler_t MINIXCompat_Processes_signal(minix_signal_t minix_signal, minix_sighandler_t handler);


/*!
 Send a signal to a process.
 */
MINIXCOMPAT_EXTERN int16_t MINIXCompat_Processes_kill(minix_pid_t minix_pid, minix_signal_t minix_signal);


/*!
 Handle any pending signals.
 */
MINIXCOMPAT_EXTERN void MINIXCompat_Processes_HandlePendingSignals(void);


/*!
 Load and run a compiled MINIX executable or interpreter file the same way `exec(2)` would.

 - Parameters:
   - executable_path: Absolute or relative MINIX path to executable file.
   - executbale_path_len: The length of  `minix_path`.
   - stack_on_host: The 0-based, Motorola-order stack to start with, which contains `argc`, `argv`, `envp`, and their contents.
   - stack_size: The size of `stack_on_host`.

 - Warning: This resets the emulation environment and should only be invoked after startup or a `fork(2)` call.
 */
MINIXCOMPAT_EXTERN int16_t MINIXCompat_Processes_ExecuteWithStackBlock(const char *executable_path, void *stack_on_host, int16_t stack_size);


/*!
 Load and run a compiled MINIX executable or interpreter file the same way `exec(2)` would.

 - Parameters:
   - executable_path: Absolute or relative MINIX path to executable file.
   - executbale_path_len: The length of  `minix_path`.
   - argc: The number of elements in `argv`.
   - argv: The executable's arguments.
   - envp: The executable's environment.

 - Warning: This resets the emulation environment and should only be invoked after startup or a `fork(2)` call.
 */
MINIXCOMPAT_EXTERN int16_t MINIXCompat_Processes_ExecuteWithHostParams(const char *executable_path, int16_t argc, char * _Nullable * _Nonnull argv, char * _Nullable * _Nonnull envp);


MINIXCOMPAT_HEADER_END


#endif /* MINIXCompat_Processes_h */
