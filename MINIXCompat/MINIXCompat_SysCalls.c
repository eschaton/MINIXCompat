//
//  MINIXCompat_SysCalls.c
//  MINIXCompat
//
//  Created by Chris Hanson on 11/30/24.
//  Copyright © 2024 Christopher M. Hanson. See file LICENSE for details.
//

#include "MINIXCompat_SysCalls.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include <arpa/inet.h> /* for ntohs et al */

#include "MINIXCompat_Types.h"
#include "MINIXCompat_Emulation.h"
#include "MINIXCompat_Errors.h"
#include "MINIXCompat_Executable.h"
#include "MINIXCompat_Filesystem.h"
#include "MINIXCompat_Messages.h"
#include "MINIXCompat_Processes.h"


//#define DEBUG_SYSCALL_MECHANISM 1


MINIXCOMPAT_SOURCE_BEGIN


// MARK: - System Call Prototypes

minix_syscall_result_t MINIXCompat_SysCall_exit(minix_syscall_func_t func, uint16_t src_dest, m68k_address_t msg, minix_message_t *message, uint32_t * _Nonnull out_result);
minix_syscall_result_t MINIXCompat_SysCall_fork(minix_syscall_func_t func, uint16_t src_dest, m68k_address_t msg, minix_message_t *message, uint32_t * _Nonnull out_result);
minix_syscall_result_t MINIXCompat_SysCall_read(minix_syscall_func_t func, uint16_t src_dest, m68k_address_t msg, minix_message_t *message, uint32_t * _Nonnull out_result);
minix_syscall_result_t MINIXCompat_SysCall_write(minix_syscall_func_t func, uint16_t src_dest, m68k_address_t msg, minix_message_t *message, uint32_t * _Nonnull out_result);
minix_syscall_result_t MINIXCompat_SysCall_open(minix_syscall_func_t func, uint16_t src_dest, m68k_address_t msg, minix_message_t *message, uint32_t * _Nonnull out_result);
minix_syscall_result_t MINIXCompat_SysCall_close(minix_syscall_func_t func, uint16_t src_dest, m68k_address_t msg, minix_message_t *message, uint32_t * _Nonnull out_result);
minix_syscall_result_t MINIXCompat_SysCall_wait(minix_syscall_func_t func, uint16_t src_dest, m68k_address_t msg, minix_message_t *message, uint32_t * _Nonnull out_result);
minix_syscall_result_t MINIXCompat_SysCall_creat(minix_syscall_func_t func, uint16_t src_dest, m68k_address_t msg, minix_message_t *message, uint32_t * _Nonnull out_result);
// minix_syscall_result_t MINIXCompat_SysCall_link(minix_syscall_func_t func, uint16_t src_dest, m68k_address_t msg, minix_message_t *message, uint32_t * _Nonnull out_result);
minix_syscall_result_t MINIXCompat_SysCall_unlink(minix_syscall_func_t func, uint16_t src_dest, m68k_address_t msg, minix_message_t *message, uint32_t * _Nonnull out_result);
// minix_syscall_result_t MINIXCompat_SysCall_exec(minix_syscall_func_t func, uint16_t src_dest, m68k_address_t msg, minix_message_t *message, uint32_t * _Nonnull out_result);
minix_syscall_result_t MINIXCompat_SysCall_chdir(minix_syscall_func_t func, uint16_t src_dest, m68k_address_t msg, minix_message_t *message, uint32_t * _Nonnull out_result);
minix_syscall_result_t MINIXCompat_SysCall_time(minix_syscall_func_t func, uint16_t src_dest, m68k_address_t msg, minix_message_t *message, uint32_t * _Nonnull out_result);
// minix_syscall_result_t MINIXCompat_SysCall_mknod(minix_syscall_func_t func, uint16_t src_dest, m68k_address_t msg, minix_message_t *message, uint32_t * _Nonnull out_result);
// minix_syscall_result_t MINIXCompat_SysCall_chmod(minix_syscall_func_t func, uint16_t src_dest, m68k_address_t msg, minix_message_t *message, uint32_t * _Nonnull out_result);
// minix_syscall_result_t MINIXCompat_SysCall_chown(minix_syscall_func_t func, uint16_t src_dest, m68k_address_t msg, minix_message_t *message, uint32_t * _Nonnull out_result);
minix_syscall_result_t MINIXCompat_SysCall_brk(minix_syscall_func_t func, uint16_t src_dest, m68k_address_t msg, minix_message_t *message, uint32_t * _Nonnull out_result);
minix_syscall_result_t MINIXCompat_SysCall_stat(minix_syscall_func_t func, uint16_t src_dest, m68k_address_t msg, minix_message_t *message, uint32_t * _Nonnull out_result);
minix_syscall_result_t MINIXCompat_SysCall_lseek(minix_syscall_func_t func, uint16_t src_dest, m68k_address_t msg, minix_message_t *message, uint32_t * _Nonnull out_result);
minix_syscall_result_t MINIXCompat_SysCall_getpid(minix_syscall_func_t func, uint16_t src_dest, m68k_address_t msg, minix_message_t *message, uint32_t * _Nonnull out_result);
// minix_syscall_result_t MINIXCompat_SysCall_mount(minix_syscall_func_t func, uint16_t src_dest, m68k_address_t msg, minix_message_t *message, uint32_t * _Nonnull out_result);
// minix_syscall_result_t MINIXCompat_SysCall_umount(minix_syscall_func_t func, uint16_t src_dest, m68k_address_t msg, minix_message_t *message, uint32_t * _Nonnull out_result);
// minix_syscall_result_t MINIXCompat_SysCall_setuid(minix_syscall_func_t func, uint16_t src_dest, m68k_address_t msg, minix_message_t *message, uint32_t * _Nonnull out_result);
minix_syscall_result_t MINIXCompat_SysCall_getuid(minix_syscall_func_t func, uint16_t src_dest, m68k_address_t msg, minix_message_t *message, uint32_t * _Nonnull out_result);
// minix_syscall_result_t MINIXCompat_SysCall_stime(minix_syscall_func_t func, uint16_t src_dest, m68k_address_t msg, minix_message_t *message, uint32_t * _Nonnull out_result);
// minix_syscall_result_t MINIXCompat_SysCall_ptrace(minix_syscall_func_t func, uint16_t src_dest, m68k_address_t msg, minix_message_t *message, uint32_t * _Nonnull out_result);
// minix_syscall_result_t MINIXCompat_SysCall_alarm(minix_syscall_func_t func, uint16_t src_dest, m68k_address_t msg, minix_message_t *message, uint32_t * _Nonnull out_result);
minix_syscall_result_t MINIXCompat_SysCall_fstat(minix_syscall_func_t func, uint16_t src_dest, m68k_address_t msg, minix_message_t *message, uint32_t * _Nonnull out_result);
// minix_syscall_result_t MINIXCompat_SysCall_pause(minix_syscall_func_t func, uint16_t src_dest, m68k_address_t msg, minix_message_t *message, uint32_t * _Nonnull out_result);
// minix_syscall_result_t MINIXCompat_SysCall_utime(minix_syscall_func_t func, uint16_t src_dest, m68k_address_t msg, minix_message_t *message, uint32_t * _Nonnull out_result);
// minix_syscall_result_t MINIXCompat_SysCall_stty(minix_syscall_func_t func, uint16_t src_dest, m68k_address_t msg, minix_message_t *message, uint32_t * _Nonnull out_result);
// minix_syscall_result_t MINIXCompat_SysCall_gtty(minix_syscall_func_t func, uint16_t src_dest, m68k_address_t msg, minix_message_t *message, uint32_t * _Nonnull out_result);
minix_syscall_result_t MINIXCompat_SysCall_access(minix_syscall_func_t func, uint16_t src_dest, m68k_address_t msg, minix_message_t *message, uint32_t * _Nonnull out_result);
// minix_syscall_result_t MINIXCompat_SysCall_nice(minix_syscall_func_t func, uint16_t src_dest, m68k_address_t msg, minix_message_t *message, uint32_t * _Nonnull out_result);
// minix_syscall_result_t MINIXCompat_SysCall_ftime(minix_syscall_func_t func, uint16_t src_dest, m68k_address_t msg, minix_message_t *message, uint32_t * _Nonnull out_result);
// minix_syscall_result_t MINIXCompat_SysCall_sync(minix_syscall_func_t func, uint16_t src_dest, m68k_address_t msg, minix_message_t *message, uint32_t * _Nonnull out_result);
minix_syscall_result_t MINIXCompat_SysCall_kill(minix_syscall_func_t func, uint16_t src_dest, m68k_address_t msg, minix_message_t *message, uint32_t * _Nonnull out_result);
// minix_syscall_result_t MINIXCompat_SysCall_rename(minix_syscall_func_t func, uint16_t src_dest, m68k_address_t msg, minix_message_t *message, uint32_t * _Nonnull out_result);
// minix_syscall_result_t MINIXCompat_SysCall_mkdir(minix_syscall_func_t func, uint16_t src_dest, m68k_address_t msg, minix_message_t *message, uint32_t * _Nonnull out_result);
// minix_syscall_result_t MINIXCompat_SysCall_rmdir(minix_syscall_func_t func, uint16_t src_dest, m68k_address_t msg, minix_message_t *message, uint32_t * _Nonnull out_result);
// minix_syscall_result_t MINIXCompat_SysCall_dup(minix_syscall_func_t func, uint16_t src_dest, m68k_address_t msg, minix_message_t *message, uint32_t * _Nonnull out_result);
// minix_syscall_result_t MINIXCompat_SysCall_pipe(minix_syscall_func_t func, uint16_t src_dest, m68k_address_t msg, minix_message_t *message, uint32_t * _Nonnull out_result);
// minix_syscall_result_t MINIXCompat_SysCall_times(minix_syscall_func_t func, uint16_t src_dest, m68k_address_t msg, minix_message_t *message, uint32_t * _Nonnull out_result);
// minix_syscall_result_t MINIXCompat_SysCall_prof(minix_syscall_func_t func, uint16_t src_dest, m68k_address_t msg, minix_message_t *message, uint32_t * _Nonnull out_result);
// unused45
// minix_syscall_result_t MINIXCompat_SysCall_setgid(minix_syscall_func_t func, uint16_t src_dest, m68k_address_t msg, minix_message_t *message, uint32_t * _Nonnull out_result);
minix_syscall_result_t MINIXCompat_SysCall_getgid(minix_syscall_func_t func, uint16_t src_dest, m68k_address_t msg, minix_message_t *message, uint32_t * _Nonnull out_result);
minix_syscall_result_t MINIXCompat_SysCall_signal(minix_syscall_func_t func, uint16_t src_dest, m68k_address_t msg, minix_message_t *message, uint32_t * _Nonnull out_result);
// unused49
// unused50
// minix_syscall_result_t MINIXCompat_SysCall_acct(minix_syscall_func_t func, uint16_t src_dest, m68k_address_t msg, minix_message_t *message, uint32_t * _Nonnull out_result);
// minix_syscall_result_t MINIXCompat_SysCall_phys(minix_syscall_func_t func, uint16_t src_dest, m68k_address_t msg, minix_message_t *message, uint32_t * _Nonnull out_result);
// minix_syscall_result_t MINIXCompat_SysCall_lock(minix_syscall_func_t func, uint16_t src_dest, m68k_address_t msg, minix_message_t *message, uint32_t * _Nonnull out_result);
// minix_syscall_result_t MINIXCompat_SysCall_ioctl(minix_syscall_func_t func, uint16_t src_dest, m68k_address_t msg, minix_message_t *message, uint32_t * _Nonnull out_result);
// minix_syscall_result_t MINIXCompat_SysCall_fcntl(minix_syscall_func_t func, uint16_t src_dest, m68k_address_t msg, minix_message_t *message, uint32_t * _Nonnull out_result);
// minix_syscall_result_t MINIXCompat_SysCall_mpx(minix_syscall_func_t func, uint16_t src_dest, m68k_address_t msg, minix_message_t *message, uint32_t * _Nonnull out_result);
// unused57
// unused58
minix_syscall_result_t MINIXCompat_SysCall_exece(minix_syscall_func_t func, uint16_t src_dest, m68k_address_t msg, minix_message_t *message, uint32_t * _Nonnull out_result);
// minix_syscall_result_t MINIXCompat_SysCall_umask(minix_syscall_func_t func, uint16_t src_dest, m68k_address_t msg, minix_message_t *message, uint32_t * _Nonnull out_result);
// minix_syscall_result_t MINIXCompat_SysCall_chroot(minix_syscall_func_t func, uint16_t src_dest, m68k_address_t msg, minix_message_t *message, uint32_t * _Nonnull out_result);
// unused62
// unused63
// minix_syscall_result_t MINIXCompat_SysCall_KSIG(minix_syscall_func_t func, uint16_t src_dest, m68k_address_t msg, minix_message_t *message, uint32_t * _Nonnull out_result);
// minix_syscall_result_t MINIXCompat_SysCall_UNPAUSE(minix_syscall_func_t func, uint16_t src_dest, m68k_address_t msg, minix_message_t *message, uint32_t * _Nonnull out_result);
// minix_syscall_result_t MINIXCompat_SysCall_BRK2(minix_syscall_func_t func, uint16_t src_dest, m68k_address_t msg, minix_message_t *message, uint32_t * _Nonnull out_result);
// minix_syscall_result_t MINIXCompat_SysCall_REVIVE(minix_syscall_func_t func, uint16_t src_dest, m68k_address_t msg, minix_message_t *message, uint32_t * _Nonnull out_result);
// minix_syscall_result_t MINIXCompat_SysCall_TASK_REPLY(minix_syscall_func_t func, uint16_t src_dest, m68k_address_t msg, minix_message_t *message, uint32_t * _Nonnull out_result);
// unused69


