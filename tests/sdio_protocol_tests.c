#include <stdio.h>

typedef unsigned char UCHAR;
typedef unsigned short USHORT;
typedef unsigned long ULONG;

#include "../src/sdio/sdio_protocol.h"

static int gFailures;

static void
CheckUlong(
    const char *Name,
    ULONG Actual,
    ULONG Expected
    )
{
    if (Actual != Expected)
    {
        fprintf(stderr,
                "FAIL %s: actual=0x%08lX expected=0x%08lX\n",
                Name,
                Actual,
                Expected);
        gFailures++;
    }
}

int
main(void)
{
    CheckUlong("CMD52 read fn0/address0",
               SdioBuildCmd52Argument(0, 0, 0, 0, 0),
               0x00000000UL);
    CheckUlong("CMD52 write/raw/max address",
               SdioBuildCmd52Argument(1, 1, 1, 0x1FFFFUL, 0xA5),
               0x9BFFFEA5UL);
    CheckUlong("CMD53 byte read count 512 encoding",
               SdioBuildCmd53Argument(0, 2, 0, 1, 0x00020UL, 512),
               0x24004000UL);
    CheckUlong("CMD53 block write",
               SdioBuildCmd53Argument(1, 2, 1, 1, 0x12345UL, 0x1FF),
               0xAE468BFFUL);

    CheckUlong("clock 200MHz to 400kHz",
               SdioCalculateClockDivider(200000, 400),
               250);
    CheckUlong("clock 200MHz to 200kHz",
               SdioCalculateClockDivider(200000, 200),
               500);
    CheckUlong("clock 200MHz to 100kHz",
               SdioCalculateClockDivider(200000, 100),
               1000);
    CheckUlong("clock 200MHz to 25MHz",
               SdioCalculateClockDivider(200000, 25000),
               4);
    CheckUlong("clock already below target",
               SdioCalculateClockDivider(25000, 50000),
               0);

    CheckUlong("R5 data-only response",
               (ULONG)SdioR5HasError(0x000000A5UL),
               0);
    CheckUlong("R5 CRC error",
               (ULONG)SdioR5HasError(0x00008000UL),
               1);
    CheckUlong("R5 invalid function",
               (ULONG)SdioR5HasError(0x00000200UL),
               1);

    CheckUlong("CMD5 attempt 1 clock",
               SdioGetCmd5TargetClockKhz(0),
               400);
    CheckUlong("CMD5 attempt 3 clock",
               SdioGetCmd5TargetClockKhz(2),
               400);
    CheckUlong("CMD5 attempt 4 clock",
               SdioGetCmd5TargetClockKhz(3),
               200);
    CheckUlong("CMD5 attempt 7 clock",
               SdioGetCmd5TargetClockKhz(6),
               100);
    CheckUlong("CMD5 out-of-range attempt",
               SdioGetCmd5TargetClockKhz(SDIO_CMD5_MAX_ATTEMPTS),
               0);

    if (gFailures != 0)
    {
        fprintf(stderr, "%d SDIO protocol test(s) failed.\n", gFailures);
        return 1;
    }

    puts("All SDIO protocol tests passed.");
    return 0;
}
