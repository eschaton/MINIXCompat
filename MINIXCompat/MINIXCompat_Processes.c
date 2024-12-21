//
//  MINIXCompat_Processes.c
//  MINIXCompat
//
//  Created by Chris Hanson on 11/30/24.
//  Copyright © 2024 Christopher M. Hanson. See file LICENSE for details.
//

#include "MINIXCompat_Processes.h"

#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <arpa/inet.h> /* for ntohs et al */
#include <sys/stat.h>
#include <sys/wait.h>

#include "MINIXCompat_Types.h"
#include "MINIXCompat.h"
#include "MINIXCompat_Emulation.h"
#include "MINIXCompat_Errors.h"
#include "MINIXCompat_Executable.h"
#include "MINIXCompat_Filesystem.h"
#include "MINIXCompat_Logging.h"


#if DEBUG

/*!
 Uncomment to debug forking.

 This will spin both the parent and child in loops immediately following the fork, allowing debugger attachment and resumption.
 */
//#define DEBUG_FORK 1

/*! Uncomment to debug signal handling. */
//#define DEBUG_SIGNAL 1

/*! Uncomment to trace process-related system calls. */
#define DEBUG_PROCESS_SYSCALLS 1

#endif


MINIXCOMPAT_SOURCE_BEGIN

/*!
 A mapping between MINIX and host process IDs.

 MINIX uses 16-bit PIDs while the host may use 32-bit or even 64-bit PIDs, so we need to maintain a mapping.
*/
typedef struct minix_process_mapping {
    pid_t host_pid;
    minix_pid_t minix_pid;
} minix_process_mapping_t;

/*!
 The table that maps between MINIX and host process IDs.

 There are unlikely to be enough entries in the table for search speed to matter, so we just maintain it unordered.

 Note that MINIX process IDs start at 3, since 0 is MM, 1 is FS, and 2 is init.
 */
static minix_process_mapping_t *MINIXCompat_ProcessTable = NULL;

/*! Maximum number of entries in the MINIX process table. */
static size_t MINIXCompat_ProcessTable_Size = 0;


/*! The MINIX process ID equivalent to the host's. */
static minix_pid_t minix_self_pid = 0;

/*! The MINIX parent process ID equivalent to the host's. */
static minix_pid_t minix_self_ppid = 0;

/*! The next process ID to allocate. */
static minix_pid_t minix_next_pid = 0;


/*! The signal handler table. */
static minix_sighandler_t minix_signal_handlers[17] = { 0x00000000 /* minix_SIG_DFL */ };

minix_sighandler_t minix_SIG_DFL = 0x00000000;
minix_sighandler_t minix_SIG_IGN = 0x00000001;
minix_sighandler_t minix_SIG_ERR = 0xFFFFFFFF;


/*! Initialize the processes subsystem. */
void MINIXCompat_Processes_Initialize(void)
{
    // We probably won't need any more than this, since MINIX sets `NR_PROCS` to this value.
    MINIXCompat_ProcessTable_Size = 32;
    MINIXCompat_ProcessTable = calloc(MINIXCompat_ProcessTable_Size, sizeof(minix_process_mapping_t));

    pid_t host_self_pid = getpid();
    pid_t host_self_ppid = getppid();

    /*
     The lowest MINIX pid for a user process is 2, since 0 and 1 are MM and FS.
     However, 2 is init. Pretending the MINIX process is launched in a terminal,
     there should be the following processes:

     3: sh started by init to run /etc/rc
     4: getty started by /etc/rc to handle terminal
     5: login started by getty on terminal to handle user session
     6: sh started by login on terminal for user use

     So the first process ID to use should be 7, with 6 as our parent, and the next PID should be 8.
     */
    const minix_pid_t pseudoparent = 6;
    const minix_pid_t ourselves = 7;

    // An entry for ourselves, first for fastest access by linear search.
    MINIXCompat_ProcessTable[0].minix_pid = ourselves;
    MINIXCompat_ProcessTable[0].host_pid = host_self_pid;

    // An entry for our parent, since it may actually be used by MINIX.
    MINIXCompat_ProcessTable[1].minix_pid = pseudoparent;
    MINIXCompat_ProcessTable[1].host_pid = host_self_ppid; // pretending that it's sh

    minix_next_pid = 8;
}

/*! Get the MINIX process corresponding to the given host-side process.. */
static minix_pid_t MINIXCompat_Processes_MINIXProcessForHostProcess(pid_t host_pid)
{
    for (size_t i = 0; i < MINIXCompat_ProcessTable_Size; i++) {
        if (MINIXCompat_ProcessTable[i].host_pid == host_pid) {
            return MINIXCompat_ProcessTable[i].minix_pid;
        }
    }

    return -1;
}

