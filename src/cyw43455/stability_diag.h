/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

/* Stable diagnostic source IDs. These describe why an already-up connection
 * was observed transitioning down; they do not change recovery behavior. */
#define CYW_DISCONNECT_SOURCE_NONE          0u
#define CYW_DISCONNECT_SOURCE_FIRMWARE      1u
#define CYW_DISCONNECT_SOURCE_EXPLICIT      2u
#define CYW_DISCONNECT_SOURCE_POWER         3u
#define CYW_DISCONNECT_SOURCE_WORKER        4u

/* Broadcom event numbers follow brcmfmac fweh.h. */
#define CYW_FW_EVENT_DEAUTH                 5u
#define CYW_FW_EVENT_DEAUTH_IND             6u
#define CYW_FW_EVENT_DISASSOC              11u
#define CYW_FW_EVENT_DISASSOC_IND          12u
#define CYW_FW_EVENT_LINK                  16u
#define CYW_FW_EVENT_AUTHORIZED            46u
#define CYW_FW_EVENT_LINK_FLAG              0x01u

#define CYW_FW_DISCONNECT_NONE               0u
#define CYW_FW_DISCONNECT_DEAUTH             1u
#define CYW_FW_DISCONNECT_DISASSOC           2u
#define CYW_FW_DISCONNECT_LINK_DOWN          3u
#define CYW_FW_DISCONNECT_AUTH_LOSS          4u
#define CYW_FW_DISCONNECT_OTHER              5u

static __inline unsigned int
CywFirmwareDisconnectClass(
    unsigned int Type,
    unsigned int Status,
    unsigned int Flags
    )
{
    if(Type==CYW_FW_EVENT_DEAUTH || Type==CYW_FW_EVENT_DEAUTH_IND)
        return CYW_FW_DISCONNECT_DEAUTH;
    if(Type==CYW_FW_EVENT_DISASSOC || Type==CYW_FW_EVENT_DISASSOC_IND)
        return CYW_FW_DISCONNECT_DISASSOC;
    if(Type==CYW_FW_EVENT_LINK &&
       ((Flags&CYW_FW_EVENT_LINK_FLAG)==0 || Status!=0))
        return CYW_FW_DISCONNECT_LINK_DOWN;
    if(Type==CYW_FW_EVENT_AUTHORIZED && Status!=6)
        return CYW_FW_DISCONNECT_AUTH_LOSS;
    if(Type==0 && Status!=0)
        return CYW_FW_DISCONNECT_OTHER;
    return CYW_FW_DISCONNECT_NONE;
}