// MARK: - System Call Names
#if DEBUG_SYSCALL_MECHANISM
/*! A table of syscall names so we can more easily trace what's going on.*/
static const char * _Nonnull const minix_syscall_name[70] = {
    "unused0",
    "exit",
    "fork",
    "read",
    "write",
    "open",
    "close",
    "wait",
    "creat",
    "link",
    "unlink",
    "exec",
    "chdir",
    "time",
    "mknod",
    "chmod",
    "chown",
    "brk",
    "stat",
    "lseek",
    "getpid",
    "mount",
    "umount",
    "setuid",
    "getuid",
    "stime",
    "ptrace",
    "alarm",
    "fstat",
    "pause",
    "utime",
    "stty",
    "gtty",
    "access",
    "nice",
    "ftime",
    "sync",
    "kill",
    "rename",
    "mkdir",
    "rmdir",
    "dup",
    "pipe",
    "times",
    "prof",
    "unused45",
    "setgid",
    "getgid",
    "signal",
    "unused49",
    "unused50",
    "acct",
    "phys",
    "lock",
    "ioctl",
    "fcntl",
    "mpx",
    "unused57",
    "unused58",
    "exece",
    "umask",
    "chroot",
    "unused62",
    "unused63",
    "KSIG",
    "UNPAUSE",
    "BRK2",
    "REVIVE",
    "TASK_REPLY",
    "unused69",
};
#endif