/*! Get the host process corresponding to the given MINIX-side process. */
static pid_t MINIXCompat_Processes_HostProcessForMINIXProcess(minix_pid_t minix_pid)
{
    for (size_t i = 0; i < MINIXCompat_ProcessTable_Size; i++) {
        if (MINIXCompat_ProcessTable[i].minix_pid == minix_pid) {
            return MINIXCompat_ProcessTable[i].host_pid;
        }
    }

    return -1;
}

/*! Get the next free entry number in the process table. */
static size_t MINIXCompat_Processes_NextFreeTableEntry(void)
{
    for (size_t i = 2; i < MINIXCompat_ProcessTable_Size; i++) {
        if (MINIXCompat_ProcessTable[i].host_pid == 0) {
            return i;
        }
    }

    // If we got here, there are no free entries. Reallocate the table with half again the size, making sure all the new entries are zeroed, and return the first of the new entries (which will correspond to the size of the old table).

    minix_process_mapping_t *old_table = MINIXCompat_ProcessTable;
    const size_t old_table_size = MINIXCompat_ProcessTable_Size;
    const size_t new_table_size = old_table_size + (old_table_size/2);
    const size_t first_new_entry = old_table_size;
    MINIXCompat_ProcessTable = calloc(new_table_size, sizeof(minix_process_mapping_t));
    memcpy(MINIXCompat_ProcessTable, old_table, MINIXCompat_ProcessTable_Size * sizeof(minix_process_mapping_t));
    MINIXCompat_ProcessTable_Size = new_table_size;
    return first_new_entry;
}

/*! Remove the given MINIX process from the process table. */
static void MINIXCompat_Processes_RemoveMINIXProcess(minix_pid_t minix_pid)
{
    assert(minix_pid > 0);

    for (size_t i = 0; i < MINIXCompat_ProcessTable_Size; i++) {
        if (MINIXCompat_ProcessTable[i].minix_pid == minix_pid) {
            MINIXCompat_ProcessTable[i].minix_pid = 0;
            MINIXCompat_ProcessTable[i].host_pid = 0;
            break;
        }
    }
}

void MINIXCompat_Processes_GetProcessIDs(minix_pid_t * _Nonnull minix_pid, minix_pid_t * _Nonnull minix_ppid)
{
    assert(minix_pid != NULL);
    assert(minix_ppid != NULL);

    if ((minix_self_pid == 0) && (minix_self_ppid == 0)) {
        minix_self_pid = MINIXCompat_ProcessTable[0].minix_pid;
        minix_self_ppid = MINIXCompat_ProcessTable[1].minix_pid;
    }

    *minix_pid = minix_self_pid;
    *minix_ppid = minix_self_ppid;

#if DEBUG_PROCESS_SYSCALLS
    MINIXCompat_Log("getpid() -> %d", minix_self_pid);
    MINIXCompat_Log("getppid() -> %d", minix_self_ppid);
#endif

    return;
}

minix_pid_t MINIXCompat_Processes_fork(void)
{
    minix_pid_t result;

    // Get a free entry in the process table prior to forking, so that both processes can have a similar table.
    size_t new_process_entry = MINIXCompat_Processes_NextFreeTableEntry();

    // Get the child PID to use and bump the next MINIX pid to use, so both parent and child have a coherent view of the world.
    minix_pid_t new_minix_process = minix_next_pid++;

    // Actually fork the host.
    pid_t new_host_process = fork();

    if (new_host_process == -1) {
        // An error occurred and no child was created; capture the error.

        result = -MINIXCompat_Errors_MINIXErrorForHostError(errno);

        // Reset minix_next_pid.

        minix_next_pid -= 1;
    } else if (new_host_process != 0) {
#if DEBUG_FORK
        volatile int continue_parent = 0;
        do {
            sleep(1);
        } while (!continue_parent);
#endif

        // This is the parent. Fill in the new entry in the process table. At this point the tables diverge.

        MINIXCompat_ProcessTable[new_process_entry].host_pid = new_host_process;
        MINIXCompat_ProcessTable[new_process_entry].minix_pid = new_minix_process;

        // Return the MINIX child PID.

        result = new_minix_process;
    } else {
#if DEBUG_FORK
        volatile int continue_child = 0;
        do {
            sleep(1);
        } while (!continue_child);
#endif
        sleep(5);

        // This is the child. Reinitialize logging (if it's a thing).

        MINIXCompat_Log_Initialize();

        // Put the old parent in the slot that the parent uses for this child, just in case. (That way there's no information lost in the fork(2) call.)

        MINIXCompat_ProcessTable[new_process_entry].host_pid = MINIXCompat_ProcessTable[1].host_pid;
        MINIXCompat_ProcessTable[new_process_entry].minix_pid = MINIXCompat_ProcessTable[1].minix_pid;

        // Adjust the handy globals for parent and self identities.

        minix_self_ppid = minix_self_pid;
        minix_self_pid = new_minix_process;

        // Now adjust the parent and self entries in the process table.

        MINIXCompat_ProcessTable[1].host_pid = MINIXCompat_ProcessTable[0].host_pid;
        MINIXCompat_ProcessTable[1].minix_pid = MINIXCompat_ProcessTable[0].minix_pid;

        MINIXCompat_ProcessTable[0].host_pid = getpid();
        MINIXCompat_ProcessTable[0].minix_pid = new_minix_process;

        // Return 0 here, because if the new process needs its own ID it can always use getpid(2) to get that.

        result = 0;
    }

#if DEBUG_PROCESS_SYSCALLS
    MINIXCompat_Log("fork() -> %d", result);
#endif

    return result;
}


