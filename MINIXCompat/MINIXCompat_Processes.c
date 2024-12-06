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

#include "MINIXCompat_Types.h"
#include "MINIXCompat.h"
#include "MINIXCompat_Emulation.h"
#include "MINIXCompat_Errors.h"
#include "MINIXCompat_Executable.h"
#include "MINIXCompat_Filesystem.h"


#if DEBUG

/*
 Uncomment to debug forking.

 This will spin both the parent and child in loops immediately following the fork, allowing debugger attachment and resumption.
 */
//#define DEBUG_FORK 1

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
static minix_sighandler_t minix_signal_handlers[16] = { 0x00000000 /* SIG_DFL */ };


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

/*! Get the next free entry number in the process table. **/
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

        // This is the child put the old parent in the slot that the parent uses for this child, just in case. (That way there's no information lost in the fork(2) call.)

        MINIXCompat_ProcessTable[new_process_entry].host_pid = MINIXCompat_ProcessTable[1].host_pid;
        MINIXCompat_ProcessTable[new_process_entry].minix_pid = MINIXCompat_ProcessTable[1].minix_pid;

        // Now adjust the parent and self entries in the process table.

        MINIXCompat_ProcessTable[1].host_pid = MINIXCompat_ProcessTable[0].host_pid;
        MINIXCompat_ProcessTable[1].minix_pid = MINIXCompat_ProcessTable[0].minix_pid;

        MINIXCompat_ProcessTable[0].host_pid = getpid();
        MINIXCompat_ProcessTable[0].minix_pid = new_minix_process;

        // Return 0 here, because if the new process needs its own ID it can always use getpid(2) to get that.

        result = 0;
    }

    return result;
}

static int16_t MINIXCompat_Processes_MINIXStatForHostStat(int host_stat)
{
    int16_t minix_stat = 0;

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
        minix_stat = WEXITSTATUS(host_stat);
    } else if (WIFSTOPPED(host_stat)) {
        minix_stat = (WSTOPSIG(host_stat) << 8) | 0177;
    } else if (WIFSIGNALED(host_stat)) {
        minix_stat = WTERMSIG(host_stat) << 8;
    } else {
        // Unsupported case on MINIX, just treat as killed by SIGKILL:
        // MSB == 0, LSB == 0x09.
        minix_stat = 0x0009;
    }

    return minix_stat;
}

minix_pid_t MINIXCompat_Processes_wait(int16_t * _Nonnull minix_stat_loc)
{
    assert(minix_stat_loc != NULL);

    minix_pid_t minix_pid;

    int host_stat = 0;
    pid_t host_pid = wait(&host_stat);
    if (host_pid == -1) {
        minix_pid = -MINIXCompat_Errors_MINIXErrorForHostError(errno);
    } else {
        minix_pid = MINIXCompat_Processes_MINIXProcessForHostProcess(host_pid);
        int16_t minix_stat = MINIXCompat_Processes_MINIXStatForHostStat(host_stat);

        *minix_stat_loc = minix_stat;
    }

    return minix_pid;
}