// MARK: - System Call Table

typedef minix_syscall_result_t (*minix_syscall_impl)(minix_syscall_func_t func, uint16_t src_dest, m68k_address_t msg, minix_message_t *message, uint32_t * _Nonnull out_result);

/*!
 MINIX System Call Table, for MM & FS calls.
 */
minix_syscall_impl _Nullable minix_syscall_table[70] = {
    NULL, // unused0
    MINIXCompat_SysCall_exit,
    MINIXCompat_SysCall_fork,
    MINIXCompat_SysCall_read,
    MINIXCompat_SysCall_write,
    MINIXCompat_SysCall_open,
    MINIXCompat_SysCall_close,
    MINIXCompat_SysCall_wait,
    MINIXCompat_SysCall_creat,
    NULL, // MINIXCompat_SysCall_link
    MINIXCompat_SysCall_unlink,
    NULL, // MINIXCompat_SysCall_exec
    MINIXCompat_SysCall_chdir,
    MINIXCompat_SysCall_time,
    NULL, // MINIXCompat_SysCall_mknod
    NULL, // MINIXCompat_SysCall_chmod
    NULL, // MINIXCompat_SysCall_chown
    MINIXCompat_SysCall_brk,
    MINIXCompat_SysCall_stat,
    MINIXCompat_SysCall_lseek,
    MINIXCompat_SysCall_getpid,
    NULL, // MINIXCompat_SysCall_mount
    NULL, // MINIXCompat_SysCall_umount
    NULL, // MINIXCompat_SysCall_setuid
    MINIXCompat_SysCall_getuid,
    NULL, // MINIXCompat_SysCall_stime
    NULL, // MINIXCompat_SysCall_ptrace
    NULL, // MINIXCompat_SysCall_alarm
    MINIXCompat_SysCall_fstat,
    NULL, // MINIXCompat_SysCall_pause
    NULL, // MINIXCompat_SysCall_utime
    NULL, // MINIXCompat_SysCall_stty
    NULL, // MINIXCompat_SysCall_gtty
    MINIXCompat_SysCall_access,
    NULL, // MINIXCompat_SysCall_nice
    NULL, // MINIXCompat_SysCall_ftime
    NULL, // MINIXCompat_SysCall_sync
    MINIXCompat_SysCall_kill,
    NULL, // MINIXCompat_SysCall_rename
    NULL, // MINIXCompat_SysCall_mkdir
    NULL, // MINIXCompat_SysCall_rmdir
    NULL, // MINIXCompat_SysCall_dup
    NULL, // MINIXCompat_SysCall_pipe
    NULL, // MINIXCompat_SysCall_times
    NULL, // MINIXCompat_SysCall_prof
    NULL, // unused45
    NULL, // MINIXCompat_SysCall_setgid
    MINIXCompat_SysCall_getgid,
    MINIXCompat_SysCall_signal,
    NULL, // unused49
    NULL, // unused50
    NULL, // MINIXCompat_SysCall_acct
    NULL, // MINIXCompat_SysCall_phys
    NULL, // MINIXCompat_SysCall_lock
    NULL, // MINIXCompat_SysCall_ioctl
    NULL, // MINIXCompat_SysCall_fcntl
    NULL, // MINIXCompat_SysCall_mpx
    NULL, // unused57
    NULL, // unused58
    MINIXCompat_SysCall_exece,
    NULL, // MINIXCompat_SysCall_umask
    NULL, // MINIXCompat_SysCall_chroot
    NULL, // unused62
    NULL, // unused63
    NULL, // MINIXCompat_SysCall_KSIG
    NULL, // MINIXCompat_SysCall_UNPAUSE
    NULL, // MINIXCompat_SysCall_BRK2
    NULL, // MINIXCompat_SysCall_REVIVE
    NULL, // MINIXCompat_SysCall_TASK_REPLY
    NULL, // unused69
};


