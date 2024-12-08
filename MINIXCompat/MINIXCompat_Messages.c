//
//  MINIXCompat_Messages.c
//  MINIXCompat
//
//  Created by Chris Hanson on 11/16/24.
//  Copyright © 2024 Christopher M. Hanson. See file LICENSE for details.
//

#include "MINIXCompat_Messages.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

#include <arpa/inet.h> /* for ntohs et al */

#include "MINIXCompat_Types.h"

#ifndef HTONS
#define HTONS(x) ((x) = htons(x))
#define HTONL(x) ((x) = htonl(x))
#endif

MINIXCOMPAT_SOURCE_BEGIN


#if defined(__LITTLE_ENDIAN__)

static void MINIXCompat_Message_Swap_header(minix_message_t * _Nonnull msg)
{
    assert(msg != NULL);

    HTONS(msg->m_source);
    HTONS(msg->m_type);
}

void MINIXCompat_Message_Swap_mess1(minix_message_t * _Nonnull msg)
{
    MINIXCompat_Message_Swap_header(msg);
    HTONS(msg->m1_i1);
    HTONS(msg->m1_i2);
    HTONS(msg->m1_i3);
    HTONL(msg->m1_p1);
    HTONL(msg->m1_p2);
    HTONL(msg->m1_p3);
}

void MINIXCompat_Message_Swap_mess2(minix_message_t * _Nonnull msg)
{
    MINIXCompat_Message_Swap_header(msg);

    HTONS(msg->m2_i1);
    HTONS(msg->m2_i2);
    HTONS(msg->m2_i3);
    HTONL(msg->m2_l1);
    HTONL(msg->m2_l2);
    HTONL(msg->m2_p1);
}

void MINIXCompat_Message_Swap_mess3(minix_message_t * _Nonnull msg)
{
    MINIXCompat_Message_Swap_header(msg);

    HTONS(msg->m3_i1);
    HTONS(msg->m3_i2);
    HTONL(msg->m3_p1);
    // m3_ca1 doesn't need any swapping
}

void MINIXCompat_Message_Swap_mess4(minix_message_t * _Nonnull msg)
{
    MINIXCompat_Message_Swap_header(msg);

    HTONL(msg->m4_l1);
    HTONL(msg->m4_l2);
    HTONL(msg->m4_l3);
    HTONL(msg->m4_l4);
}

void MINIXCompat_Message_Swap_mess5(minix_message_t * _Nonnull msg)
{
    MINIXCompat_Message_Swap_header(msg);

    // m5_c1 needs no swapping
    // m5_c2 needs no swapping
    HTONS(msg->m5_i1);
    HTONS(msg->m5_i2);
    HTONL(msg->m5_l1);
    HTONL(msg->m5_l2);
    HTONL(msg->m5_l3);
}

void MINIXCompat_Message_Swap_mess6(minix_message_t * _Nonnull msg)
{
    MINIXCompat_Message_Swap_header(msg);

    HTONS(msg->m6_i1);
    HTONS(msg->m6_i2);
    HTONS(msg->m6_i3);
    HTONL(msg->m6_l1);
    HTONL(msg->m6_f1);
}

#endif


void MINIXCompat_Message_Clear(minix_message_t * _Nonnull msg)
{
    assert(msg != NULL);

    memset(msg, 0, sizeof(minix_message_t));
}


MINIXCOMPAT_SOURCE_END