static int MINIXCompat_Processes_HostSignalForMINIXSignal(minix_signal_t minix_signal)
{
    switch (minix_signal) {
        case minix_SIGHUP: return SIGHUP;
        case minix_SIGINT: return SIGINT;
        case minix_SIGQUIT: return SIGQUIT;
        case minix_SIGILL: return SIGILL;
        case minix_SIGTRAP: return SIGTRAP;
        case minix_SIGABRT: return SIGABRT;
        case minix_SIGUNUSED: return SIGEMT; // Should never be used, but available just in case.
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

static int minix_current_signal = 0;

static void MINIXCompat_Processes_SignalHandler_DFL(int sig)
{
    // TODO: Implement handling for MINIX signals.
    // This should just set a flag that a signal needs to be handled MINIX-side, and then handle that signal when the emulation loop returns.
    minix_current_signal = sig;
}

static void MINIXCompat_Processes_SignalHandler_Other(int sig)
{
    // TODO: Implement handling for MINIX signals.
    // This should just set a flag that a signal needs to be handled MINIX-side, and then handle that signal when the emulation loop returns.
    minix_current_signal = sig;
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
    assert((minix_signal > 0) && (minix_signal <= 16));

    // Update the MINIX signal table.

    minix_sighandler_t old_minix_handler = minix_signal_handlers[minix_signal - 1];
    minix_signal_handlers[minix_signal - 1] = minix_handler;

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

    return old_minix_handler;
}

int16_t MINIXCompat_Processes_kill(minix_pid_t minix_pid, minix_signal_t minix_signal)
{
    int16_t result;

    assert(minix_pid > 0);
    assert((minix_signal > 0) && (minix_signal <= 16));

    int host_signal = MINIXCompat_Processes_HostSignalForMINIXSignal(minix_signal);
    if (host_signal <= 0) {
        return -minix_EINVAL;
    }

    pid_t host_pid = (minix_pid > 0) ? MINIXCompat_Processes_HostProcessForMINIXProcess(minix_pid) : minix_pid;
    if (host_pid <= 0) {
        return -minix_ESRCH;
    }

    int kill_result = kill(host_pid, host_signal);
    if (kill_result == -1) {
        result = -MINIXCompat_Errors_MINIXErrorForHostError(errno);
    } else {
        result = kill_result;
    }

    return result;
}

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
            strncpy(&content[content_offset], argv_n, argv_n_len);
            m68k_address_t content_addr = MINIXCompat_Stack_Base + argc_ragv_envp_size + content_offset;
            argc_argv_envp[argc_argv_envp_idx++] = htonl(content_addr);
            content_offset += round_up_32(argv_n_len);
        }

        argc_argv_envp[argc_argv_envp_idx++] = htonl(0);

        for (char **iter_envp = host_envp; *iter_envp != NULL; iter_envp++) {
            char *envp_n = *iter_envp;
            if (strncmp("MINIX_", envp_n, 6) == 0) {
                size_t envp_n_len = (uint32_t)strlen(envp_n) + 1 - 6;
                strncpy(&content[content_offset], &envp_n[6], envp_n_len);
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
    // TODO: Support interpreter scripts
    /*
     We could do this by parsing the first line for a #! prefix and using the interpreter there as the tool to run, with the given arguments.

     I.e. if /tmp/foo.sh is an executable script that starts with `#!/bin/sh`, that means we'd load /bin/sh, pass /tmp/foo.sh as argv[1], and pass the other arguments as argv[2] and so on.
     */

    // Get the path to the tool to run and ensure it actually exists.

    char *executable_host_path = MINIXCompat_Filesystem_CopyHostPathForPath(executable_path);
    struct stat executable_host_stat;
    int stat_err = stat(executable_host_path, &executable_host_stat);
    if (stat_err == -1) {
        return -MINIXCompat_Errors_MINIXErrorForHostError(errno);
    }

    // Load the tool into host memory, relocate it, and load the relocated tool into emulator memory.

    FILE *toolfile = fopen(executable_host_path, "r");
    if (toolfile == NULL) {
        return -MINIXCompat_Errors_MINIXErrorForHostError(EIO);
    }

    struct MINIXCompat_Executable *executable = NULL;
    uint8_t *executable_text_and_data = NULL;
    uint32_t executable_text_and_data_len = 0;

    int load_err = MINIXCompat_Executable_Load(toolfile, &executable, &executable_text_and_data, &executable_text_and_data_len);
    if (load_err != 0) {
        return load_err;
    }

    MINIXCompat_RAM_Copy_Block_From_Host(MINIXCompat_Executable_Base, executable_text_and_data, executable_text_and_data_len);

    fclose(toolfile);

    free(executable_host_path);

    return 0;
}

int16_t MINIXCompat_Processes_ExecuteWithStackBlock(const char *executable_path, void *stack_on_host, int16_t stack_size)
{
    // Load and relocate the executable.

    int16_t load_err = MINIXCompat_Processes_LoadTool(executable_path);
    if (load_err != 0) {
        return load_err;
    }

    // Relocate the stack.

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

    // Ready to go!

    MINIXCompat_Execution_ChangeState(MINIXCompat_Execution_State_Ready);

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

    // Ready to go!

    MINIXCompat_Execution_ChangeState(MINIXCompat_Execution_State_Ready);

    return 0;
}


MINIXCOMPAT_SOURCE_END