// MARK: - System Call Infrastructure

/*! All the world is `ast:adm` (uid 8, gid 3), whose HOME is `/usr/ast`. Thanks, Dr. Tannenbaum! */
const uint16_t minix_default_uid = 8;

/*! All the world is `ast:adm` (uid 8, gid 3), whose HOME is `/usr/ast`. Thanks, Dr. Tannenbaum! */
const uint16_t minix_default_gid = 3;

/*! All the world is effectively `root:root` (uid 0, gid 0). */
const uint16_t minix_default_euid = 0;

/*! All the world is effectively `root:root` (uid 0, gid 0). */
const uint16_t minix_default_egid = 0;


void MINIXCompat_SysCall_Initialize(void)
{
    // Nothing yet.
}


minix_syscall_result_t MINIXCompat_System_Call(minix_syscall_func_t func, uint16_t src_dest, m68k_address_t msg, uint32_t * _Nonnull out_result)
{
    assert((func >= minix_syscall_func_send) && (func <= minix_syscall_func_both));

#if DEBUG_SYSCALL_MECHANISM && (DEBUG_SYSCALL_MECHANISM > 1)
    // Log the message.
    const char * const func_str[3] = { "send", "receive", "sendrec" };
    fprintf(stderr, "%s(src_dest: 0x%04hx, msg: *0x%08x)\n", func_str[func - 1], src_dest, msg);
#endif

    minix_syscall_result_t result = minix_syscall_result_failure;

    // Get the message from emulator to host.

    minix_message_t *message = MINIXCompat_RAM_Copy_Block_To_Host(msg, sizeof(minix_message_t));

    if ((func == minix_syscall_func_send) || (func == minix_syscall_func_both)) {
        // Figure out the system call to which the message corresponds, and call the appropriate function.
        // NOTE: *System calls* are only ever sent to MM or FS (0 or 1), but
        //       there are other well-known processes that also need
        //       emulation. These are covered by the ``minix_task_t`` enum.
        //       (Currently we don't support MINIX processes messaging each
        //       other because each process is fully isolated from the
        //       other.)

        minix_task_t task = (minix_task_t)src_dest;
        switch (task) {

            // Almost all "system calls" are sent to the MM or FS.
            case minix_task_fs:
            case minix_task_mm: {
                minix_syscall_t sc = ntohs(message->m_type);
                assert((sc >= minix_syscall_unused0) && (sc <= minix_syscall_unused69));
#if DEBUG_SYSCALL_MECHANISM
                const char *scn = minix_syscall_name[sc];
                fprintf(stderr, "syscall(%d): %s (%hd)\n", getpid(), scn, sc);
#endif
                minix_syscall_impl scimpl = minix_syscall_table[sc];
                if (scimpl == NULL) {
#if DEBUG_SYSCALL_MECHANISM
                    fprintf(stderr, "unimplemented syscall: %s (%hd)\n", scn, sc);
#endif
                    result = minix_syscall_result_failure;
                } else {
                    result = scimpl(func, src_dest, msg, message, out_result);
                }
            } break;

            // Other tasks may need to be emulated to be compatible.
            case minix_task_tty:
            case minix_task_printer:
            case minix_task_storage_fixed:
            case minix_task_storage_removable:
            case minix_task_memory:
            case minix_task_clock:
            case minix_task_system:
            case minix_task_hardware:
            case minix_task_init:
            default: {
                // TODO: Implement other tasks?
                fprintf(stderr, "task %hu is currently not yet emulated\n", task);
                result = minix_syscall_result_failure;
            } break;
        }

        // If the sender is expecting a response beyond the value of `d0.l`, it should have adjusted the message it was given to contain that response and ensured it's appropriately swapped.

        if (func == minix_syscall_func_both) {
            MINIXCompat_RAM_Copy_Block_From_Host(msg, message, sizeof(minix_message_t));
        }
    } else if (func == minix_syscall_func_receive) {
        // Blocking and waiting for a message via receive() isn't actually done by any user processes in the default system, so we aren't supporting it (yet).
#if DEBUG_SYSCALL_MECHANISM
        fprintf(stderr, "receive-only messages are not yet supported\n");
#endif
        result = minix_syscall_result_failure;
    } else {
        // We'll never actually get here without memory corruption.
        assert(false);
    }

    free(message);

    return result;
}


// MARK: - System Call Implementations

/*
 The system call implementations is that they should be thin wrappers that call the relevant subsystem; in other words, they shouldn't do much more than argument/result transcoding, while the appropriate subsystem is called to do the real work.
 */

/*! MINIX `_exit(2)` implementation. */
minix_syscall_result_t MINIXCompat_SysCall_exit(minix_syscall_func_t func, uint16_t src_dest, m68k_address_t msg, minix_message_t *message, uint32_t * _Nonnull out_result)
{
    // exit uses mess1
    MINIXCompat_Message_Swap(mess1, message);

    int16_t value = message->m1_i1;

    MINIXCompat_exit(value);

    return minix_syscall_result_success_empty;
}

/*! MINIX `fork(2)` implementation. */
minix_syscall_result_t MINIXCompat_SysCall_fork(minix_syscall_func_t func, uint16_t src_dest, m68k_address_t msg, minix_message_t *message, uint32_t * _Nonnull out_result)
{
    // fork(2) sends mess1 with no parameters.

    minix_pid_t minix_pid = MINIXCompat_Processes_fork();

    // fork(2) receives mess2 in two cases
    //
    // parent:
    // - m_type: child pid or -errno on error
    //
    // child (only if successfully created):
    // - m_type: 0

    MINIXCompat_Message_Clear(message);
    message->m_type = minix_pid;

    MINIXCompat_Message_Swap(mess2, message);

    return minix_syscall_result_success_empty;
}

