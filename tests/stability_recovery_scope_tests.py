"""v0.7.1.20 stability recovery isolation guard."""
from pathlib import Path
import re,subprocess
ROOT=Path(__file__).resolve().parents[1]
BASELINE="1dbe4b11ef06ee4dde360e5fc8172732d53e0ecc"
UTILITY_BASELINE="e7de4e26d317bca947171c25967b10201380711a"
RETIRED_UTILITY_FILES={"utility/RPi5-WiFi-App.ps1","utility/RPi5-WiFi-App.cmd"}
ALLOWED_SRC={
 "src/sdio/fifo_blocks.h","src/driver/driver.h","src/driver/driver.c",
 "src/cyw43455/network.c","src/cyw43455/runtime_recovery.h",
}
def git(*args):return subprocess.check_output(["git","-C",str(ROOT),*args],text=True)
def main():
 git("merge-base","--is-ancestor",BASELINE,"HEAD")
 files=git("ls-tree","-r","--name-only",BASELINE,"src").splitlines()
 current={p.relative_to(ROOT).as_posix() for p in (ROOT/"src").rglob("*") if p.is_file()}
 assert current==set(files)|{"src/cyw43455/runtime_recovery.h"},"Unexpected production source surface"
 changed=set()
 for name in files:
  actual=(ROOT/name).read_text(encoding="utf-8");before=git("show",BASELINE+":"+name)
  if actual!=before:changed.add(name)
  if name not in ALLOWED_SRC:assert actual==before,"Protected v0.7.1.19 source changed: "+name
 assert changed==ALLOWED_SRC-{"src/cyw43455/runtime_recovery.h"},"Unexpected/missing stability source changes"
 # Performance policy is byte-for-byte protected.
 for name in ("src/cyw43455/tx_types.h","src/cyw43455/tx_queue.h","src/cyw43455/transport_send.h",
              "src/cyw43455/tx_pressure_pump.h","src/cyw43455/tx_credit_pump.h",
              "src/cyw43455/tx_retry_gate.h","src/cyw43455/tx_glom2_config.h",
              "src/cyw43455/connection.h","src/cyw43455/firmware.c"):
  assert (ROOT/name).read_text(encoding="utf-8")==git("show",BASELINE+":"+name),"Protected policy changed: "+name
 fifo=(ROOT/"src/sdio/fifo_blocks.h").read_text()
 assert "Hardware state wins over the software deadline" in fifo
 assert fifo.index("if(status&(SDHCI_INT_ERROR") < fifo.index("now=KeQueryInterruptTime()")
 assert fifo.index("now=KeQueryInterruptTime()") < fifo.index("if(now>=Deadline)break;")
 assert "FifoLateCompletionAccepted" in fifo and "FifoLastFailureWaitEvent" in fifo
 assert "READ_REGISTER_BUFFER_ULONG" not in fifo and "WRITE_REGISTER_BUFFER_ULONG" not in fifo
 network=(ROOT/"src/cyw43455/network.c").read_text()
 assert '#include "runtime_recovery.h"' in network
 assert "CywQueueRuntimeRecovery" in network and "NdisQueueIoWorkItem" in network
 assert "CywNetworkPower(A,FALSE)" in network and "CywNetworkPower(A,TRUE)" in network
 assert "RuntimeRecoveryAttempts" in network
 driver=(ROOT/"src/driver/driver.c").read_text()
 assert 'SET_DWORD(L"DiagVersion", 45);' in driver
 for key in ("FifoLateCompletionAccepted","FifoLastFailureStatus","FifoLastFailureCompletedBlocks",
             "RuntimeRecoveryAttempts","RuntimeRecoveryRestarts","RuntimeRecoveryFailures",
             "RuntimeRecoveryTriggerStatus","RuntimeRecoveryLastStatus"):
  assert key in driver
 header=(ROOT/"src/driver/driver.h").read_text()
 assert "RuntimeRecoveryAttempts" in header and "FifoLastFailureWaitEvent" in header
 utility_files=git("ls-tree","-r","--name-only",UTILITY_BASELINE,"utility").splitlines()
 for name in utility_files:
  if name in RETIRED_UTILITY_FILES:
   assert not (ROOT/name).exists(),"Retired legacy GUI returned: "+name
   continue
  assert (ROOT/name).read_text(encoding="utf-8")==git("show",UTILITY_BASELINE+":"+name),"Frozen utility changed: "+name
 inf=(ROOT/"package/rpi5cyw.inf").read_text()
 assert re.search(r"(?m)^DriverVer\s*=\s*10/05/2026,0\.7\.1\.20\s*$",inf)
 installer=(ROOT/"installer/Install-RPi5-WiFi-Driver.ps1").read_text()
 assert "$script:InstallerVersion = '0.7.1.20'" in installer
 package=(ROOT/"scripts/package-tx-credit.ps1").read_text()
 assert "0.7.1.20 STABILITY RECOVERY" in package
 assert "adaptive_tx_hybrid=1" in package
 assert "fifo_event_before_deadline=1" in package
 assert "runtime_recovery_max=1" in package
 print("PASS: v0.7.1.20 keeps v0.7.1.19 adaptive TX byte-for-byte, fixes only block wait ordering, adds failure evidence and one-shot adapter lifecycle recovery.")
if __name__=="__main__":main()
