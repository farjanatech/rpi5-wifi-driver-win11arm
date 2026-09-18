#pragma once

#include "../driver/driver.h"

#define CYW43455_CHIP_ID 0x4345u

NTSTATUS
Cyw43455Probe(
    _Inout_ PRPI5CYW_ADAPTER Adapter
    );