/*! MINIX `read(2)` implementation. */
minix_syscall_result_t MINIXCompat_SysCall_read(minix_syscall_func_t func, uint16_t src_dest, m68k_address_t msg, minix_message_t *message, uint32_t * _Nonnull out_result)
{
    // read(2) sends mess1
    // - m1_i1: fd
    // - m1_i2: nbytes
    // - m1_p1: buf

    MINIXCompat_Message_Swap(mess1, message);

    minix_fd_t minix_fd = message->m1_i1;
    int16_t minix_nbytes = message->m1_i2;
    m68k_address_t minix_buf = message->m1_p1;

    uint8_t *buf = calloc(minix_nbytes, sizeof(uint8_t));

    int16_t result = MINIXCompat_File_Read(minix_fd, buf, minix_nbytes);

    if (result > 0) {
        MINIXCompat_RAM_Copy_Block_From_Host(minix_buf, buf, result);
    }

    // raed(2) replies with mess1
    // - m_type: result

    MINIXCompat_Message_Clear(message);
    message->m_type = result;

    MINIXCompat_Message_Swap(mess1, message);

    return minix_syscall_result_success_empty;
}

/*! MINIX `write(2)` implementation. */
minix_syscall_result_t MINIXCompat_SysCall_write(minix_syscall_func_t func, uint16_t src_dest, m68k_address_t msg, minix_message_t *message, uint32_t * _Nonnull out_result)
{
    // write(2) sends mess1
    // - m1_i1: fd
    // - m1_i2: nbytes
    // - m1_p1: buf

    MINIXCompat_Message_Swap(mess1, message);

    minix_fd_t minix_fd = message->m1_i1;
    int16_t minix_nbytes = message->m1_i2;
    m68k_address_t minix_buf = message->m1_p1;

    uint8_t *buf = MINIXCompat_RAM_Copy_Block_To_Host(minix_buf, minix_nbytes);

    int16_t result = MINIXCompat_File_Write(minix_fd, buf, minix_nbytes);

    // write(2) replies with mess1
    // - m_type: result

    MINIXCompat_Message_Clear(message);
    message->m_type = result;

    MINIXCompat_Message_Swap(mess1, message);

    return minix_syscall_result_success_empty;
}

/*! MINIX `open(2)` implementation. */
minix_syscall_result_t MINIXCompat_SysCall_open(minix_syscall_func_t func, uint16_t src_dest, m68k_address_t msg, minix_message_t *message, uint32_t * _Nonnull out_result)
{
    // open(2) sends mess1 (when O_CREAT is set) or mess3 (when O_CREAT is not set)
    //
    // When O_CREAT is set, open sends mess1:
    // - m1_i1: len(name)
    // - m1_i2: flags
    // - m1_i3: mode
    // - m1_p1: name
    //
    // When O_CREAT is not set, open sends mess3:
    // - m3_i1: len(name)
    // - m3_i2: flags
    // - m3_p1: name
    // - m3_ca1: name (copied into place)
    //
    // Thus check flags for O_CREAT being set first, since that indicates which message structure is used.

    int16_t minix_flags = htons(message->m1_i2);
    int16_t minix_mode;
    m68k_address_t minix_name;
    int16_t minix_name_len;

    if (minix_flags & minix_O_CREAT) {
        MINIXCompat_Message_Swap(mess1, message);
        minix_name_len = message->m1_i1;
        minix_name = message->m1_p1;
        minix_mode = message->m1_i3;
    } else {
        MINIXCompat_Message_Swap(mess3, message);
        minix_name_len = message->m3_i1;
        minix_name = message->m3_p1;
        minix_mode = 0; // only needed on creation
    }

    // Copy the name (path) to host memory.
    // The name will include any trailing '\0' character. (Use of the pointer from the message rather than the message's inline copy is intentional, since the latter can only handle up to 14 characters.)

    char *minix_name_on_host = MINIXCompat_RAM_Copy_Block_To_Host(minix_name, minix_name_len);

    // Open the file at that path, relative to MINIXCOMPAT_DIR.

    minix_fd_t fd = MINIXCompat_File_Open(minix_name_on_host, minix_flags, minix_mode);

    // Return the result via mess1.m_type containing the error or the emulator-side fd.

    MINIXCompat_Message_Clear(message);
    message->m_type = fd;

    MINIXCompat_Message_Swap(mess1, message);

    // Clean up.

    free(minix_name_on_host);

    return minix_syscall_result_success_empty;
}

/*! MINIX `close(2)` implementation. */
minix_syscall_result_t MINIXCompat_SysCall_close(minix_syscall_func_t func, uint16_t src_dest, m68k_address_t msg, minix_message_t *message, uint32_t * _Nonnull out_result)
{
    // close(2) sends mess1
    // - m.m1_i1: fd
    MINIXCompat_Message_Swap(mess1, message);

    minix_fd_t minix_fd = message->m1_i1;

    int16_t result = MINIXCompat_File_Close(minix_fd);

    // Respond with mess1.
    // - m1.m_type: result

    MINIXCompat_Message_Clear(message);
    message->m_type = result;

    MINIXCompat_Message_Swap(mess1, message);

    return minix_syscall_result_success_empty;
}

/*! MINIX `wait(2)` implementation. */
minix_syscall_result_t MINIXCompat_SysCall_wait(minix_syscall_func_t func, uint16_t src_dest, m68k_address_t msg, minix_message_t *message, uint32_t * _Nonnull out_result)
{
    // wait(2) sends mess1 with no parameters

    int16_t minix_stat = 0;
    minix_pid_t minix_pid = MINIXCompat_Processes_wait(&minix_stat);

    // wait(2) receives mess2:
    // m_type = pid
    // m2_i1 = stat

    MINIXCompat_Message_Clear(message);
    message->m_type = minix_pid;
    message->m2_i1 = minix_stat;

    MINIXCompat_Message_Swap(mess2, message);

    return minix_syscall_result_success_empty;
}

