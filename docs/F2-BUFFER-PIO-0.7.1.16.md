# v0.7.1.16 — bounded F2 register-buffer PIO

Branch: **`new-improvement-f2-buffer-pio`**  
Direct experiment baseline: **v0.7.1.15 / `66e0198609913fcc407c595e580e39c83777b60f`**  
Immediate rollback: **v0.7.1.11 / `6ae93623c8767eda050b8c408250d3ec3ce19bfb`**

## Evidence that motivated this change

v0.7.1.15 successfully reduced host TX status/credit pressure, but the Fast.com
result moved only from roughly 40 to 42 Mbps upload while download fell from
about 82 to 64 Mbps. That means more TX scheduling changes are not justified.

The latest driver evidence also showed roughly 71–72 MiB of F2 block-mode data
during each 64 MiB download stage. With 512-byte blocks that is around 140,000
F2 blocks. The current block-mode implementation moves each block through the
SDHCI BUFFER register using 128 individual 32-bit register accesses.

Windows provides READ_REGISTER_BUFFER_ULONG and WRITE_REGISTER_BUFFER_ULONG
for moving a sequence of ULONG values through a mapped device register. The
next experiment therefore targets the common F2 PIO copy path used by both
download/RX and upload/TX.

## Exact change

The SDIO wire protocol is unchanged.

For an aligned 512-byte F2 block, v0.7.1.16 divides the block into four bounded
128-byte chunks. Each chunk uses one register-buffer operation containing
32 ULONG values:

- RX: `READ_REGISTER_BUFFER_ULONG(..., 32)`
- TX: `WRITE_REGISTER_BUFFER_ULONG(..., 32)`

That changes the host-side register API call count from **128 scalar calls per
block to four bounded buffer calls per block**.

The driver checks `IoStopped` before every 128-byte chunk, so cancellation /
shutdown observation remains bounded to at most one 128-byte chunk rather than
one full 512-byte block.

If the supplied F2 buffer is not ULONG-aligned, the exact scalar implementation
is retained as a fallback.

## Preserved v0.7.1.15 gains

The entire Service-Burst2 path is byte-for-byte protected against the v0.7.1.15
baseline:

- fresh F1 is still required for the first ordinary frame;
- at most one following ordinary F2 can reuse that fresh service;
- two genuine firmware credits are still required;
- the grant is single-use and pump-local;
- failed F2 transfers are never replayed.

## Other unchanged behavior

- active TX queue: **64 frames**;
- deferred backlog: **128 frames**;
- glom pressure threshold: **32**;
- glom cap: **2 frames**;
- F2 block size: **512 bytes**;
- maximum block command: **32 blocks**;
- F2 CMD53 address/mode semantics unchanged;
- SDIO bus: verified **50 MHz / 4-bit** when available;
- extended SDPCM framing from v0.7.1.11 unchanged;
- RX batching/glom policy unchanged;
- firmware, NVRAM, CLM, country and radio behavior unchanged;
- interrupt policy unchanged;
- no DMA/ADMA;
- no queue enlargement;
- no utility changes.

## Failure semantics

A partial FIFO operation is still terminal. There is no automatic replay or
fallback after a block has started. Existing reset/failure latching remains
unchanged.

## Diagnostics

DiagVersion **42** adds:

- `FifoBufferPioEnabled=1`
- `FifoBufferPioBurstWords=32`
- `FifoBufferPioReadBlocks`
- `FifoBufferPioWriteBlocks`
- `FifoScalarPioBlocks`

Existing Service-Burst2 diagnostics remain available.

## Hardware qualification

Do not use the abandoned all-in-one speed benchmark for performance decisions.

After install/reboot:

1. Run Fast.com under the same AP, 5 GHz band and physical placement.
2. Record download and upload.
3. Collect diagnostics only.

The important reference points are:

- v0.7.1.11: about **82 Mbps download / 40 Mbps upload**;
- v0.7.1.15: about **64 Mbps download / 42 Mbps upload**.

A successful v0.7.1.16 should recover/improve download while preserving or
improving upload and all stability counters. If not, reject it and return to
v0.7.1.11.
