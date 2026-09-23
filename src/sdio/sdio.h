#pragma once

#include "../driver/driver.h"
#include "sdio_protocol.h"

#define SDHCI_BLOCK_SIZE              0x04
#define SDHCI_BLOCK_COUNT             0x06
#define SDHCI_ARGUMENT                0x08
#define SDHCI_TRANSFER_MODE           0x0C
#define SDHCI_COMMAND                 0x0E
#define SDHCI_RESPONSE0               0x10
#define SDHCI_BUFFER                  0x20
#define SDHCI_PRESENT_STATE           0x24
#define SDHCI_HOST_CONTROL            0x28
#define SDHCI_POWER_CONTROL           0x29
#define SDHCI_CLOCK_CONTROL           0x2C
#define SDHCI_TIMEOUT_CONTROL         0x2E
#define SDHCI_SOFTWARE_RESET          0x2F
#define SDHCI_INT_STATUS              0x30
#define SDHCI_INT_STATUS_ENABLE       0x34
#define SDHCI_INT_SIGNAL_ENABLE       0x38
#define SDHCI_HOST_CONTROL2           0x3E
#define SDHCI_CAPABILITIES            0x40
#define SDHCI_CAPABILITIES2           0x44
#define SDHCI_HOST_VERSION            0xFE

#define SDHCI_PS_CMD_INHIBIT          0x00000001UL
#define SDHCI_PS_DATA_INHIBIT         0x00000002UL
#define SDHCI_PS_CARD_INSERTED        0x00010000UL

#define SDHCI_HC_DATA_WIDTH_4BIT      0x02
#define SDHCI_HC_HIGH_SPEED_ENABLE    0x04
#define SDHCI_HC_DATA_WIDTH_8BIT      0x20
#define CYW_SDIO_CCCR_BUS_INTERFACE   0x07UL
#define CYW_SDIO_CCCR_CAPS            0x08UL
#define CYW_SDIO_CCCR_SPEED           0x13UL
#define CYW_SDIO_BUS_WIDTH_MASK       0x03
#define CYW_SDIO_BUS_WIDTH_4BIT       0x02
#define CYW_SDIO_SPEED_BSS_MASK       0x0e
#define CYW_SDIO_SPEED_SUPPORTS_HS    0x01
#define CYW_SDIO_SPEED_ENABLE_HS      0x02
#define CYW_SDIO_CAP_LOW_SPEED        0x40
#define CYW_SDIO_OPERATING_CLOCK_KHZ  25000UL
#define CYW_SDIO_HIGH_SPEED_CLOCK_KHZ 50000UL

#define SDHCI_PC_BUS_POWER_ON         0x01
#define SDHCI_PC_BUS_VOLTAGE_180      0x0A
#define SDHCI_PC_BUS_VOLTAGE_300      0x0C
#define SDHCI_PC_BUS_VOLTAGE_330      0x0E

#define SDHCI_CLK_INT_CLK_ENABLE      0x0001
#define SDHCI_CLK_INT_CLK_STABLE      0x0002
#define SDHCI_CLK_SD_CLK_ENABLE       0x0004
#define SDHCI_CLK_FREQ_SEL_SHIFT      8

#define SDHCI_RESET_ALL               0x01
#define SDHCI_RESET_CMD               0x02
#define SDHCI_RESET_DATA              0x04

#define SDHCI_INT_CMD_COMPLETE        0x00000001UL
#define SDHCI_INT_XFER_COMPLETE       0x00000002UL
#define SDHCI_INT_BUFFER_READ_READY   0x00000020UL
#define SDHCI_INT_BUFFER_WRITE_READY  0x00000010UL
#define SDHCI_INT_DATA_ERROR_MASK     0x00700000UL
#define SDHCI_INT_ERROR               0x00008000UL
#define SDHCI_INT_CMD_TIMEOUT         0x00010000UL
#define SDHCI_INT_CMD_CRC             0x00020000UL
#define SDHCI_INT_CMD_END_BIT         0x00040000UL
#define SDHCI_INT_CMD_INDEX           0x00080000UL
#define SDHCI_INT_DATA_TIMEOUT        0x00100000UL
#define SDHCI_INT_DATA_CRC            0x00200000UL
#define SDHCI_INT_DATA_END_BIT        0x00400000UL
#define SDHCI_INT_RESPONSE_ERROR      0x08000000UL
#define SDHCI_INT_ALL_MASK            0xFFFFFFFFUL
#define SDHCI_INT_CMD_ERROR_MASK      (SDHCI_INT_CMD_TIMEOUT | SDHCI_INT_CMD_CRC | SDHCI_INT_CMD_END_BIT | SDHCI_INT_CMD_INDEX | SDHCI_INT_RESPONSE_ERROR)

