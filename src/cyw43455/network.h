#pragma once
#include "../driver/driver.h"
#include "network_protocol.h"

typedef struct _CYW_NETWORK CYW_NETWORK;
NTSTATUS CywNetworkInitialize(PRPI5CYW_ADAPTER Adapter);
VOID CywNetworkStop(PRPI5CYW_ADAPTER Adapter);
VOID CywNetworkPause(PRPI5CYW_ADAPTER Adapter, BOOLEAN Paused);
NDIS_STATUS CywNetworkSend(PRPI5CYW_ADAPTER Adapter, PNET_BUFFER_LIST Nbl);
NTSTATUS CywControlRegister(NDIS_HANDLE DriverHandle);
VOID CywControlDeregister(VOID);
NTSTATUS CywBpRead(PRPI5CYW_ADAPTER Adapter, ULONG Address, PULONG Value);
NTSTATUS CywBpWrite(PRPI5CYW_ADAPTER Adapter, ULONG Address, ULONG Value);
NTSTATUS CywFirmwareStart(PRPI5CYW_ADAPTER Adapter);
VOID CywFirmwareStop(PRPI5CYW_ADAPTER Adapter);
NTSTATUS CywReadFirmwareFile(PCWSTR Name, PUCHAR *Data, PULONG Size, ULONG Limit);
NTSTATUS CywFirmwareCommand(PRPI5CYW_ADAPTER Adapter, ULONG Command,
                            BOOLEAN Set, PUCHAR Data, ULONG Length);
NTSTATUS CywIovar(PRPI5CYW_ADAPTER Adapter, const char *Name,
                  BOOLEAN Set, PUCHAR Data, ULONG Length);
