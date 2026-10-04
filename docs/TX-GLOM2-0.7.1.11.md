# Host TX glom framing fix candidate 0.7.1.11

Branch: **`new-improvement-tx-glom-fix`**  
Failed predecessor retained: **v0.7.1.10 / `new-improvement-tx-glom` / `a8eb3a6ff480821be885faf398e25857a248dc8f`**  
Hardware rollback: **v0.7.1.9 / `new-improvement` / `0bc5ed12e9438cd82555d7bc8ae77211aa6f236b`**

## Hardware finding from v0.7.1.10

The Pi logs showed:

- v0.7.1.10 loaded successfully at 50 MHz / 4-bit SDIO;
- `bus:rxglom=1` negotiation succeeded (`TxGlomEnabled=1`, config status 0);
- no aggregate data transfer had yet occurred;
- the worker then failed with `STATUS_IO_TIMEOUT (0xC00000B5)`;
- the last firmware command was WLC_SET_VAR (263);
- request capacity was 8 bytes and value length 4 bytes, matching the next
  `mpc=0` IOVAR in startup configuration.

Linux brcmfmac switches its host TX header length globally after
`bus:rxglom` succeeds. v0.7.1.10 incorrectly used the extended SDPCM header
only for two-frame data aggregates. The next normal control IOVAR therefore
used the legacy 12-byte framing after firmware had switched to the extended
format, so firmware did not return a control response.

## 0.7.1.11 correction

Once host TX glom negotiation succeeds, **every subsequent host-to-firmware
frame** uses the 20-byte extended SDPCM TX header:

- single control / IOVAR frames;
- single data frames;
- two-frame aggregate data chains.

The negotiation request itself is still sent in the legacy 12-byte format
because the firmware has not switched modes yet.

For a single extended frame:

- the first hardware length covers the complete padded transfer;
- the extension declares one final subframe;
- tail padding is encoded in the extension;
- the SDPCM software header moves from offset 4 to offset 12;
- data offset becomes 20;
- sequence and credit accounting remain one frame.

The existing two-frame pair format/selection/ownership code is otherwise
unchanged from v0.7.1.10.

## Lifecycle correction

A fresh firmware boot always starts in legacy host-TX framing. Therefore
`CywConfigure` now clears negotiated glom framing **before CLM/WLC/IOVAR
traffic** on every worker life. D3/D0 or a future worker restart cannot inherit
a stale `TxGlomEnabled=1` from the previous firmware instance.

## Unchanged safety boundaries

- active TX queue remains 64 frames;
- pending backlog remains 128 frames;
- two-frame aggregation cap remains exactly 2;
- pair eligibility, two-credit gate and BUSY fallback remain unchanged;
- pair F2 failure remains no-replay / fail-both;
- RX aggregation/parser is unchanged;
- SDIO clock, bus width, PIO transport and interrupt logic are unchanged;
- firmware, CLM, NVRAM, country and radio policy are unchanged.

## New diagnostics

DiagVersion **39** adds:

- `TxGlomExtendedDataSingles`
- `TxGlomExtendedControlSingles`

These prove that post-negotiation single-frame traffic is actually using the
extended format.

## Required first hardware gate

After install/reboot, before any throughput test, collect diagnostics and verify:

- driver version 0.7.1.11;
- `NetworkPhase=500` or later, not 450 failure;
- `NetworkStatus=0`;
- `WorkerFailureCount=0`;
- `TxGlomRequested=1`;
- if firmware supports it, `TxGlomEnabled=1`;
- `TxGlomExtendedControlSingles>0`;
- `TxGlomErrors=0`;
- `Cmd53Timeouts=0`;
- `FifoTransportFailed=0`.

Only after that startup gate is clean should performance/readiness testing run.

## Performance gate

Compare against v0.7.1.9 using the same Pi, UEFI, AP/router, band/channel,
placement and Windows power state. A successful 0.7.1.11 run should show
advancing `TxGlomChains` under sustained pressure, no worker/disconnect/SDIO
regression, and reduced or equal `TxBacklogFull` without a meaningful latency
or throughput regression.

Do not test four-frame aggregation until this corrected two-frame protocol has
clean hardware evidence.