/*! A MINIX wait status. */
typedef union minix_wait_stat {
    int16_t raw;
    struct {
        uint8_t exitstat;
        uint8_t sigstat;
    } cooked MINIXCOMPAT_PACK_STRUCT;
} minix_wait_stat_t;


static bool MINIXCompat_WIFEXITED(minix_wait_stat_t minix_wait_stat)
{
    return (minix_wait_stat.cooked.sigstat == 0);
}

static bool MINIXCompat_WIFSIGNALED(minix_wait_stat_t minix_wait_stat)
{
    return (minix_wait_stat.cooked.exitstat == 0);
}

static minix_signal_t MINIXCompat_WTERMSIG(minix_wait_stat_t minix_wait_stat)
{
    return (minix_signal_t) minix_wait_stat.cooked.sigstat;
}

static int16_t MINIXCompat_WEXITSTATUS(minix_wait_stat_t minix_wait_stat)
{
    return (int16_t) minix_wait_stat.cooked.exitstat;
}


/*! Figure out the MINIX wait status for the given host wait status. */
static minix_wait_stat_t MINIXCompat_Processes_MINIXStatForHostStat(int host_stat)
{
    minix_wait_stat_t minix_wait_stat;
    minix_wait_stat.raw = 0;

    // The MINIX status has three separate styles:
    //
    // LSB == 0 (exit):
    //   High byte is exit status
    // LSB == 0177 (job control):
    //   High byte is signal number
    // MSB == 0 (signal):
    //   Low byte is signal
    //
    // Portably construct this using the matching info in the host status.

    if (WIFEXITED(host_stat)) {
        minix_wait_stat.cooked.exitstat = WEXITSTATUS(host_stat);
    } else if (WIFSTOPPED(host_stat)) {
        minix_wait_stat.cooked.exitstat = WSTOPSIG(host_stat);
        minix_wait_stat.cooked.sigstat = 0177;
    } else if (WIFSIGNALED(host_stat)) {
        minix_wait_stat.cooked.exitstat = WTERMSIG(host_stat);
    } else {
        // Unsupported case on MINIX, just treat as killed by SIGKILL.
        minix_wait_stat.cooked.exitstat = (uint8_t) minix_SIGKILL;
    }

    return minix_wait_stat;
}

#if DEBUG_PROCESS_SYSCALLS
char *MINIXCompat_Processes_StringForWaitStat(minix_wait_stat_t minix_wait_stat)
{
    static char buf[64] = {0};

    // See above for MINIX status layout.

    if (MINIXCompat_WIFEXITED(minix_wait_stat)) {
        snprintf(buf, 64, "exited(%d)", MINIXCompat_WEXITSTATUS(minix_wait_stat));
    } else if (MINIXCompat_WIFSIGNALED(minix_wait_stat)) {
        snprintf(buf, 64, "signaled(%d)", MINIXCompat_WTERMSIG(minix_wait_stat));
    } else {
        snprintf(buf, 64, "other(0x%04x)", minix_wait_stat.raw);
    }

    return buf;
}
#endif

