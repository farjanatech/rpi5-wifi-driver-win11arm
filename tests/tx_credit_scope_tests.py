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
    assert re.search(r"(?m)^DriverVer\s*=\s*10/04/2026,0\.7\.1\.8\s*$", inf), "Candidate version must be distinct from stable"
    # Every original TX queue/ownership, transport, RX, interrupt, retry, radio,
    # firmware and power implementation is covered above, not just file names.
    network = (ROOT / "src/cyw43455/network.c").read_text()
    early = network.index("Status=CywTxCreditPostReceivePump(A,&N->Sends,&sentAfter);")
    assert network.index("CywTxDiagRx(&A->TxCreditDiag") < early < network.index("CywTransportSample(A);")
    assert network.count("Status=CywTxCreditPostReceivePump(") == 1
    assert "#if !RPI5CYW_TX_CREDIT_SCHEDULING" in network
    # 0.7.1.8 mode 1 must not spend worker cycles on a known-exhausted queue.
    # Exact equality is supplied by the already-tested read-only eligibility gate.
    skip = "if(CywTxRetryEligible(A))sentBefore=0;"
    assert skip in network, "Known exhausted TX queue must skip the initial pump"
    assert network.index(skip) < network.index("Status=CywMeasuredTxPump(A,&N->Sends,4,&sentBefore);")
    # Middle policy: preserve the already-tested bounded retry selector
    # (up to four 1 ms event waits, then its 10 ms backoff), but never re-add
    # the 0.7.1.7 direct 10 ms exhausted-credit override.
    retry_call = "retryMs=CywTxRetrySelect(&A->TxRetry,KeQueryInterruptTime(),"
    assert retry_call in network, "Bounded fast retry selector must remain active"
    assert "if(CywTxRetryEligible(A)) {CywTxRetryReset(&A->TxRetry);retryMs=10;}" not in network
    retry = (ROOT / "src/cyw43455/tx_retry.h").read_text()
    assert "#define CYW_TX_RETRY_FAST_LIMIT 4u" in retry
    assert "return 1;" in retry and "return 10;" in retry
    pump = (ROOT / "src/cyw43455/tx_credit_pump.h").read_text()
    assert pump.count("CywTxPostReceivePump(A,Q,Sent)") == 1
    assert "CywMeasuredTxPump(" not in pump and "CywTxPressureEligible(" not in pump
    assert "for(" not in pump and "while(" not in pump, "No experimental extension loop"
    assert "#define RPI5CYW_TX_CREDIT_SCHEDULING 0" in (ROOT / "src/cyw43455/tx_credit_diag.h").read_text()
    project = (ROOT / "rpi5-cyw43455.vcxproj").read_text()
    assert '<Rpi5TxCreditScheduling Condition="\'$(Rpi5TxCreditScheduling)\'==\'\'">1</Rpi5TxCreditScheduling>' in project
    package = (ROOT / "scripts/package-tx-credit.ps1").read_text()
    assert "[ValidateSet('0','1')][string]$TxCreditScheduling='1'" in package
    print("PASS: immutable v0.7.1.4 source, bounded 64-frame queue, bounded fast credit wake, promoted mode-1 defaults, exact rollback path.")

if __name__ == "__main__":
    main()