/*! MINIX `creat(2)` implementation. */
minix_syscall_result_t MINIXCompat_SysCall_creat(minix_syscall_func_t func, uint16_t src_dest, m68k_address_t msg, minix_message_t *message, uint32_t * _Nonnull out_result)
{
    // creat(2) sends mess3
    // - m3_i1: len(name)
    // - m3_i2: mode
    // - m3_p1: name
    // - m3_ca1: name (copied into place)

    MINIXCompat_Message_Swap(mess3, message);
    int16_t minix_name_len = message->m3_i1;
    m68k_address_t minix_name = message->m3_p1;
    int16_t minix_mode = message->m3_i2;

    // Copy the name (path) to host memory.
    // The name will include any trailing '\0' character. (Use of the pointer from the message rather than the message's inline copy is intentional, since the latter can only handle up to 14 characters.)

    char *minix_name_on_host = MINIXCompat_RAM_Copy_Block_To_Host(minix_name, minix_name_len);

    // Create the file at that path, relative to MINIXCOMPAT_DIR.

    minix_fd_t fd = MINIXCompat_File_Create(minix_name_on_host, minix_mode);

    // Return the result via mess1.m_type containing the error or the emulator-side fd.

    MINIXCompat_Message_Clear(message);
    message->m_type = fd;

    MINIXCompat_Message_Swap(mess1, message);

    // Clean up.

    free(minix_name_on_host);

    return minix_syscall_result_success_empty;
}

/*! MINIX `unlink(2)` implementation. */
minix_syscall_result_t MINIXCompat_SysCall_unlink(minix_syscall_func_t func, uint16_t src_dest, m68k_address_t msg, minix_message_t *message, uint32_t * _Nonnull out_result)
{
    // unlink(2) sends mess3
    // - m3_i1: len(name)
    // - m3_p1: name
    // - m3_ca1: name (copied into place)

    MINIXCompat_Message_Swap(mess3, message);
    int16_t minix_name_len = message->m3_i1;
    m68k_address_t minix_name = message->m3_p1;

    // Copy the name (path) to host memory.
    // The name will include any trailing '\0' character. (Use of the pointer from the message rather than the message's inline copy is intentional, since the latter can only handle up to 14 characters.)

    char *minix_name_on_host = MINIXCompat_RAM_Copy_Block_To_Host(minix_name, minix_name_len);

    // Unlink the file at that path, relative to MINIXCOMPAT_DIR.

    int16_t unlink_err = MINIXCompat_File_Unlink(minix_name_on_host);

    // unlink(2) responds with mess1
    // - mess1.m_type: result

    MINIXCompat_Message_Clear(message);
    message->m_type = unlink_err;

    MINIXCompat_Message_Swap(mess1, message);

    *out_result = unlink_err;

    // Clean up.

    free(minix_name_on_host);

    return minix_syscall_result_success;
}

/*! MINIX `time(2)` implementation. */
minix_syscall_result_t MINIXCompat_SysCall_time(minix_syscall_func_t func, uint16_t src_dest, m68k_address_t msg, minix_message_t *message, uint32_t * _Nonnull out_result)
{
    // time(2) sends mess1
    // - no parameters

    time_t t = time(NULL);
    int time_result = (t < 0) ? -MINIXCompat_Errors_MINIXErrorForHostError(errno) : 0;

    // time(2) receives mess2
    // - m_type: success or error
    // - m2_l1: time

    MINIXCompat_Message_Clear(message);
    message->m_type = time_result;
    message->m2_l1 = (uint32_t) t;

    MINIXCompat_Message_Swap(mess2, message);

    *out_result = (uint32_t) t;

    return minix_syscall_result_success;
}

/*!
 MINIX `BRK` implementation.

 There is only one process and it has full run of the address space, up to `0x00FE0000`, so just allow any value up to that.

 - Note: The MINIX `BRK` system call does *not* return `NULL` on success, but instead returns the actual break value. However, the MINIX `brk(2)` *library function* **does** return `NULL` on success--it just also squirrels away the current break value returned by the `BRK` system call for use by `sbrk(2)`. So the behavior of this function has to match the `BRK` *system call*, not the `brk(2)` *library function*.
 */
minix_syscall_result_t MINIXCompat_SysCall_brk(minix_syscall_func_t func, uint16_t src_dest, m68k_address_t msg, minix_message_t *message, uint32_t * _Nonnull out_result)
{
    // brk(2) sends mess1
    // - m.m1_p1: addr

    MINIXCompat_Message_Swap(mess1, message);

    m68k_address_t minix_requested_addr = message->m1_p1;
    m68k_address_t minix_resulting_addr;
    int16_t minix_brk_error = 0;

    // There is only one process and it has full run of the address space up to 0x00FE0000, so just allow any value up to that. Also keep track of the current break since we never allow it to be set lower.

    static m68k_address_t minix_current_break = 0;

    if ((minix_requested_addr < MINIXCompat_Executable_Limit)
        && (minix_requested_addr >= minix_current_break))
    {
        minix_resulting_addr = minix_requested_addr;
        minix_current_break = minix_resulting_addr;
    } else {
        minix_brk_error = -minix_ENOMEM;
        minix_resulting_addr = 0xFFFFFFFF; // MINIX-side ((char *)-1) value
    }

    // brk(2) replies mess2
    // - m.m2_p1: new break address

    MINIXCompat_Message_Clear(message);
    message->m_type = minix_brk_error;
    message->m2_p1 = minix_resulting_addr;

    MINIXCompat_Message_Swap(mess2, message);

    return minix_syscall_result_success_empty;
}

