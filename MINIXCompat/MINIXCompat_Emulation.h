//
//  MINIXCompat_Emulation.h
//  MINIXCompat
//
//  Created by Chris Hanson on 11/4/24.
//  Copyright © 2024 Christopher M. Hanson. See file LICENSE for details.
//

#ifndef MINIXCompat_Emulation_h
#define MINIXCompat_Emulation_h

#include "MINIXCompat_Types.h"


MINIXCOMPAT_HEADER_BEGIN


/*! Initialize the CPU emulation. */
MINIXCOMPAT_EXTERN int MINIXCompat_CPU_Initialize(void);

/*! Reste the CPU emulation, necessary after everything is initialized and configred but before running. */
MINIXCOMPAT_EXTERN void MINIXCompat_CPU_Reset(void);

/*! Run the CPU emulation. */
MINIXCOMPAT_EXTERN int MINIXCompat_CPU_Run(int cycles);


/*! Get the current program counter. */
MINIXCOMPAT_EXTERN m68k_address_t MINIXCompat_CPU_GetPC(void);

/*! Set the current program counter. */
MINIXCOMPAT_EXTERN void MINIXCompat_CPU_SetPC(m68k_address_t value);

/*! Get the status register. */
MINIXCOMPAT_EXTERN uint16_t MINIXCompat_CPU_GetSR(void);

/*! Push a 16-bit word on the stack, and return the address to which it was pushed.. */
MINIXCOMPAT_EXTERN m68k_address_t MINIXCompat_CPU_Push_16(uint16_t value);

/*! Push a 32-bit word on the stack, and return the address to which it was pushed. */
MINIXCOMPAT_EXTERN m68k_address_t MINIXCompat_CPU_Push_32(uint32_t value);


/*! Read the 8-bit byte at \a m68k_address from RAM. */
MINIXCOMPAT_EXTERN uint8_t MINIXCompat_RAM_Read_8(m68k_address_t m68k_address);

/*! Read the 16-bit word at \a m68k_address from RAM, converting to host byte order. */
MINIXCOMPAT_EXTERN uint16_t MINIXCompat_RAM_Read_16(m68k_address_t m68k_address);

/*! Read the 32-bit longword at \a m68k_address from RAM, converting to host byte order. */
MINIXCOMPAT_EXTERN uint32_t MINIXCompat_RAM_Read_32(m68k_address_t m68k_address);

/*! Read the 8-bit byte \a value to \a m68k_address in RAM. */
MINIXCOMPAT_EXTERN void MINIXCompat_RAM_Write_8(m68k_address_t m68k_address, uint8_t value);

/*! Read the 16-bit word \a value to \a m68k_address in RAM, converting from host byte order. */
MINIXCOMPAT_EXTERN void MINIXCompat_RAM_Write_16(m68k_address_t m68k_address, uint16_t value);

/*! Read the 32-bit longword \a value to \a m68k_address in RAM, converting from host byte order. */
MINIXCOMPAT_EXTERN void MINIXCompat_RAM_Write_32(m68k_address_t m68k_address, uint32_t value);


/*!
 Copy a block of memory to the emulated CPU from the host address space.

 The `host_block_size` plus the `m68k_address` must not extend past the end of the 16MB address space.
 */
MINIXCOMPAT_EXTERN void MINIXCompat_RAM_Copy_Block_From_Host(m68k_address_t m68k_address, void *host_block_address, uint32_t host_block_size);

/*!
 Copy a block of memory from the emulated CPU to the host address space. The copied block is a new allocation created with `calloc` that must be freed using `free`.

 The `m68k_block_size` plus the `m68k_address` must not extend past the end of the 16MB address space.
 */
MINIXCOMPAT_EXTERN void *MINIXCompat_RAM_Copy_Block_To_Host(m68k_address_t m68k_address, uint32_t m68k_block_size);


MINIXCOMPAT_HEADER_END


#endif /* MINIXCompat_Emulation_h */
