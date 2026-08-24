#pragma once

/*
 * Pure SDIO/SDHCI protocol helpers. Keep these free of MMIO and kernel calls so
 * the exact argument/divider logic can be compiled and tested on the CI host.
 * ULONG/UCHAR/USHORT are supplied by driver.h in the kernel build and by the
 * small test harness in tests/sdio_protocol_tests.c.
 */

#define SDIO_CMD52_WRITE_SHIFT       31
#define SDIO_CMD52_FUNCTION_SHIFT    28
#define SDIO_CMD52_RAW_SHIFT         27
#define SDIO_CMD52_ADDRESS_SHIFT      9
#define SDIO_CMD52_ADDRESS_MASK       0x1FFFFUL

#define SDIO_CMD53_WRITE_SHIFT       31
#define SDIO_CMD53_FUNCTION_SHIFT    28
#define SDIO_CMD53_BLOCK_SHIFT       27
#define SDIO_CMD53_INCREMENT_SHIFT   26
#define SDIO_CMD53_ADDRESS_SHIFT      9
#define SDIO_CMD53_ADDRESS_MASK       0x1FFFFUL
#define SDIO_CMD53_COUNT_MASK         0x1FFUL

#define SDIO_R5_ERROR_MASK            0x0000CB00UL

#define SDIO_CMD5_CLOCK_COUNT          3UL
#define SDIO_CMD5_RETRIES_PER_CLOCK    3UL
#define SDIO_CMD5_MAX_ATTEMPTS         (SDIO_CMD5_CLOCK_COUNT * SDIO_CMD5_RETRIES_PER_CLOCK)

static __forceinline ULONG
SdioBuildCmd52Argument(
    int Write,
    UCHAR Function,
    int Raw,
    ULONG Address,
    UCHAR Data
    )
{
    return (Write ? (1UL << SDIO_CMD52_WRITE_SHIFT) : 0) |
           ((ULONG)(Function & 7U) << SDIO_CMD52_FUNCTION_SHIFT) |
           (Raw ? (1UL << SDIO_CMD52_RAW_SHIFT) : 0) |
           ((Address & SDIO_CMD52_ADDRESS_MASK) << SDIO_CMD52_ADDRESS_SHIFT) |
           (ULONG)Data;
}

static __forceinline ULONG
SdioBuildCmd53Argument(
    int Write,
    UCHAR Function,
    int BlockMode,
    int IncrementAddress,
    ULONG Address,
    ULONG Count
    )
{
    return (Write ? (1UL << SDIO_CMD53_WRITE_SHIFT) : 0) |
           ((ULONG)(Function & 7U) << SDIO_CMD53_FUNCTION_SHIFT) |
           (BlockMode ? (1UL << SDIO_CMD53_BLOCK_SHIFT) : 0) |
           (IncrementAddress ? (1UL << SDIO_CMD53_INCREMENT_SHIFT) : 0) |
           ((Address & SDIO_CMD53_ADDRESS_MASK) << SDIO_CMD53_ADDRESS_SHIFT) |
           (Count & SDIO_CMD53_COUNT_MASK);
}

static __forceinline int
SdioR5HasError(
    ULONG Response
    )
{
    return (Response & SDIO_R5_ERROR_MASK) != 0;
}

static __forceinline ULONG
SdioGetCmd5TargetClockKhz(
    ULONG AttemptIndex
    )
{
    static const ULONG ClockKhz[SDIO_CMD5_CLOCK_COUNT] = { 400UL, 200UL, 100UL };
    ULONG ClockIndex = AttemptIndex / SDIO_CMD5_RETRIES_PER_CLOCK;

    if (ClockIndex >= SDIO_CMD5_CLOCK_COUNT)
    {
        return 0;
    }

    return ClockKhz[ClockIndex];
}

static __forceinline USHORT
SdioCalculateClockDivider(
    ULONG BaseClockKhz,
    ULONG TargetClockKhz
    )
{
    ULONG RealDivisor;

    if (BaseClockKhz == 0 || TargetClockKhz == 0 ||
        BaseClockKhz <= TargetClockKhz)
    {
        return 0;
    }

    for (RealDivisor = 2; RealDivisor < 2046; RealDivisor += 2)
    {
        if ((unsigned long long)BaseClockKhz <=
            ((unsigned long long)TargetClockKhz * RealDivisor))
        {
            break;
        }
    }

    if (RealDivisor >= 2046)
    {
        RealDivisor = 2046;
    }

    return (USHORT)(RealDivisor >> 1);
}
