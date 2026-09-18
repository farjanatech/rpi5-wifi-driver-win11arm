#pragma once
/* Host-only simulation. No real registers, kernel APIs, or driver installation. */
#include <stdint.h>
#include <string.h>
#include <stddef.h>
typedef unsigned char UCHAR, BOOLEAN, *PUCHAR;
typedef unsigned short USHORT, *PUSHORT;
typedef unsigned long ULONG, *PULONG;
typedef unsigned long long ULONG64;
typedef long NTSTATUS;
typedef long long LONGLONG;
typedef void VOID, *PVOID, *NDIS_HANDLE, *PDEVICE_OBJECT;
typedef int NDIS_MEDIA_CONNECT_STATE, NDIS_MEDIA_DUPLEX_STATE;
typedef struct { ULONG LowPart; long HighPart; } NDIS_PHYSICAL_ADDRESS;
typedef struct { LONGLONG QuadPart; } LARGE_INTEGER;
typedef NTSTATUS DRIVER_INITIALIZE(void);
#ifndef _In_
#define _In_
#define _Inout_
#define _Out_
#define _Out_opt_
#endif
#define TRUE 1
#define FALSE 0
#define PASSIVE_LEVEL 0
#define APC_LEVEL 1
#define KernelMode 0
#define C_ASSERT(x) _Static_assert(x, #x)
#define STATUS_SUCCESS ((NTSTATUS)0)
#define STATUS_INVALID_PARAMETER ((NTSTATUS)0xC000000DL)
#define STATUS_INVALID_DEVICE_STATE ((NTSTATUS)0xC0000184L)
#define STATUS_IO_TIMEOUT ((NTSTATUS)0xC00000B5L)
#define STATUS_IO_DEVICE_ERROR ((NTSTATUS)0xC0000185L)
#define STATUS_DEVICE_DATA_ERROR ((NTSTATUS)0xC000009CL)
#define STATUS_DEVICE_CONFIGURATION_ERROR ((NTSTATUS)0xC0000182L)
#define STATUS_INTERNAL_ERROR ((NTSTATUS)0xC00000E5L)
#define NT_SUCCESS(s) ((NTSTATUS)(s) >= 0)
#define RtlZeroMemory(p,n) memset(p,0,n)
#define KeMemoryBarrier() ((void)0)
extern int TestIrql;
#define KeGetCurrentIrql() TestIrql
void KeStallExecutionProcessor(ULONG Microseconds);
NTSTATUS KeDelayExecutionThread(int Mode, BOOLEAN Alertable, LARGE_INTEGER *Delay);
UCHAR READ_REGISTER_UCHAR(PUCHAR Address);
USHORT READ_REGISTER_USHORT(PUSHORT Address);
ULONG READ_REGISTER_ULONG(PULONG Address);
void WRITE_REGISTER_UCHAR(PUCHAR Address, UCHAR Value);
void WRITE_REGISTER_USHORT(PUSHORT Address, USHORT Value);
void WRITE_REGISTER_ULONG(PULONG Address, ULONG Value);
