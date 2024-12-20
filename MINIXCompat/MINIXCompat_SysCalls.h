//
//  MINIXCompat_SysCalls.h
//  MINIXCompat
//
//  Created by Chris Hanson on 11/30/24.
//  Copyright © 2024 Christopher M. Hanson. See file LICENSE for details.
//

#ifndef MINIXCompat_SysCalls_h
#define MINIXCompat_SysCalls_h

#include <stdint.h>

#include "MINIXCompat_Types.h"


MINIXCOMPAT_HEADER_BEGIN


/*! system calls to mm/fs */
typedef enum minix_syscall: int16_t {
    minix_syscall_unused0    = 0,
    minix_syscall_exit       = 1,
    minix_syscall_fork       = 2,
    minix_syscall_read       = 3,
    minix_syscall_write      = 4,
    minix_syscall_open       = 5,
    minix_syscall_close      = 6,
    minix_syscall_wait       = 7,
    minix_syscall_creat      = 8,
    minix_syscall_link       = 9,
    minix_syscall_unlink     = 10,
    minix_syscall_exec       = 11,
    minix_syscall_chdir      = 12,
    minix_syscall_time       = 13,
    minix_syscall_mknod      = 14,
    minix_syscall_chmod      = 15,
    minix_syscall_chown      = 16,
    minix_syscall_break      = 17,
    minix_syscall_stat       = 18,
    minix_syscall_lseek      = 19,
    minix_syscall_getpid     = 20,
    minix_syscall_mount      = 21,
    minix_syscall_umount     = 22,
    minix_syscall_setuid     = 23,
    minix_syscall_getuid     = 24,
    minix_syscall_stime      = 25,
    minix_syscall_ptrace     = 26,
    minix_syscall_alarm      = 27,
    minix_syscall_fstat      = 28,
    minix_syscall_pause      = 29,
    minix_syscall_utime      = 30,
    minix_syscall_stty       = 31,
    minix_syscall_gtty       = 32,
    minix_syscall_access     = 33,
    minix_syscall_nice       = 34,
    minix_syscall_ftime      = 35,
    minix_syscall_sync       = 36,
    minix_syscall_kill       = 37,
    minix_syscall_rename     = 38,
    minix_syscall_mkdir      = 39,
    minix_syscall_rmdir      = 40,
    minix_syscall_dup        = 41,
    minix_syscall_pipe       = 42,
    minix_syscall_times      = 43,
    minix_syscall_prof       = 44,
    minix_syscall_unused45   = 45,
    minix_syscall_setgid     = 46,
    minix_syscall_getgid     = 47,
    minix_syscall_sig        = 48,
    minix_syscall_unused49   = 49,
    minix_syscall_unused50   = 50,
    minix_syscall_acct       = 51,
    minix_syscall_phys       = 52,
    minix_syscall_lock       = 53,
    minix_syscall_ioctl      = 54,
    minix_syscall_fcntl      = 55,
    minix_syscall_mpx        = 56,
    minix_syscall_unused57   = 57,
    minix_syscall_unused58   = 58,
    minix_syscall_exece      = 59,
    minix_syscall_umask      = 60,
    minix_syscall_chroot     = 61,
    minix_syscall_unused62   = 62,
    minix_syscall_unused63   = 63,
    minix_syscall_KSIG       = 64, /* signals originating in the kernel */
    minix_syscall_UNPAUSE    = 65,
    minix_syscall_BRK2       = 66, /* used to tell MM size of FS,INIT */
    minix_syscall_REVIVE     = 67,
    minix_syscall_TASK_REPLY = 68,
    minix_syscall_unused69   = 69,
} minix_syscall_t;


/*!
 The `func` parameter to the `send`, `receive`, and `sendrec` system calls.
 */
typedef enum minix_syscall_func: uint16_t {
    /*! Send the message, blocking until sent.. */
    minix_syscall_func_send        = 1,

    /*! Receive a message, blocking until one is available. */
    minix_syscall_func_receive        = 2,

    /*! Send the message then receive a response to it, blocking until the response is received. */
    minix_syscall_func_both        = 3,
} minix_syscall_func_t;


/*!
 Initialize the System Call subsystem. The only way this can fail is if other things fail to initialize first.
 */
MINIXCOMPAT_EXTERN void MINIXCompat_SysCall_Initialize(void);


/*!
 The result of a system call indicates whether/how to pass a value back in `d0.l` in the emulator.
 */
typedef enum minix_syscall_result: int16_t {
    /*! A host-side error of some sort that shouldn't be passed to the emulator occurred. */
    minix_syscall_result_failure = -1,

    /*! The call completed successfully and has no updated `d0.l` value.*/
    minix_syscall_result_success_empty        = 0,

    /*! The call completed successfully and has an updated `d0.l` value. */
    minix_syscall_result_success        = 1,
} minix_syscall_result_t;

/*!
 MINIX 1.5.10.7 has only one system call, which is "send or receive a message (or both)."

 MINIX implements this as an M68000 `TRAP #0` instruction with the following register usage:
 - On call:
   - `d0.w` contains the function (a ``minix_syscall_func_t``).
   - `d1.l` contains the src_dest (the task to which the message should be sent to and/or recieved from); and,
   - `a0` contains a pointer to the message itself.
 - On return:
   - `d0.l` *may* contain a result.

 MINIX can also save and restore task state, including performing task switches, around system call invocations. However our implementation doesn't since it only runs a single task.

 The return value from this function is a tri-state ``minix_syscall_result_t``.
*/
MINIXCOMPAT_EXTERN minix_syscall_result_t MINIXCompat_System_Call(minix_syscall_func_t func, uint16_t src_dest, m68k_address_t msg, uint32_t * _Nonnull out_result);


MINIXCOMPAT_HEADER_END


#endif /* MINIXCompat_SysCalls_h */
