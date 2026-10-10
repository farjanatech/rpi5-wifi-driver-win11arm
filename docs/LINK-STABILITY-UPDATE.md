# Link-state correction — 0.7.1.24-damian.4

This version fixes a reproduced firmware-event handling defect. After release,
the owner confirmed it is stable on their Raspberry Pi 5 C1 with Windows 11 ARM64
and requested promotion to `main`. The tested source commit `a268bdb` is now
included in the default branch; `damian-edition/link-stability` is retained.
The ARM64 driver, native GUI and embedded setup all identify version 0.7.1.24.

## Defect and resulting behavior

The CYW43455 firmware's event 46 is `PSK_SUP`, a supplicant state notification.
The driver previously set `Authorized = (status == 6)` for every such event.
Consequently, a key-handshake progress event on an established connection
cleared authorization, indicated disconnected media to Windows, closed the TX
gate, and caused receive packets to be dropped by the host's state check.
The diagnostic classifier also counted this as authentication loss.

The corrected handler preserves existing authorization for the documented
progress states 4, 5, 8, 9, 10 and 11, provided the firmware reports no error
reason. These include the group-key exchange states. Progress never grants
authorization to an unauthenticated connection: only completion (6) without an
error reason does.
Timeout (7), other/unknown non-completion states, and nonzero failure reasons
still revoke authorization. Link-down, deauthentication, disassociation and
failed join events still disconnect. Counters use the same classification.

The state values are defined in the primary
[Linux v6.12 brcmfmac firmware event header](https://github.com/torvalds/linux/blob/v6.12/drivers/net/wireless/broadcom/brcm80211/brcmfmac/fweh.h#L125-L168).
Its [connection handler](https://github.com/torvalds/linux/blob/v6.12/drivers/net/wireless/broadcom/brcm80211/brcmfmac/cfg80211.c#L6118-L6178)
also distinguishes supplicant state from established-link teardown. This patch
retains this driver's conservative failure handling rather than treating every
supplicant notification as an authorization Boolean.

## Evidence and limits

The new host regression executes `CywEvent` extracted from the production C
file, with wire-format packets and observed notification/diagnostic sinks.
Before the fix it failed both the connected-rekey assertion and the subsequent
real-disconnect attribution assertion. After the fix it passes:

- 1,000 complete progress/completion cycles without false disconnection;
- initial handshakes in both association/completion orders, without premature
  authorization;
- supplicant timeouts, unknown statuses, nonzero failure reasons, real drops
  during rekey, duplicate failure notifications, and reconnect;
- scan isolation, band-selection publication gating, wrong BCDC interface,
  truncated packets/payloads and malformed headers.

CI runs the event tests with AddressSanitizer, the existing transport/recovery/
queue regressions, both ARM64 TX build configurations, the pinned Damian UEFI
contract, and native GUI/setup self-tests. The release BUILD.txt and workflow
results identify the exact tested commit.

The owner reported immediate fluctuating 5–80 Mbps downloads on 5 GHz and
repeated disconnects with 0.7.1.23, then confirmed that the released .24 version
is stable. This is user-reported hardware confirmation in addition to the
automated tests. No capture establishes that the earlier drops were PSK_SUP
progress events, so the precise cause of those symptoms remains unconfirmed.

## Scope and installation

Only the kernel event handler and its disconnect classifier differ from .23;
a source guard verifies that boundary. Firmware blobs, SDIO clock/FIFO behavior,
warm reset, runtime recovery, TX/RX scheduling, queue sizes and radio policy are
unchanged. This is not a throughput-tuning release. The earlier async diagnostic
and FIFO-refill fixes remain included. No UEFI flash is required.

Install the new `RPi5-WiFi-Setup.exe` on the Pi and restart Windows. The GUI title
and Device Manager driver version should both be **0.7.1.24**. Updating only the
standalone GUI does not install the driver correction. Existing saved profiles
remain compatible. This package binds only `ACPI\RPI1060`; Damian's `RPI0011`
interrupt provider is preserved. See [installation details](DAMIAN-EDITION.md).

The .23 release remains flagged for its reported problems. The .24 source is
promoted to `main` at the owner's request; no additional driver changes or
replacement release binaries are part of that promotion. The existing release
tag and prerelease designation remain unchanged. Earlier .21/.22 GitHub release
assets were removed by the owner; retained source history does not imply that
those binaries can still be downloaded.
