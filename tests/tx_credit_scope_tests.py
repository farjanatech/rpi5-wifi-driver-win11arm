"""Exact v0.7.1.4 isolation guard. Run from any working directory."""
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
BASELINE = "c0b032543f945707c82ffc1b05a8ca7012821560"
OBSERVED = {"src/driver/driver.h", "src/cyw43455/network.c", "src/cyw43455/transport_send.h"}
NEW = {"src/cyw43455/tx_credit_diag.h", "src/cyw43455/tx_credit_runtime.h", "src/cyw43455/tx_credit_pump.h"}

def git(*args):
    return subprocess.check_output(["git", "-C", str(ROOT), *args], text=True)

def strip_observations(text):
    for kind in ("DIAG", "SCHED"):
        begin = "/* TX-CREDIT-" + kind + "-BEGIN */\n"
        end = "/* TX-CREDIT-" + kind + "-END */\n"
        assert text.count(begin) == text.count(end), "Unbalanced permitted splice markers"
        text = re.sub(re.escape(begin) + r".*?" + re.escape(end), "", text, flags=re.S)
    return text

def main():
    git("merge-base", "--is-ancestor", BASELINE, "HEAD")
    files = git("ls-tree", "-r", "--name-only", BASELINE, "src").splitlines()
    for name in files:
        actual = (ROOT / name).read_text(encoding="utf-8")
        before = git("show", BASELINE + ":" + name)
        if name in OBSERVED:
            actual = strip_observations(actual)
        assert actual == before, "Protected baseline code changed: " + name
    current = {p.relative_to(ROOT).as_posix() for p in (ROOT / "src").rglob("*") if p.is_file()}
    assert current - set(files) == NEW, "Unexpected new production surface: " + str(current - set(files))
    assert "#define RPI5CYW_TX_LIMIT 64u" in (ROOT / "src/driver/driver.h").read_text()
    inf = (ROOT / "package/rpi5cyw.inf").read_text()
    assert re.search(r"(?m)^DriverVer\s*=\s*10/03/2026,0\.7\.1\.5\s*$", inf), "Candidate version must be distinct from stable"
    # Every original TX queue/ownership, transport, RX, interrupt, retry, radio,
    # firmware and power implementation is covered above, not just file names.
    network = (ROOT / "src/cyw43455/network.c").read_text()
    early = network.index("Status=CywTxCreditPostReceivePump(A,&N->Sends,&sentAfter);")
    assert network.index("CywTxDiagRx(&A->TxCreditDiag") < early < network.index("CywTransportSample(A);")
    assert network.count("Status=CywTxCreditPostReceivePump(") == 1
    assert "#if !RPI5CYW_TX_CREDIT_SCHEDULING" in network
    print("PASS: immutable v0.7.1.4 protected source, exact removable TX observation/scheduling splices, 64-frame cap, rollback path.")

if __name__ == "__main__":
    main()