#define SDHCI_CAP_BASE_CLK_MASK       0x0000FF00UL
#define SDHCI_CAP_BASE_CLK_SHIFT      8
#define SDHCI_CAP_HIGH_SPEED          0x00200000UL
#define SDHCI_CAP_VOLTAGE_330         0x01000000UL
#define SDHCI_CAP_VOLTAGE_300         0x02000000UL
#define SDHCI_CAP_VOLTAGE_180         0x04000000UL

#define SDHCI_CMD_RESP_NONE           0x0000
#define SDHCI_CMD_RESP_136            0x0001
#define SDHCI_CMD_RESP_48             0x0002
#define SDHCI_CMD_RESP_48_BUSY        0x0003
#define SDHCI_CMD_RESP_MASK           0x0003
#define SDHCI_CMD_CRC_CHECK           0x0008
#define SDHCI_CMD_INDEX_CHECK         0x0010
#define SDHCI_CMD_DATA_PRESENT        0x0020
#define SDHCI_TRNS_READ               0x0010
#define SDHCI_CMD_INDEX_SHIFT         8
#define SDHCI_MAKE_CMD(_idx,_flags)   ((USHORT)(((_idx) << SDHCI_CMD_INDEX_SHIFT) | (_flags)))

#define SDCMD_GO_IDLE_STATE           0
#define SDCMD_IO_SEND_OP_COND         5
#define SDCMD_SEND_RELATIVE_ADDR      3
#define SDCMD_SELECT_CARD             7
#define SDCMD_IO_RW_DIRECT            52
#define SDCMD_IO_RW_EXTENDED          53

#define SDIO_OCR_READY                0x80000000UL
#define SDIO_OCR_NUM_FUNCTIONS_MASK   0x70000000UL
#define SDIO_OCR_NUM_FUNCTIONS_SHIFT  28
#define SDIO_OCR_VDD_RANGE            0x00FF8000UL

#define CYW_SDIO_CCCR_REVISION        0x00000UL
#define CYW_SDIO_CCCR_IO_ENABLE       0x00002UL
#define CYW_SDIO_CCCR_IO_READY        0x00003UL
#define CYW_SDIO_F1_INTERFACE         0x00100UL
#define CYW_SDIO_F2_INTERFACE         0x00200UL

NTSTATUS SdioCmd52Read(PRPI5CYW_ADAPTER Adapter, UCHAR Function,
                       ULONG Address, PUCHAR Value);
NTSTATUS SdioCmd52Write(PRPI5CYW_ADAPTER Adapter, UCHAR Function,
                        ULONG Address, UCHAR Value, UCHAR VerifyMask);
NTSTATUS SdioCmd53Read(PRPI5CYW_ADAPTER Adapter, UCHAR Function,
                       ULONG Address, PUCHAR Buffer, ULONG Length);
/* Startup does not call this until chip RAM/core state is hardware-validated. */
NTSTATUS SdioCmd53Write(PRPI5CYW_ADAPTER Adapter, UCHAR Function,
                        ULONG Address, PUCHAR Buffer, ULONG Length);
VOID SdioDelayMilliseconds(ULONG Milliseconds);
NTSTATUS SdioFifoTransfer(PRPI5CYW_ADAPTER Adapter, PUCHAR Buffer,
                          ULONG Length, BOOLEAN Write);
/* Serialized PASSIVE_LEVEL only. Optional standards-gated high-speed SDR;
 * never UHS/DDR, voltage switching, DMA, or a board-specific register guess.
 * Successful negotiation/restore still requires caller CMD53 verification. */
NTSTATUS SdioRestoreIdentificationBus(PRPI5CYW_ADAPTER Adapter);
NTSTATUS SdioRestoreDefaultOperatingBus(PRPI5CYW_ADAPTER Adapter);
NTSTATUS SdioNegotiateOperatingSpeed(PRPI5CYW_ADAPTER Adapter);