minix_pid_t MINIXCompat_Processes_wait(int16_t * _Nonnull minix_stat_loc)
{
    assert(minix_stat_loc != NULL);

    minix_pid_t minix_pid;

    int host_stat = 0;
    pid_t host_pid;

    // Ensure wait(2) doesn't fail with EINTR since most MINIX code won't handle that well.
    do host_pid = wait(&host_stat);
    while(host_pid == -1 && errno == EINTR);

    if (host_pid == -1) {
        minix_pid = -MINIXCompat_Errors_MINIXErrorForHostError(errno);
    } else {
        // Construct the return values.

        minix_pid = MINIXCompat_Processes_MINIXProcessForHostProcess(host_pid);
        minix_wait_stat_t minix_stat = MINIXCompat_Processes_MINIXStatForHostStat(host_stat);

        *minix_stat_loc = minix_stat.raw;

        // Maintain the process table: If the given process exited or was signaled,

        if (MINIXCompat_WIFEXITED(minix_stat)
            || MINIXCompat_WIFSIGNALED(minix_stat))
        {
            MINIXCompat_Processes_RemoveMINIXProcess(minix_pid);
        }
    }

#if DEBUG_PROCESS_SYSCALLS
    {
        minix_wait_stat_t minix_wait_stat;
        minix_wait_stat.raw = *minix_stat_loc;
        char *minix_stat_str = MINIXCompat_Processes_StringForWaitStat(minix_wait_stat);
        MINIXCompat_Log("wait(%p = %s) -> %d", minix_stat_loc, minix_stat_str, minix_pid);
    }
#endif

    return minix_pid;
}


int MINIXCompat_Processes_ExitStatus = 0;


void MINIXCompat_Processes_exit(int16_t status)
{
    MINIXCompat_Processes_ExitStatus = status;
    MINIXCompat_Execution_ChangeState(MINIXCompat_Execution_State_Finished);

#if DEBUG_PROCESS_SYSCALLS
    MINIXCompat_Log("exit(%d)", status);
#endif
}


// MARK: - Signal Handling

#if DEBUG_PROCESS_SYSCALLS
const char *MINIXCompat_Processes_NameForMINIXSignal(minix_signal_t minix_signal)
{
    switch (minix_signal) {
        case minix_SIGHUP: return "SIGHUP";
        case minix_SIGINT: return "SIGINT";
        case minix_SIGQUIT: return "SIGQUIT";
        case minix_SIGILL: return "SIGILL";
        case minix_SIGTRAP: return "SIGTRAP";
        case minix_SIGABRT: return "SIGABRT";
        case minix_SIGUNUSED: return "SIGUNUSED";
        case minix_SIGFPE: return "SIGFPE";
        case minix_SIGKILL: return "SIGKILL";
        case minix_SIGUSR1: return "SIGUSR1";
        case minix_SIGSEGV: return "SIGSEGV";
        case minix_SIGUSR2: return "SIGUSR2";
        case minix_SIGPIPE: return "SIGPIPE";
        case minix_SIGALRM: return "SIGALRM";
        case minix_SIGTERM: return "SIGTERM";
        case minix_SIGSTKFLT: return "SIGSTKFLT";
    }
}

const char *MINIXCompat_Processes_NameForMINIXSignalHandler(minix_sighandler_t minix_handler)
{
    if      (minix_handler == minix_SIG_DFL) return "SIG_DFL";
    else if (minix_handler == minix_SIG_IGN) return "SIG_IGN";
    else if (minix_handler == minix_SIG_ERR) return "SIG_ERR";
    else {
        static char namebuf[64] = {0};
        snprintf(namebuf, 64, "0x%08x", minix_handler);
        return namebuf;
    }
}
#endif

static int MINIXCompat_Processes_HostSignalForMINIXSignal(minix_signal_t minix_signal)
{
    switch (minix_signal) {
        case minix_SIGHUP: return SIGHUP;
        case minix_SIGINT: return SIGINT;
        case minix_SIGQUIT: return SIGQUIT;
        case minix_SIGILL: return SIGILL;
        case minix_SIGTRAP: return SIGTRAP;
        case minix_SIGABRT: return SIGABRT;
        case minix_SIGUNUSED: return SIGXFSZ; // Should never be used, but available just in case.
        case minix_SIGFPE: return SIGFPE;
        case minix_SIGKILL: return SIGKILL;
        case minix_SIGUSR1: return SIGUSR1;
        case minix_SIGSEGV: return SIGSEGV;
        case minix_SIGUSR2: return SIGUSR2;
        case minix_SIGPIPE: return SIGPIPE;
        case minix_SIGALRM: return SIGALRM;
        case minix_SIGTERM: return SIGTERM;
        case minix_SIGSTKFLT: return SIGXCPU; // Doesn't really exist for us so just use a signal we're unlikely to get.
    }
}

