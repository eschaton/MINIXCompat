//
//  MINIXCompat_Emulation.c
//  MINIXCompat
//
//  Created by Chris Hanson on 11/4/24.
//  Copyright © 2024 Christopher M. Hanson. See file LICENSE for details.
//

#include "MINIXCompat_Emulation.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <arpa/inet.h> /* for ntohs et al */

#include "MINIXCompat_Types.h"
#include "MINIXCompat_Executable.h"
#include "MINIXCompat_SysCalls.h"

#include "m68k.h"


MINIXCOMPAT_SOURCE_BEGIN


/*!
 The RAM for the emulated CPU. Since all I/O is handled via system calls, there are no I/O devices and there isn't even a bootstrap ROM. Instead, the "bootstrap" is handled by manually setting a PC value and starting execution. This gives us the entire 16MB address space to work with, modulo data structures stored at specific addresses.

 Note that everything within this array is in Motorola (network) byte order.
 */
static uint8_t *MINIXCompat_RAM;


int MINIXCompat_CPU_Trap_Callback(int trap);


int MINIXCompat_CPU_Initialize(void)
{
    // Configure the RAM.

    MINIXCompat_RAM = calloc(16 * 1024 * 1024, sizeof(uint8_t));
    assert(MINIXCompat_RAM != NULL);

    // Configure the CPU.

    m68k_init();
    m68k_set_cpu_type(M68K_CPU_TYPE_68000);
    m68k_set_trap_instr_callback(MINIXCompat_CPU_Trap_Callback);

    // Ready to reset and then execute instructions!

    return 0;
}


void MINIXCompat_CPU_Reset(void)
{
    // Establish initial vectors (really only SSP and PC).

    MINIXCompat_RAM_Write_32(0x000000, MINIXCompat_Stack_Base);
    MINIXCompat_RAM_Write_32(0x000004, MINIXCompat_Executable_Base);

    // Ensure the CPU starts in user mode with interrupts unmasked.

    m68k_set_reg(M68K_REG_SR, 0x00000000);

    // Pulse reset so the CPU can be run.

    m68k_pulse_reset();
}


int MINIXCompat_CPU_Run(int cycles)
{
    return m68k_execute(cycles);
}


int MINIXCompat_CPU_Trap_Callback(int trap)
{
    assert((trap >= 0x0) && (trap <= 0xf));
    bool handled = false;

    switch (trap) {
        case 0x0: {
            // Get D0.w, D1.w, A0 and use them for func, src_dest, and msg respectively.
            uint32_t D0_l = m68k_get_reg(NULL, M68K_REG_D0);
            uint32_t D1_l = m68k_get_reg(NULL, M68K_REG_D1);
            uint32_t A0 = m68k_get_reg(NULL, M68K_REG_A0);

            minix_syscall_func_t func = (uint16_t) D0_l;
            uint16_t src_dest = (uint16_t) D1_l;
            m68k_address_t msg = A0;

            uint32_t new_D0_l = 0;
            minix_syscall_result_t syscall_result = MINIXCompat_System_Call(func, src_dest, msg, &new_D0_l);
            switch (syscall_result) {
                case minix_syscall_result_success:
                    m68k_set_reg(M68K_REG_D0, htonl(new_D0_l));
                    break;

                case minix_syscall_result_success_empty:
                    m68k_set_reg(M68K_REG_D0, 0x00000000);
                    break;

                case minix_syscall_result_failure:
                    m68k_set_reg(M68K_REG_D0, 0xFFFFFFFF);
                    break;
            }

            handled = true;
        } break;

        default: {
            // Let the CPU handle the trap.
            handled = false;
        } break;
    }

    return handled ? 1 : 0;
}


uint8_t MINIXCompat_RAM_Read_8(m68k_address_t m68k_address)
{
    assert(m68k_address < 0x01000000);
    const uint8_t *RAM = MINIXCompat_RAM + m68k_address;
    const uint8_t *RAM8 = (const uint8_t *)RAM;
    return *RAM8;
}

