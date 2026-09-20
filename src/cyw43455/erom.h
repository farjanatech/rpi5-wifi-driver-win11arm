#pragma once
/* Bounded AI EROM decoder. Descriptor definitions follow Linux v6.12
 * brcmfmac/chip.c (ISC, Broadcom 2014); see THIRD_PARTY_NOTICES.md.
 * No allocation or MMIO: the caller supplies a bounded word reader.
 */
typedef NTSTATUS (*CYW_EROM_NEXT)(PVOID Context, PULONG Word);
typedef struct _CYW_CORE_MAP {
    ULONG Count, ChipCommon, Sdio, D11, Cr4, Cr4Wrapper;
} CYW_CORE_MAP;

static __forceinline int CywValidCoreAddress(ULONG Address)
{
    return Address >= 0x18000000UL && Address < 0x18200000UL &&
           (Address & 0xFFFUL) == 0;
}

static NTSTATUS CywRecordCore(CYW_CORE_MAP *Map, ULONG Id,
                             ULONG Base, ULONG Wrapper)
{
    PULONG Target = NULL;
    if (Id == 0x800) Target = &Map->ChipCommon;
    if (Id == 0x829) Target = &Map->Sdio;
    if (Id == 0x812) Target = &Map->D11;
    if (Id == 0x83E) Target = &Map->Cr4;
    if (Target == NULL) return STATUS_SUCCESS;
    if (*Target != 0 || !CywValidCoreAddress(Base) ||
        !CywValidCoreAddress(Wrapper) || Base == Wrapper)
        return STATUS_DEVICE_DATA_ERROR;
    *Target = Base;
    if (Id == 0x83E) Map->Cr4Wrapper = Wrapper;
    return STATUS_SUCCESS;
}

static NTSTATUS CywParseErom(CYW_EROM_NEXT Next, PVOID Context,
                            CYW_CORE_MAP *Map)
{
    ULONG Word, Type, Id = 0, Base = 0, Wrapper = 0, WrapperType = 2;
    ULONG Upper, Size, SizeType, SlaveType, HaveCore = 0, Steps;
    NTSTATUS Status;
    if (Next == NULL || Map == NULL) return STATUS_INVALID_PARAMETER;
    RtlZeroMemory(Map, sizeof(*Map));
    /* Caller also limits actual word reads, including payload words. */
    for (Steps = 0; Steps < 512; Steps++)
    {
        Status = Next(Context, &Word);
        if (!NT_SUCCESS(Status)) return Status;
        if ((Word & 1) == 0) continue;
        Type = Word & 0xFUL;
        if (Type == 1 || Type == 0xF)
        {
            if (HaveCore)
            {
                Status = CywRecordCore(Map, Id, Base, Wrapper);
                if (!NT_SUCCESS(Status)) return Status;
            }
            if (Type == 0xF)
            {
                if (Word != 0xF || Map->ChipCommon != 0x18000000UL ||
                    Map->Sdio == 0 || Map->D11 == 0 || Map->Cr4 == 0)
                    return STATUS_DEVICE_DATA_ERROR;
                if (Map->ChipCommon == Map->Sdio || Map->ChipCommon == Map->Cr4 ||
                    Map->ChipCommon == Map->D11 || Map->Sdio == Map->Cr4 ||
                    Map->Sdio == Map->D11 || Map->Cr4 == Map->D11)
                    return STATUS_DEVICE_DATA_ERROR;
                return STATUS_SUCCESS;
            }
            Id = (Word >> 8) & 0xFFF;
            Status = Next(Context, &Word); /* component B */
            if (!NT_SUCCESS(Status)) return Status;
            if ((Word & 0xF) != 1) return STATUS_DEVICE_DATA_ERROR;
            if (++Map->Count > 32) return STATUS_DEVICE_DATA_ERROR;
            WrapperType = ((Word >> 14) & 0x1F) != 0 ? 3 : 2;
            Base = Wrapper = 0;
            HaveCore = 1;
            continue;
        }
        if ((Type & ~8UL) != 5) continue;
        if (!HaveCore) return STATUS_DEVICE_DATA_ERROR;
        Upper = 0;
        if (Word & 8)
        {
            Status = Next(Context, &Upper);
            if (!NT_SUCCESS(Status)) return Status;
        }
        SizeType = (Word >> 4) & 3;
        if (SizeType == 3)
        {
            Status = Next(Context, &Size);
            if (!NT_SUCCESS(Status)) return Status;
            if (Size & 8)
            {
                Status = Next(Context, &Size);
                if (!NT_SUCCESS(Status)) return Status;
            }
        }
        if (Upper != 0 || SizeType > 1) continue;
        SlaveType = (Word >> 6) & 3;
        if (SlaveType == 0 && Base == 0) Base = Word & 0xFFFFF000UL;
        if (SlaveType == WrapperType && Wrapper == 0)
            Wrapper = Word & 0xFFFFF000UL;
    }
    return STATUS_DEVICE_DATA_ERROR;
}