/*! Return the MINIX signal that correponds to the given host signal; if there isn't one, returns `0`. */
static minix_signal_t MINIXCompat_Processes_MINIXSignalForHostSignal(int host_signal)
{
    switch (host_signal) {
        case SIGHUP: return minix_SIGHUP;
        case SIGINT: return minix_SIGINT;
        case SIGQUIT: return minix_SIGQUIT;
        case SIGILL: return minix_SIGILL;
        case SIGTRAP: return minix_SIGTRAP;
        case SIGABRT: return minix_SIGABRT;
        case SIGXFSZ: return minix_SIGUNUSED; // Should never be used, but available just in case.
        case SIGFPE: return minix_SIGFPE;
        case SIGKILL: return minix_SIGKILL;
        case SIGUSR1: return minix_SIGUSR1;
        case SIGSEGV: return minix_SIGSEGV;
        case SIGUSR2: return minix_SIGUSR2;
        case SIGPIPE: return minix_SIGPIPE;
        case SIGALRM: return minix_SIGALRM;
        case SIGTERM: return minix_SIGTERM;
        case SIGXCPU: return minix_SIGSTKFLT; // Doesn't really exist for us so just use a signal we're unlikely to get.
        default: return 0; // indicate that MINIX doesn't support this signal
    }
}

static bool MINIXCompat_Processes_HasPendingSignal = false;
static bool MINIXCompat_Processes_PendingSignals[17] = { false };

/*! Indicate that a signal was received and needs to be processed. */
static void MINIXCompat_Processes_RegisterPendingSignal(int host_signal)
{
    minix_signal_t minix_signal = MINIXCompat_Processes_MINIXSignalForHostSignal(host_signal);
    if (minix_signal != 0) {
        MINIXCompat_Processes_HasPendingSignal = true;
        MINIXCompat_Processes_PendingSignals[minix_signal] = true;
    }
}

static void MINIXCompat_Processes_SignalHandler_DFL(int host_signal)
{
    MINIXCompat_Processes_RegisterPendingSignal(host_signal);
}

static void MINIXCompat_Processes_SignalHandler_Other(int host_signal)
{
    MINIXCompat_Processes_RegisterPendingSignal(host_signal);
}

static void MINIXCompat_Processes_HandlePendingSignal(minix_signal_t minix_signal)
{
    minix_sighandler_t handler = minix_signal_handlers[minix_signal];

    if (handler == minix_SIG_IGN) {
        return;
    } else if (handler == minix_SIG_DFL) {
        // Handle default behavior for the signal.
#if DEBUG_SIGNAL
        MINIXCompat_Log("default signal handler for %d called", minix_signal);
#endif
        // TODO: Default handler behavior for minix_signal.
        return;
    } else if (handler == minix_SIG_ERR) {
        // Representation of an error, should never be called but just in case it is...
#if DEBUG_SIGNAL
        MINIXCompat_Log("error signal handler for %d", minix_signal);
#endif
        // Do nothing.
        return;
    } else {
        // A real 68K handler was specified, set it up to be called.

        /*
         Here's our theory of operation, based on a suggestion by Warren Toomey.

         When we have a 68K signal handler to execute, we have to meet the expectations of the `_begsig` library function which always wraps the actual signal handler:

         1. Push the current PC.
         2. Push the current SR.
         3. Push the signal number.
         4. Set the current PC to the signal handler address (which will be `_begsig`).

         Then when we next run the emulator, it will run the signal handler, which expects the first thing on the stack to be the signal number. When that's done, it adjusts the stack and does an `RTR` will restore the SR and PC to what it was before running the handler, thus resuming execution where it left off.

         This doesn't support code that does a `longjmp(3)` out of a signal handler but it's not clear whether MINIX 1.5 supported such code either.
         */

        m68k_address_t pc = MINIXCompat_CPU_GetPC();
        MINIXCompat_CPU_Push_32(pc);
        uint16_t sr = MINIXCompat_CPU_GetSR();
        MINIXCompat_CPU_Push_16(sr);
        MINIXCompat_CPU_Push_16(minix_signal);
        MINIXCompat_CPU_SetPC(handler);
    }
}

void MINIXCompat_Processes_HandlePendingSignals(void)
{
    if (MINIXCompat_Processes_HasPendingSignal) {
        MINIXCompat_Processes_HasPendingSignal = false;
        for (minix_signal_t minix_signal = minix_SIGHUP;
             minix_signal <= minix_SIGSTKFLT;
             minix_signal++)
        {
            if (MINIXCompat_Processes_PendingSignals[minix_signal]) {
                MINIXCompat_Processes_PendingSignals[minix_signal] = false;
                MINIXCompat_Processes_HandlePendingSignal(minix_signal);
            }
        }
    }
}

