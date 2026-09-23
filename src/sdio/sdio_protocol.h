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

#define SDIO_CMD5_CLOCK_COUNT             3UL
#define SDIO_CMD5_CYCLES_PER_CLOCK        3UL
#define SDIO_CMD5_COMMANDS_PER_CYCLE      2UL
#define SDIO_CMD5_MAX_CYCLES              (SDIO_CMD5_CLOCK_COUNT * SDIO_CMD5_CYCLES_PER_CLOCK)
#define SDIO_CMD5_MAX_ATTEMPTS            (SDIO_CMD5_MAX_CYCLES * SDIO_CMD5_COMMANDS_PER_CYCLE)

/* Single byte-mode incrementing transfer; never silently wrap an address. */
static __forceinline int
SdioIsValidByteRead(UCHAR Function, ULONG Address, ULONG Length)
{
    return Function >= 1 && Function <= 7 && Length >= 1 && Length <= 512 &&
           Address <= SDIO_CMD53_ADDRESS_MASK &&
           (Length - 1) <= SDIO_CMD53_ADDRESS_MASK - Address;
}

static __forceinline ULONG
SdioLoadLe32(const UCHAR *Bytes)
{
    return (ULONG)Bytes[0] | ((ULONG)Bytes[1] << 8) |
           ((ULONG)Bytes[2] << 16) | ((ULONG)Bytes[3] << 24);
}

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
SdioGetCmd5CycleClockKhz(
    ULONG CycleIndex
    )
{
    static const ULONG ClockKhz[SDIO_CMD5_CLOCK_COUNT] = { 400UL, 200UL, 100UL };
    ULONG ClockIndex = CycleIndex / SDIO_CMD5_CYCLES_PER_CLOCK;

    if (ClockIndex >= SDIO_CMD5_CLOCK_COUNT)
    {
        return 0;
    }

    return ClockKhz[ClockIndex];
}

static __forceinline int
SdioR4HasBasicInfo(
    ULONG Response
    )
{
    return ((Response & 0x70000000UL) != 0) &&
           ((Response & 0x00FF8000UL) != 0);
}

static __forceinline int
SdioR4IsReady(
    ULONG Response
    )
{
    return SdioR4HasBasicInfo(Response) &&
           ((Response & 0x80000000UL) != 0);
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

/* Standard SD high-speed (single data rate), NOT UHS SDR50 or DDR50.
 * Linux v6.12 mmc/core/sdio.c mmc_sdio_switch_hs and host/sdhci.h:
 * host HISPD capability bit21, card CCCR SPEED SHS bit0/EHS bit1.
 * This implementation knows SDHCI3.0 divided-clock mode only. Unknown host
 * versions, UHS/1.8V/tuning/preset/v4 modes and absent base-clock evidence do
 * not authorize changing timing. Those paths stay at the existing baseline.
 */
#define SDIO_HS_REJECT_HOST_VERSION 0x01UL
#define SDIO_HS_REJECT_HOST_CAP     0x02UL
#define SDIO_HS_REJECT_BASE_CLOCK   0x04UL
#define SDIO_HS_REJECT_HOST_MODE    0x08UL
#define SDIO_HS_REJECT_CCCR_VERSION 0x10UL
#define SDIO_HS_REJECT_CARD_CAP     0x20UL
#define SDIO_HS_REJECT_CARD_MODE    0x40UL
#define SDIO_HS_REJECT_NO_INCREASE  0x80UL
static __forceinline ULONG
SdioHighSpeedRejectReason(ULONG HostVersion, ULONG Capabilities,
                         USHORT HostControl2, UCHAR CccrRevision, UCHAR CardSpeed)
{
    ULONG BaseKhz, ActualKhz, Reason=0;
    USHORT Divider;
    if((HostVersion & 0xffUL)!=2)Reason|=SDIO_HS_REJECT_HOST_VERSION;
    if(!(Capabilities & 0x00200000UL))Reason|=SDIO_HS_REJECT_HOST_CAP;
    if(!(Capabilities & 0x0000ff00UL))Reason|=SDIO_HS_REJECT_BASE_CLOCK;
    if(HostControl2 & 0x90cfU)Reason|=SDIO_HS_REJECT_HOST_MODE;
    if((CccrRevision & 15U)<2 || (CccrRevision & 15U)>3)Reason|=SDIO_HS_REJECT_CCCR_VERSION;
    if(!(CardSpeed & 1U))Reason|=SDIO_HS_REJECT_CARD_CAP;
    if(CardSpeed & 14U)Reason|=SDIO_HS_REJECT_CARD_MODE;
    if(Reason)return Reason;
    BaseKhz=((Capabilities >> 8) & 255UL)*1000UL;
    Divider=SdioCalculateClockDivider(BaseKhz,50000UL);
    ActualKhz=Divider?BaseKhz/(2UL*Divider):BaseKhz;
    return ActualKhz>25000UL && ActualKhz<=50000UL?0:SDIO_HS_REJECT_NO_INCREASE;
}
static __forceinline int
SdioCanUseHighSpeed(ULONG HostVersion, ULONG Capabilities,
                   USHORT HostControl2, UCHAR CccrRevision, UCHAR CardSpeed)
{return SdioHighSpeedRejectReason(HostVersion,Capabilities,HostControl2,CccrRevision,CardSpeed)==0;}
