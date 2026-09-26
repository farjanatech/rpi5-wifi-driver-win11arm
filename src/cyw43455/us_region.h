/* SPDX-License-Identifier: GPL-3.0-or-later
 * Admission-only policy for the US firmware comparison package.
 * Never rewrites a country or relaxes firmware SET/readback validation.
 * No hardware I/O, queue, timing, credential storage or packet-path changes.
 */
#ifndef CYW_US_REGION_H
#define CYW_US_REGION_H
#include "network_protocol.h"
static __inline int CywUsCountryAllowed(const uint8_t *country)
{ return country && country[0]=='U' && country[1]=='S'; }
static __inline int CywUsValidConnect(const CYW_CONNECT_REQUEST *request)
{ return CywValidConnect(request) && CywUsCountryAllowed(request->Country); }
#endif