static void *MINIXCompat_Processes_HostSignalHandlerForMINIXSignalHandler(minix_sighandler_t minix_handler)
{
    if (minix_handler == minix_SIG_DFL) {
        return MINIXCompat_Processes_SignalHandler_DFL;
    } else if (minix_handler == minix_SIG_IGN) {
        return SIG_IGN;
    } else if (minix_handler == minix_SIG_ERR) {
        return SIG_ERR;
    } else {
        return MINIXCompat_Processes_SignalHandler_Other;
    }
}

minix_sighandler_t MINIXCompat_Processes_signal(minix_signal_t minix_signal, minix_sighandler_t minix_handler)
{
    assert((minix_signal >= minix_SIGHUP) && (minix_signal <= minix_SIGSTKFLT));

    // Update the MINIX signal table.

    minix_sighandler_t old_minix_handler = minix_signal_handlers[minix_signal];
    minix_signal_handlers[minix_signal] = minix_handler;

    // Register a host-side handler for the given signal.

    int host_signal = MINIXCompat_Processes_HostSignalForMINIXSignal(minix_signal);
    void *host_handler = MINIXCompat_Processes_HostSignalHandlerForMINIXSignalHandler(minix_handler);

    void *old_host_handler = signal(host_signal, host_handler);

    if (old_host_handler == SIG_DFL) {
        old_minix_handler = minix_SIG_DFL;
    } else if (old_host_handler == SIG_IGN) {
        old_minix_handler = minix_SIG_IGN;
    } else if (old_host_handler == SIG_ERR) {
        old_minix_handler = minix_SIG_ERR;
    } else {
        // Just preserve the old MINIX handler as retrieved from the table.
    }

#if DEBUG_PROCESS_SYSCALLS
    {
        const char *signal_name = MINIXCompat_Processes_NameForMINIXSignal(minix_signal);
        const char *new_handler_name = MINIXCompat_Processes_NameForMINIXSignalHandler(minix_handler);
        const char *old_handler_name = MINIXCompat_Processes_NameForMINIXSignalHandler(old_minix_handler);

        MINIXCompat_Log("signal(%s (%d), %s) -> %s", signal_name, minix_signal, new_handler_name, old_handler_name);
    }
#endif

    return old_minix_handler;
}

int16_t MINIXCompat_Processes_kill(minix_pid_t minix_pid, minix_signal_t minix_signal)
{
    int16_t result;

    assert(minix_pid > 0);
    assert((minix_signal >= minix_SIGHUP) && (minix_signal <= minix_SIGSTKFLT));

    int host_signal = MINIXCompat_Processes_HostSignalForMINIXSignal(minix_signal);
    if (host_signal <= 0) {
        result = -minix_EINVAL;
        goto done;
    }

    pid_t host_pid = (minix_pid > 0) ? MINIXCompat_Processes_HostProcessForMINIXProcess(minix_pid) : minix_pid;
    if (host_pid <= 0) {
        result = -minix_ESRCH;
        goto done;
    }

    int kill_result = kill(host_pid, host_signal);
    if (kill_result == -1) {
        result = -MINIXCompat_Errors_MINIXErrorForHostError(errno);
    } else {
        result = kill_result;
    }

done:
#if DEBUG_PROCESS_SYSCALLS
    {
        const char *minix_signal_name = MINIXCompat_Processes_NameForMINIXSignal(minix_signal);
        MINIXCompat_Log("kill(%d, %s (%d)) -> %d", minix_pid, minix_signal_name, minix_signal, -minix_EINVAL);
    }
#endif

    return result;
}


// MARK: - Exec

