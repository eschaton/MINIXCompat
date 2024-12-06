//
//  MINIXCompat_Executable.c
//  MINIXCompat
//
//  Created by Chris Hanson on 11/6/24.
//  Copyright © 2024 Christopher M. Hanson. See file LICENSE for details.
//

#include "MINIXCompat_Executable.h"

#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <arpa/inet.h> /* for ntohs et al */

#include "MINIXCompat_Types.h"
#include "MINIXCompat_Errors.h"


MINIXCOMPAT_SOURCE_BEGIN


const m68k_address_t MINIXCompat_Executable_Base  = 0x00001000; // text + data + bss + brk(2)
const m68k_address_t MINIXCompat_Executable_Limit = 0x00fe0000; // heap grows upward
const m68k_address_t MINIXCompat_Stack_Base       = 0x00ff0000; // up to 64KB stack
const m68k_address_t MINIXCompat_Stack_Limit      = 0x00fe0000; // stack grows downward, not upward


const uint32_t MINIX_CLICK_SIZE = 256;
const uint32_t MINIX_CLICK_SHIFT = 8;
#define MINIX_CLICK_ROUND(s) (((s) + MINIX_CLICK_SIZE - 1) >> MINIX_CLICK_SHIFT)


struct minix_exec {
    uint32_t a_magic;
    uint32_t a_flags;
    uint32_t a_text;
    uint32_t a_data;
    uint32_t a_bss;
    uint32_t a_no_entry;
    uint32_t a_total;
    uint32_t a_syms;
} __attribute__((packed));


const uint32_t minix_exec_magic_combined = 0x04100301;
const uint32_t minix_exec_magic_separate = 0x04200301;
const uint32_t minix_exec_flags = 0x00000020;
const uint32_t minix_exec_no_entry = 0x00000000;


/*! The full version of the MINIXExecutable structure. */
struct MINIXCompat_Executable {

    /*! Executable header, swapped to host byte order. */
    struct minix_exec exec_h;
};


static int MINIXExecutableLoadHeader(FILE *pef, struct MINIXCompat_Executable *peh);
static void MINIXExecutableRelocateLongAtOffset(uint8_t *buf, uint32_t relocation_base, uint32_t relocation_offset);
static int MINIXExecutableRelocate(FILE *pef, struct MINIXCompat_Executable *peh, uint8_t *buf);


int MINIXCompat_Executable_Load(FILE *pef, struct MINIXCompat_Executable * _Nullable * _Nonnull out_peh, uint8_t * _Nullable * _Nonnull out_buf, uint32_t *out_buf_len)
{
    assert(pef != NULL);
    assert((out_peh != NULL) && (*out_peh == NULL));
    assert((out_buf != NULL) && (*out_buf == NULL));
    assert(out_buf_len != NULL);

    // Load and validate the executable header.

    struct MINIXCompat_Executable *peh = calloc(sizeof(struct MINIXCompat_Executable), 1);
    if (peh == NULL) return -ENOMEM;
    *out_peh = peh;

    int header_err = MINIXExecutableLoadHeader(pef, peh);
    if (header_err != 0) return header_err;

    // Compute the size of buffer to allocate for the program.

    struct minix_exec *exec_h = &peh->exec_h;

    uint32_t text_clicks = MINIX_CLICK_ROUND(exec_h->a_text);
    uint32_t total_clicks = MINIX_CLICK_ROUND(exec_h->a_total);

    uint8_t *buf = calloc(total_clicks * MINIX_CLICK_SIZE, 1);
    if (buf == NULL) return -ENOMEM;
    *out_buf = buf;
    *out_buf_len = total_clicks * MINIX_CLICK_SIZE;

    // Seek past the header, then load the data and text into the blob at the appropriate offsets.

    int seek_err = fseek(pef, sizeof(struct minix_exec), SEEK_SET);
    if (seek_err != 0) return -MINIXCompat_Errors_MINIXErrorForHostError(errno);

    uint32_t text_base = 0;
    uint32_t data_base = text_base + (text_clicks * MINIX_CLICK_SIZE);

    if (text_clicks > 0) {
        size_t text_read = fread(buf + text_base, exec_h->a_text, 1, pef);
        if (text_read < 1) return -MINIXCompat_Errors_MINIXErrorForHostError(ENOEXEC);
    }

    size_t data_read = fread(buf + data_base, exec_h->a_data, 1, pef);
    if (data_read < 1) return -MINIXCompat_Errors_MINIXErrorForHostError(ENODATA);

    // Relocation information is after any symbol table, so skip that.

    if (exec_h->a_syms) {
        int skip_err = fseek(pef, exec_h->a_syms, SEEK_CUR);
        if (skip_err != 0) return -MINIXCompat_Errors_MINIXErrorForHostError(errno);
    }

    // Do any necessary relocation.

    int relocate_err = MINIXExecutableRelocate(pef, peh, buf);
    if (relocate_err != 0) return -relocate_err;

    return 0;
}


