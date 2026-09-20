/* Included by the actual-code host harness after CHECK/Erom are defined. */
typedef struct _EROM_TEST_STREAM {
    ULONG Words[520], Count, Index, FailAt;
} EROM_TEST_STREAM;

static NTSTATUS TestNextWord(PVOID Context, PULONG Word)
{
    EROM_TEST_STREAM *Stream = Context;
    if (Stream->Index == Stream->FailAt) return STATUS_IO_TIMEOUT;
    if (Stream->Index >= Stream->Count) return STATUS_DEVICE_DATA_ERROR;
    *Word = Stream->Words[Stream->Index++];
    return STATUS_SUCCESS;
}

static void InitEromStream(EROM_TEST_STREAM *Stream)
{
    memset(Stream, 0, sizeof(*Stream));
    memcpy(Stream->Words, Erom, sizeof(Erom));
    Stream->Count = sizeof(Erom) / sizeof(Erom[0]);
    Stream->FailAt = 0xFFFFFFFF;
}

static void RunEromTests(void)
{
    EROM_TEST_STREAM Stream;
    CYW_CORE_MAP Map;
    ULONG Index;
    InitEromStream(&Stream);
    CHECK(CywParseErom(TestNextWord, &Stream, &Map) == 0);
    CHECK(Map.Count == 4 && Map.Cr4 == 0x18004000 && Map.Cr4Wrapper == 0x18102000);
    for (Index = 0; Index < sizeof(Erom) / sizeof(Erom[0]); Index++)
    {
        InitEromStream(&Stream); Stream.Count = Index;
        CHECK(!NT_SUCCESS(CywParseErom(TestNextWord, &Stream, &Map)));
        InitEromStream(&Stream); Stream.FailAt = Index;
        CHECK(CywParseErom(TestNextWord, &Stream, &Map) == STATUS_IO_TIMEOUT);
    }
    InitEromStream(&Stream); Stream.Words[1] = 0; /* malformed component B */
    CHECK(!NT_SUCCESS(CywParseErom(TestNextWord, &Stream, &Map)));
    InitEromStream(&Stream); Stream.Words[4] = Stream.Words[0]; /* duplicate CC */
    CHECK(!NT_SUCCESS(CywParseErom(TestNextWord, &Stream, &Map)));
    InitEromStream(&Stream); Stream.Words[14] = 0xDEAD0005; /* address outside chip */
    CHECK(!NT_SUCCESS(CywParseErom(TestNextWord, &Stream, &Map)));
    InitEromStream(&Stream); Stream.Words[14] = Stream.Words[2]; /* aliases CC */
    CHECK(!NT_SUCCESS(CywParseErom(TestNextWord, &Stream, &Map)));
    InitEromStream(&Stream); Stream.Words[16] = 0xFFFFFFFF; /* bus float != EOT */
    CHECK(!NT_SUCCESS(CywParseErom(TestNextWord, &Stream, &Map)));
    InitEromStream(&Stream); Stream.Words[16] = 0; /* no EOT */
    CHECK(!NT_SUCCESS(CywParseErom(TestNextWord, &Stream, &Map)));
    memset(&Stream, 0, sizeof(Stream)); Stream.Count = 520; Stream.FailAt = 0xFFFFFFFF;
    CHECK(!NT_SUCCESS(CywParseErom(TestNextWord, &Stream, &Map)));
    CHECK(Stream.Index == 512);
    /* A >32-bit descriptor payload which resembles EOT must not terminate scan. */
    InitEromStream(&Stream);
    memmove(&Stream.Words[4], &Stream.Words[2], sizeof(Erom) - 2 * sizeof(ULONG));
    Stream.Words[2] = 0x1900000D; Stream.Words[3] = 0xF;
    Stream.Count += 2;
    CHECK(CywParseErom(TestNextWord, &Stream, &Map) == 0);
    /* Explicit-size descriptor + upper size, neither a component nor EOT. */
    InitEromStream(&Stream);
    memmove(&Stream.Words[5], &Stream.Words[2], sizeof(Erom) - 2 * sizeof(ULONG));
    Stream.Words[2] = 0x19000035; Stream.Words[3] = 0x1008; Stream.Words[4] = 0xF;
    Stream.Count += 3;
    CHECK(CywParseErom(TestNextWord, &Stream, &Map) == 0);
    CHECK(CywParseErom(NULL, &Stream, &Map) == STATUS_INVALID_PARAMETER);
    CHECK(CywParseErom(TestNextWord, &Stream, NULL) == STATUS_INVALID_PARAMETER);
    puts("PASS: bounded EROM parser, truncation, IO failures, payloads, invalid addresses and duplicates.");
}
