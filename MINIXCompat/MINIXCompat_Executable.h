//
//  MINIXCompat_Executable.h
//  MINIXCompat
//
//  Created by Chris Hanson on 11/6/24.
//  Copyright © 2024 Christopher M. Hanson. See file LICENSE for details.
//

#ifndef MINIXCompat_Executable_h
#define MINIXCompat_Executable_h

#include <stdint.h>
#include <stdio.h>

#include "MINIXCompat_Types.h"


MINIXCOMPAT_HEADER_BEGIN


/*! Forward declaration of opaque MINIXExcutable structure. */
struct MINIXCompat_Executable;


/*! Base address in emulated CPU  at which every executable is loaded. */
MINIXCOMPAT_EXTERN const m68k_address_t MINIXCompat_Executable_Base;

/*! First address after ``MINIXCompat_Executable_Base`` in emulated CPU  after end of initialized and uninitialized data. */
MINIXCOMPAT_EXTERN const m68k_address_t MINIXCompat_Executable_Limit;

/*! Top of the MINIX stack at start of execution, above which are argc, argv, envp, and their contents, up to 64KB worth. */
MINIXCOMPAT_EXTERN const m68k_address_t MINIXCompat_Stack_Base;

/*! Limit of the MINIX stack, *below* which it must not grow since that would collide with initialized and uniitalized data. */
MINIXCOMPAT_EXTERN const m68k_address_t MINIXCompat_Stack_Limit;

/*!
 Get the process' initial break value
 */
MINIXCOMPAT_EXTERN m68k_address_t MINIXCompat_Executable_Get_Initial_Break(void);


/*!
 Loads a MINIX executable.

 Loads a MINIX executable's text (code) and data from \a pef into host RAM, with proper alignment to the MINIX 256-byte "click" size, and performs any relocation the file indicates is necessary.

 - Parameters:
   - pef: pointer to `FILE` from which to load the executable
   - out_peh: where to place the pointer to the new `struct MINIXCompat_Executable` representing the executable, which should be `NULL` on entry
   - out_buf: where to place the pointer to the buffer containing the executable, which should be `NULL` on entry
   - out_buf_len: where to place the length of the buffer containing the executable

 - Returns:
   - `0` on success, `-errno` on error; if `out_pef` or `out_buf` points to a non-`NULL` pointer, those must be freed by the caller with `free(3)`, even if an error has occurred.

 - Warning: Allocation of both `out_peh` and `out_buf` is performed with `calloc()` and is the caller's responsibility to `free()`.
            These should explicitly point to `NULL` pointers before a call, and may still need to be freed upon error.
 */
MINIXCOMPAT_EXTERN int MINIXCompat_Executable_Load(FILE *pef, struct MINIXCompat_Executable * _Nullable * _Nonnull out_peh, uint8_t * _Nullable * _Nonnull out_buf, uint32_t *out_buf_len);


MINIXCOMPAT_HEADER_END


#endif /* MINIXCompat_Executable_h */