static void MINIXCompat_Arguments_Initialize(uint32_t host_argc, char **host_argv, uint32_t host_envc, char **host_envp)
{
    /*! Round up a value to the next multiple of 4. 0 = 0 but 1..3 = 4, 5..7 = 8, etc. */
#define round_up_32(x) ((x) + (4 - ((x) % 4)))

    /*
     Note there is no envc but we of course know the count of envp entries too on our way in.

     The region at and above the stack pointer is as follows:
         argc
         argv[0] (tool)
         argv[1]..argv[argc-1]
         NULL
         envp[0]..envp[envc-1]
         NULL

     This leads to the following:
     1. &argc is sp
     2. &argv[n] is (sp+4)+(n*4)
     3. &argv[argc] is (sp+4)+(argc*4) and contains NULL
     4. &envp[n] is &argv[argc+n]+4
     5. &envp[envc] contains NULL

     All the actual string content comes after the argc/argv/envp, with each entry 4-byte aligned.
     */

    uint32_t argc_argv_envp_count = 1 + (host_argc + 1) + (host_envc + 1);
    uint32_t argc_ragv_envp_size = argc_argv_envp_count * sizeof(uint32_t);
    uint32_t *argc_argv_envp = calloc(argc_argv_envp_count, sizeof(uint32_t));
    uint32_t content_size = 0;
    char *content = NULL;

    // Figure out content_size first and set up a buffer for it.

    {
        for (char **iter_argv = host_argv; *iter_argv != NULL; iter_argv++) {
            char *argv_n = *iter_argv;
            uint32_t iter_argv_len = (uint32_t)strlen(argv_n) + 1;
            content_size += round_up_32(iter_argv_len);
        }

        for (char **iter_envp = host_envp; *iter_envp != NULL; iter_envp++) {
            char *envp_n = *iter_envp;
            if (strncmp("MINIX_", envp_n, 6) == 0) {
                uint32_t iter_envp_len = (uint32_t)strlen(envp_n) + 1 - 6;
                content_size += round_up_32(iter_envp_len);
            }
        }

        content = calloc(content_size, sizeof(char));
    }

    // Copy the content into place and put the emulator-side pointers to it in place as well.

    {
        int argc_argv_envp_idx = 0;
        uint32_t content_offset = 0;

        // Start with argc.

        argc_argv_envp[argc_argv_envp_idx++] = htonl(host_argc);

        // Copy and put in place pointers to argv and envp.

        for (char **iter_argv = host_argv; *iter_argv != NULL; iter_argv++) {
            char *argv_n = *iter_argv;
            size_t argv_n_len = (uint32_t)strlen(argv_n) + 1;
            strncpy(&content[content_offset], argv_n, content_size - content_offset);
            m68k_address_t content_addr = MINIXCompat_Stack_Base + argc_ragv_envp_size + content_offset;
            argc_argv_envp[argc_argv_envp_idx++] = htonl(content_addr);
            content_offset += round_up_32(argv_n_len);
        }

        argc_argv_envp[argc_argv_envp_idx++] = htonl(0);

        for (char **iter_envp = host_envp; *iter_envp != NULL; iter_envp++) {
            char *envp_n = *iter_envp;
            if (strncmp("MINIX_", envp_n, 6) == 0) {
                size_t envp_n_len = (uint32_t)strlen(envp_n) + 1 - 6;
                strncpy(&content[content_offset], &envp_n[6], content_size - content_offset);
                m68k_address_t content_addr = MINIXCompat_Stack_Base + argc_ragv_envp_size + content_offset;
                argc_argv_envp[argc_argv_envp_idx++] = htonl(content_addr);
                content_offset += round_up_32(envp_n_len);
            }
        }

        argc_argv_envp[argc_argv_envp_idx++] = htonl(0);
    }

    // Copy buffers from the host to the emulated environment, making them contiguous.

    MINIXCompat_RAM_Copy_Block_From_Host(MINIXCompat_Stack_Base, argc_argv_envp, argc_ragv_envp_size);
    MINIXCompat_RAM_Copy_Block_From_Host(MINIXCompat_Stack_Base + argc_ragv_envp_size, content, content_size);

    // Free the host-side buffers now.

    free(argc_argv_envp);
    free(content);
#undef round_up_32
}

static int16_t MINIXCompat_Processes_LoadTool(const char *executable_path)
{
    int16_t result;

    // TODO: Support interpreter scripts
    /*
     We could do this by parsing the first line for a #! prefix and using the interpreter there as the tool to run, with the given arguments.

     I.e. if /tmp/foo.sh is an executable script that starts with `#!/bin/sh`, that means we'd load /bin/sh, pass /tmp/foo.sh as argv[1], and pass the other arguments as argv[2] and so on.
     */

    // Get the path to the tool to run and ensure it actually exists.

    FILE *toolfile = NULL;
    char *executable_host_path = MINIXCompat_Filesystem_CopyHostPathForPath(executable_path);
    struct stat executable_host_stat;
    int stat_err = stat(executable_host_path, &executable_host_stat);
    if (stat_err == -1) {
        result = -MINIXCompat_Errors_MINIXErrorForHostError(errno);
        goto done;
    }

    // Load the tool into host memory, relocate it, and load the relocated tool into emulator memory.

    toolfile = fopen(executable_host_path, "r");
    if (toolfile == NULL) {
        result = -MINIXCompat_Errors_MINIXErrorForHostError(EIO);
        goto done;
    }

    struct MINIXCompat_Executable *executable = NULL;
    uint8_t *executable_text_and_data = NULL;
    uint32_t executable_text_and_data_len = 0;

    int load_err = MINIXCompat_Executable_Load(toolfile, &executable, &executable_text_and_data, &executable_text_and_data_len);
    if (load_err != 0) {
        result = load_err;
        goto done;
    }

    MINIXCompat_RAM_Copy_Block_From_Host(MINIXCompat_Executable_Base, executable_text_and_data, executable_text_and_data_len);
    result = 0;

done:
    if (toolfile) {
        fclose(toolfile);
    }

    free(executable_host_path);

    return result;
}