uint16_t MINIXCompat_RAM_Read_16(m68k_address_t m68k_address)
{
    assert(m68k_address < 0x01000000);
    const uint8_t *RAM = MINIXCompat_RAM + m68k_address;
    const uint16_t *RAM16 = (const uint16_t *)RAM;
    return ntohs(*RAM16);
}

uint32_t MINIXCompat_RAM_Read_32(m68k_address_t m68k_address)
{
    assert(m68k_address < 0x01000000);
    const uint8_t *RAM = MINIXCompat_RAM + m68k_address;
    const uint32_t *RAM32 = (const uint32_t *)RAM;
    return ntohl(*RAM32);
}


void MINIXCompat_RAM_Write_8(m68k_address_t m68k_address, uint8_t value)
{
    assert(m68k_address < 0x01000000);
    uint8_t *RAM = MINIXCompat_RAM + m68k_address;
    uint8_t *RAM8 = (uint8_t *)RAM;
    *RAM8 = value;
}

void MINIXCompat_RAM_Write_16(m68k_address_t m68k_address, uint16_t value)
{
    assert(m68k_address < 0x01000000);
    uint8_t *RAM = MINIXCompat_RAM + m68k_address;
    uint16_t *RAM16 = (uint16_t *)RAM;
    *RAM16 = htons(value);
}

void MINIXCompat_RAM_Write_32(m68k_address_t m68k_address, uint32_t value)
{
    assert(m68k_address < 0x01000000);
    uint8_t *RAM = MINIXCompat_RAM + m68k_address;
    uint32_t *RAM32 = (uint32_t *)RAM;
    *RAM32 = htonl(value);
}


void MINIXCompat_RAM_Copy_Block_From_Host(m68k_address_t m68k_address, void *host_block_address, uint32_t host_block_size)
{
    assert(m68k_address < 0x01000000);
    assert((host_block_size + m68k_address) <= 0x01000000);
    uint8_t *RAM = MINIXCompat_RAM + m68k_address;

    memcpy(RAM, host_block_address, host_block_size);
}


void *MINIXCompat_RAM_Copy_Block_To_Host(m68k_address_t m68k_address, uint32_t m68k_block_size)
{
    assert(m68k_address < 0x01000000);
    assert((m68k_block_size + m68k_address) <= 0x01000000);

    const uint8_t *RAM = MINIXCompat_RAM + m68k_address;

    uint8_t *host_block = malloc(m68k_block_size);
    assert(host_block != NULL);

    memcpy(host_block, RAM, m68k_block_size);

    return host_block;
}


unsigned int m68k_read_memory_8(unsigned int address)
{
    return MINIXCompat_RAM_Read_8(address);
}

unsigned int m68k_read_memory_16(unsigned int address)
{
    return MINIXCompat_RAM_Read_16(address);
}

unsigned int m68k_read_memory_32(unsigned int address)
{
    return MINIXCompat_RAM_Read_32(address);
}

unsigned int m68k_read_disassembler_8  (unsigned int address)
{
    return MINIXCompat_RAM_Read_8(address);
}

unsigned int m68k_read_disassembler_16 (unsigned int address)
{
    return MINIXCompat_RAM_Read_16(address);
}

unsigned int m68k_read_disassembler_32 (unsigned int address)
{
    return MINIXCompat_RAM_Read_32(address);
}

void m68k_write_memory_8(unsigned int address, unsigned int value)
{
    assert(value < 0x00000100);
    MINIXCompat_RAM_Write_8(address, value);
}

void m68k_write_memory_16(unsigned int address, unsigned int value)
{
    assert(value < 0x00010000);
    MINIXCompat_RAM_Write_16(address, value);
}

void m68k_write_memory_32(unsigned int address, unsigned int value)
{
    MINIXCompat_RAM_Write_32(address, value);
}


MINIXCOMPAT_SOURCE_END