/*! MINIX `stat(2)` implementation. */
minix_syscall_result_t MINIXCompat_SysCall_stat(minix_syscall_func_t func, uint16_t src_dest, m68k_address_t msg, minix_message_t *message, uint32_t * _Nonnull out_result)
{
    // stat(2) sends mess1
    // - m1_i1: len(name)
    // - m1_p1: name
    // - m1_p2: buffer
    // - m1_ca1: name

    MINIXCompat_Message_Swap(mess1, message);

    int16_t minix_name_len = message->m1_i1;
    m68k_address_t minix_name = message->m1_p1;
    m68k_address_t minix_stat_buf = message->m1_p2;

    // Copy the name (path) to host memory.
    // The name will include any trailing '\0' character. (Use of the pointer from the message rather than the message's inline copy is intentional, since the latter can only handle up to 14 characters.)

    char *minix_name_on_host = MINIXCompat_RAM_Copy_Block_To_Host(minix_name, minix_name_len);

    // Do the stat(2).

    minix_stat_t host_stat_buf;
    int16_t stat_err = MINIXCompat_File_Stat(minix_name_on_host, &host_stat_buf);

    // Send the host-side MINIX stat buf to MINIX. (Even if there's an error, this is OK to do, the caller reserved space.)

    MINIXCompat_RAM_Copy_Block_From_Host(minix_stat_buf, &host_stat_buf, sizeof(minix_stat_t));

    // stat(2) replies mess1
    // - m_type: result

    MINIXCompat_Message_Clear(message);
    message->m_type = stat_err;

    MINIXCompat_Message_Swap(mess1, message);

    // Clean up.

    free(minix_name_on_host);

    return minix_syscall_result_success_empty;
}

/*! MINIX `lseek(2)` implementation. */
minix_syscall_result_t MINIXCompat_SysCall_lseek(minix_syscall_func_t func, uint16_t src_dest, m68k_address_t msg, minix_message_t *message, uint32_t * _Nonnull out_result)
{
    // lseek(2) sends mess2
    // - m2_i1: fd
    // - m2_i2: whence
    // - m2_l1: offset

    MINIXCompat_Message_Swap(mess2, message);

    minix_fd_t minix_fd = message->m2_i1;
    int16_t minix_whence = message->m2_i2;
    minix_off_t minix_offset = message->m2_l1;

    int16_t seek_err = MINIXCompat_File_Seek(minix_fd, minix_offset, minix_whence);

    // lseek(2) receives mess2
    // - m_type: result
    // - m2_l1: offset

    MINIXCompat_Message_Clear(message);

    message->m_type = (seek_err == 0) ? minix_offset : seek_err;
    message->m2_l1 = minix_offset;

    MINIXCompat_Message_Swap(mess2, message);

    *out_result = (seek_err == 0) ? minix_offset : ((int32_t)seek_err);
    return minix_syscall_result_success;
}

/*! MINIX `getpid(2)` implementation. */
minix_syscall_result_t MINIXCompat_SysCall_getpid(minix_syscall_func_t func, uint16_t src_dest, m68k_address_t msg, minix_message_t *message, uint32_t * _Nonnull out_result)
{
    // getpid(2) sends mess1 with no parameters

    // getpid(2) receives mess1
    // - m_type: result
    // - m1_i1: ppid

    MINIXCompat_Message_Clear(message);

    minix_pid_t minix_pid = 0, minix_ppid = 0;

    MINIXCompat_Processes_GetProcessIDs(&minix_pid, &minix_ppid);

    message->m_type = minix_pid;
    message->m1_i1 = minix_ppid;

    MINIXCompat_Message_Swap(mess1, message);

    return minix_syscall_result_success_empty;
}

/*! MINIX `getuid(2)` implementation. */
minix_syscall_result_t MINIXCompat_SysCall_getuid(minix_syscall_func_t func, uint16_t src_dest, m68k_address_t msg, minix_message_t *message, uint32_t * _Nonnull out_result)
{
    // getuid sends mess1
    MINIXCompat_Message_Swap(mess1, message);

    // getuid receives mess2 in response
    MINIXCompat_Message_Clear(message);
    message->m_type = minix_default_uid;
    message->m_u.m_m2.m2i1 = minix_default_euid;

    MINIXCompat_Message_Swap(mess2, message);

    return minix_syscall_result_success_empty;
}

/*! MINIX `fstat(2)` implementation. */
minix_syscall_result_t MINIXCompat_SysCall_fstat(minix_syscall_func_t func, uint16_t src_dest, m68k_address_t msg, minix_message_t *message, uint32_t * _Nonnull out_result)
{
    // fstat(2) sends mess1
    // - m1_i1: fd
    // - m1_p1: buffer

    MINIXCompat_Message_Swap(mess1, message);

    minix_fd_t minix_fd = message->m1_i1;
    m68k_address_t minix_stat_buf = message->m1_p1;

    // Do the fstat(2).

    minix_stat_t host_stat_buf;
    int16_t stat_err = MINIXCompat_File_StatOpen(minix_fd, &host_stat_buf);

    // Send the host-side MINIX stat buf to MINIX. (Even if there's an error, this is OK to do, the caller reserved space.)

    MINIXCompat_RAM_Copy_Block_From_Host(minix_stat_buf, &host_stat_buf, sizeof(minix_stat_t));

    // stat(2) replies mess1
    // - m_type: result

    MINIXCompat_Message_Clear(message);
    message->m_type = stat_err;

    MINIXCompat_Message_Swap(mess1, message);

    return minix_syscall_result_success_empty;
}

/*! MINIX `access(2)` implementation. */
minix_syscall_result_t MINIXCompat_SysCall_access(minix_syscall_func_t func, uint16_t src_dest, m68k_address_t msg, minix_message_t *message, uint32_t * _Nonnull out_result)
{
    // access(2) sends mess3
    // - m3_i1: len(name)
    // - m3_i2: mode
    // - m3_p1: name
    // - m3_ca1: name (copied into place)

    MINIXCompat_Message_Swap(mess3, message);
    int16_t minix_name_len = message->m3_i1;
    m68k_address_t minix_name = message->m3_p1;
    minix_mode_t minix_mode = message->m3_i2;

    // Copy the name (path) to host memory.
    // The name will include any trailing '\0' character. (Use of the pointer from the message rather than the message's inline copy is intentional, since the latter can only handle up to 14 characters.)

    char *minix_name_on_host = MINIXCompat_RAM_Copy_Block_To_Host(minix_name, minix_name_len);

    // Check access on the file at that path, relative to MINIXCOMPAT_DIR.

    int16_t access_err = MINIXCompat_File_Access(minix_name_on_host, minix_mode);

    // access(2) responds with mess1
    // - mess1.m_type: result

    MINIXCompat_Message_Clear(message);
    message->m_type = access_err;

    MINIXCompat_Message_Swap(mess1, message);

    // Clean up.

    free(minix_name_on_host);

    return minix_syscall_result_success_empty;
}