int16_t MINIXCompat_Processes_ExecuteWithStackBlock(const char *executable_path, void *stack_on_host, int16_t stack_size)
{
    // Clear out 68K memory.

    MINIXCompat_RAM_Clear();

    // Load and relocate the executable.

    int16_t load_err = MINIXCompat_Processes_LoadTool(executable_path);
    if (load_err != 0) {
        return load_err;
    }

    // Relocate the initial stack, which was copied out of 68K memory before clear.

    uint32_t *stack = stack_on_host;
    stack++; // skip argc

    while (*stack != 0) {
        uint32_t relocated_argv = ntohl(*stack) + MINIXCompat_Stack_Base;
        *stack = htonl(relocated_argv);
        stack++;
    }

    stack++; // skip argv[argc]

    while (*stack != 0) {
        uint32_t relocated_envp = ntohl(*stack) + MINIXCompat_Stack_Base;
        *stack = htonl(relocated_envp);
        stack++;
    }

    // Load the relocated stack into emulator RAM.

    MINIXCompat_RAM_Copy_Block_From_Host(MINIXCompat_Stack_Base, stack_on_host, stack_size);

    // Ready to go! The ready state reinitializes the emulator; any existing emulator-side state is blown away.

    MINIXCompat_Execution_ChangeState(MINIXCompat_Execution_State_Ready);

#if DEBUG_PROCESS_SYSCALLS
    MINIXCompat_Log("exec(\"%s\")", executable_path);
#endif

    return 0;
}

int16_t MINIXCompat_Processes_ExecuteWithHostParams(const char *executable_path, int16_t argc, char * _Nullable * _Nonnull argv, char * _Nullable * _Nonnull envp)
{
    // Load and relocate the executable.

    int16_t load_err = MINIXCompat_Processes_LoadTool(executable_path);
    if (load_err != 0) {
        return load_err;
    }

    // Set up the MINIX host-side argc, argv, and envp, and put them in their well-known locations in the "prix fixe" stack.
    // Note that when copying environment variables, only those prefixed with `MINIX_` are copied, allowing fine-grained control.

    uint32_t host_argc = argc - 1;
    char **host_argv = &argv[1];
    uint32_t host_envc = 0;
    char **host_envp = envp;

    for (char **iter_envp = envp; *iter_envp; iter_envp++) {
        if (strncmp("MINIX_", *iter_envp, 6) == 0) {
            host_envc += 1;
        }
    }

    // Place the arguments into MINIX memory.

    MINIXCompat_Arguments_Initialize(host_argc, host_argv, host_envc, host_envp);

    // Ready to go! The ready state reinitializes the emulator; any existing emulator-side state is blown away.

    MINIXCompat_Execution_ChangeState(MINIXCompat_Execution_State_Ready);

#if DEBUG_PROCESS_SYSCALLS
    MINIXCompat_Log("exec_host(\"%s\")", executable_path);
#endif

    return 0;
}


// MARK: - "Break" Handling

int16_t MINIXCompat_Processes_brk(m68k_address_t minix_requested_addr, m68k_address_t *minix_resulting_addr)
{
    int16_t result;

    assert(minix_resulting_addr != NULL);

    // There is only one process and it has full run of the address space up to 0x00FE0000, so just allow any value up to that. Also keep track of the current break so it can be properly returned when requested.

    static m68k_address_t minix_current_break = 0;

    if (minix_current_break == 0) {
        minix_current_break = MINIXCompat_Executable_Get_Initial_Break();
    }

    if ((minix_requested_addr < MINIXCompat_Executable_Limit)
        && (minix_requested_addr >= MINIXCompat_Executable_Get_Initial_Break()))
    {
        result = 0;
        *minix_resulting_addr = minix_requested_addr;
        minix_current_break = minix_requested_addr;
    } else {
        result = -minix_ENOMEM;
        *minix_resulting_addr = 0xFFFFFFFF; // MINIX-side ((char *)-1) value
    }

#if DEBUG_PROCESS_SYSCALLS
    MINIXCompat_Log("brk(0x%08x, %p = 0x%08x) -> %d", minix_requested_addr, minix_resulting_addr, minix_requested_addr, result);
#endif

    return result;
}


MINIXCOMPAT_SOURCE_END