static int MINIXExecutableLoadHeader(FILE *pef, struct MINIXCompat_Executable *peh)
{
    assert(pef != NULL);
    assert(peh != NULL);

    // Get the network byte order header at the head of the file.

    struct minix_exec exec_n;

    int seek_err = fseek(pef, 0, SEEK_SET);
    if (seek_err != 0) return -MINIXCompat_Errors_MINIXErrorForHostError(errno);

    size_t read_count = fread(&exec_n, sizeof(struct minix_exec), 1, pef);
    if (read_count < 1) return -MINIXCompat_Errors_MINIXErrorForHostError(EIO);

    // Copy the network byte order header to the header in host byte order.

    struct minix_exec *exec_h = &peh->exec_h;

    exec_h->a_magic = ntohl(exec_n.a_magic);
    exec_h->a_flags = ntohl(exec_n.a_flags);
    exec_h->a_text = ntohl(exec_n.a_text);
    exec_h->a_data = ntohl(exec_n.a_data);
    exec_h->a_bss = ntohl(exec_n.a_bss);
    exec_h->a_no_entry = ntohl(exec_n.a_no_entry);
    exec_h->a_total = ntohl(exec_n.a_total);
    exec_h->a_syms = ntohl(exec_n.a_syms);

    // Validate the header.

    if (   (exec_h->a_magic != minix_exec_magic_combined)
        && (exec_h->a_magic != minix_exec_magic_separate)) {
        return -ENOEXEC;
    }

    if (exec_h->a_flags != minix_exec_flags) return -ENOEXEC;
    if (exec_h->a_no_entry != minix_exec_no_entry) return -ENOEXEC;
    if (exec_h->a_total == 0) return -ENOEXEC;

    if (exec_h->a_magic == minix_exec_magic_combined) {
        // Combined I&D is considered all data, so adjust.
        exec_h->a_data += exec_h->a_text;
        exec_h->a_text = 0;
    } else {
        // Do adjustment as demonstrated in MINIX mm/exec.c (if needed).
    }

    return 0;
}


/*!
 Relocate!

 Change the longword at \a relocation_offset so it's relative to \a relocation_base rather than `0`.

 - NOTE: The values in _buf_  are always in network byte order so we have to swap back and forth.
 */
static void MINIXExecutableRelocateLongAtOffset(uint8_t *buf, uint32_t relocation_base, uint32_t relocation_offset)
{
    uint8_t *pb = buf + relocation_offset;
    uint32_t *pl = (uint32_t *)pb;
    uint32_t l = ntohl(*pl);
    uint32_t rl = l + relocation_base;
    *pl = htonl(rl);
}


static int MINIXExecutableRelocate(FILE *pef, struct MINIXCompat_Executable *peh, uint8_t *buf)
{
    assert(pef != NULL);
    assert(peh != NULL);
    assert(buf != NULL);

    // At this point pef should be set to the point in the file where relocation information lives.

    int32_t relocation_initial_offset_n;

    size_t read_count = fread(&relocation_initial_offset_n, sizeof(int32_t), 1, pef);
    if (read_count < 1) {
        // No relocation information, just return success.
        return 0;
    }

    uint32_t relocation_offset = ntohl(relocation_initial_offset_n);
    bool done = (relocation_offset == 0);

    if (!done) {
        // Do the first relocation.

        MINIXExecutableRelocateLongAtOffset(buf, MINIXCompat_Executable_Base, relocation_offset);

        // Do the rest of the relocations one at a time.

        do {
            uint8_t b;
            size_t relo_read_count = fread(&b, sizeof(uint8_t), 1, pef);
            if (relo_read_count < 1) return -EIO;

            if (b == 0x00) {
                // Relocation done.
                done = true;
            } else if (b == 0x01) {
                // Don't relocate, just bump the relocation offset by 254.
                relocation_offset += 254;
            } else if ((b & 0x01) == 0x00) {
                // Bump the offset by the value encoded into b.

                relocation_offset += b;

                MINIXExecutableRelocateLongAtOffset(buf, MINIXCompat_Executable_Base, relocation_offset);
            } else if ((b & 0x01) == 0x01) {
                return -ENOEXEC;
            }
        } while (!done);
    }

    return 0;
}


MINIXCOMPAT_SOURCE_END