/*! MINIX `chdir(2)` implementation. */
minix_syscall_result_t MINIXCompat_SysCall_chdir(minix_syscall_func_t func, uint16_t src_dest, m68k_address_t msg, minix_message_t *message, uint32_t * _Nonnull out_result)
{
    // chdir(2) sends mess3
    // - m3_i1: len(name)
    // - m3_p1: name

    MINIXCompat_Message_Swap(mess3, message);
    int16_t minix_name_len = message->m3_i1;
    m68k_address_t minix_name = message->m3_p1;

    // Copy the name (path) to host memory.
    // The name will include any trailing '\0' character. (Use of the pointer from the message rather than the message's inline copy is intentional, since the latter can only handle up to 14 characters.)

    char *minix_name_on_host = MINIXCompat_RAM_Copy_Block_To_Host(minix_name, minix_name_len);

    // Change to that directory, relative to MINIXCOMPAT_DIR.

    int16_t chdir_err = MINIXCompat_File_Chdir(minix_name_on_host);

    // chdir(2) responds with mess1
    // - mess1.m_type: result

    MINIXCompat_Message_Clear(message);
    message->m_type = chdir_err;

    MINIXCompat_Message_Swap(mess1, message);

    // Clean up.

    free(minix_name_on_host);

    return minix_syscall_result_success_empty;
}

/*! MINIX `kill(2)` implementation. */
minix_syscall_result_t MINIXCompat_SysCall_kill(minix_syscall_func_t func, uint16_t src_dest, m68k_address_t msg, minix_message_t *message, uint32_t * _Nonnull out_result)
{
    // kill(2) sends mess1
    // - m1_i1: pid
    // - m1_i2: signal

    MINIXCompat_Message_Swap(mess1, message);
    minix_pid_t minix_pid = message->m1_i1;
    minix_signal_t minix_signal = message->m1_i2;

    int16_t kill_result = MINIXCompat_Processes_kill(minix_pid, minix_signal);

    // kill(2) receives mess2
    // - m_type: result

    MINIXCompat_Message_Clear(message);
    message->m_type = kill_result;

    MINIXCompat_Message_Swap(mess2, message);

    return minix_syscall_result_success_empty;
}

/*! MINIX `getgid(2)` implementation. */
minix_syscall_result_t MINIXCompat_SysCall_getgid(minix_syscall_func_t func, uint16_t src_dest, m68k_address_t msg, minix_message_t *message, uint32_t * _Nonnull out_result)
{
    // getuid(2) sends mess1
    MINIXCompat_Message_Swap(mess1, message);

    // getuid(2) receives mess2 in response
    MINIXCompat_Message_Clear(message);
    message->m_type = minix_default_gid;
    message->m_u.m_m2.m2i1 = minix_default_egid;

    MINIXCompat_Message_Swap(mess2, message);

    return minix_syscall_result_success_empty;
}

/*! MINIX `signal(2)` implementation. */
minix_syscall_result_t MINIXCompat_SysCall_signal(minix_syscall_func_t func, uint16_t src_dest, m68k_address_t msg, minix_message_t *message, uint32_t * _Nonnull out_result)
{
    // signal(2) sends mess6
    MINIXCompat_Message_Swap(mess6, message);

    minix_signal_t minix_signal = message->m6_i1;
    minix_sighandler_t new_handler = message->m6_f1;

    minix_sighandler_t old_handler = MINIXCompat_Processes_signal(minix_signal, new_handler);

    // signal(2) receives mess2
    // - m_type: result (OK)

    MINIXCompat_Message_Clear(message);
    message->m_type = 0;

    MINIXCompat_Message_Swap(mess2, message);

    // signal(2) receives the old handler via d0.l

    *out_result = old_handler;

    return minix_syscall_result_success;
}

/*! MINIX `exece(2)` implementation. */
minix_syscall_result_t MINIXCompat_SysCall_exece(minix_syscall_func_t func, uint16_t src_dest, m68k_address_t msg, minix_message_t *message, uint32_t * _Nonnull out_result)
{
    // exece(2) sends mess1
    // - m1_i1: len(path)
    // - m2_i2: stack size
    // - m1_p1: path
    // - m1_p2: stack pointer
    MINIXCompat_Message_Swap(mess1, message);

    int16_t minix_path_len = message->m1_i1;
    m68k_address_t minix_path = message->m1_p1;

    int16_t minix_stack_size = message->m1_i2;
    m68k_address_t minix_stack = message->m1_p2;

    // Copy the path to host memory.
    // The path will include any trailing '\0' character.

    char *minix_path_on_host = MINIXCompat_RAM_Copy_Block_To_Host(minix_path, minix_path_len);

    /*
     Copy the stack snapshot to host memory.

     The stack snapshot has the following layout of all 32-bit Motorola-order longwords:
     0x00: argc (int32_t)
     0x04+(4*n): argv[n] (with n from 0 to argc) as offsets from 0
     0x04+(4*argc): NULL to terminate argv
     0x04+(4*(argc+1+m): envp[m] (with m from 0 to envc) as offsets from 0
     0x04+(4*(argc+1+envc): NULL to terminate envp
     0x04+(4*(argc+1+envc+1): start of storage for argv and envp values
     */

    uint8_t *minix_stack_on_host = MINIXCompat_RAM_Copy_Block_To_Host(minix_stack, minix_stack_size);

    // Perform the exec(2) itself. This will do things like reset the emulator and install an adjusted version of the stack snapshot in emulator RAM.

    int16_t exec_err = MINIXCompat_Processes_ExecuteWithStackBlock(minix_path_on_host, minix_stack_on_host, minix_stack_size);

    // exec(2) receives mess2
    // - m_type: result (OK)

    // If the exec(2) failed, reply with a message containing the failure. If the exec(2) succeeds, there's no reply, just the execution starting.

    MINIXCompat_Message_Clear(message);
    message->m_type = exec_err;
    MINIXCompat_Message_Swap(mess2, message);

    // Clean up.
    
    free(minix_path_on_host);

    return minix_syscall_result_success;
}


MINIXCOMPAT_SOURCE_END
