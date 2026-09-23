using System.Diagnostics;
using System.Security.AccessControl;
using System.Security.Principal;

namespace Rpi5Wifi;

internal sealed class OperationBusyException : InvalidOperationException
{
    public OperationBusyException() : base("Another Wi-Fi operation is running. Wait for it to finish.") { }
}

internal sealed class OperationLease : IDisposable
{
    private readonly Mutex mutex;
    private bool owned;
    public OperationLease()
    {
        var acl = new MutexSecurity(); acl.SetAccessRuleProtection(true, false);
        foreach (string sid in new[] { "S-1-5-18", "S-1-5-32-544" })
            acl.AddAccessRule(new MutexAccessRule(new SecurityIdentifier(sid), MutexRights.FullControl, AccessControlType.Allow));
        mutex = MutexAcl.Create(false, @"Global\RPi5WiFi.Operation.v1", out _, acl);
        try
        {
            foreach (MutexAccessRule rule in mutex.GetAccessControl().GetAccessRules(true, true, typeof(SecurityIdentifier)))
                if (rule.AccessControlType == AccessControlType.Allow && rule.IdentityReference.Value is not ("S-1-5-18" or "S-1-5-32-544"))
                    throw new InvalidOperationException("Unsafe operation lock permissions.");
            try { owned = mutex.WaitOne(0); } catch (AbandonedMutexException) { owned = true; }
            if (!owned) throw new OperationBusyException();
        }
        catch { Dispose(); throw; }
    }
    public void Dispose() { if (owned) { owned = false; mutex.ReleaseMutex(); } mutex.Dispose(); }
}

internal static class Operations
{
    // Called on one dedicated worker thread, so named-mutex ownership never
    // crosses an await. Status calls are memory-only, not radio polling.
    public static bool Connect(IDriver driver, string country, string ssid, byte[] pmk, CancellationToken cancel, Action<string> progress)
    {
        using var lease = new OperationLease();
        var state = LiveState.Parse(driver.Call(0x126004));
        if (state.Authenticated && state.Status == 0) { progress("Already connected; no reconnect requested."); return false; }
        if (!state.Idle) throw new InvalidOperationException("Driver must be disconnected and ready before connecting.");
        byte[] request = Protocol.Connect(country, ssid, pmk);
        try { cancel.ThrowIfCancellationRequested(); driver.Call(0x12A000, request); }
        finally { System.Security.Cryptography.CryptographicOperations.ZeroMemory(request); }
        var watch = Stopwatch.StartNew();
        while (watch.Elapsed < TimeSpan.FromMinutes(3))
        {
            cancel.ThrowIfCancellationRequested(); state = LiveState.Parse(driver.Call(0x126004)); progress(state.Progress);
            if (state.Status != 0) throw new InvalidOperationException(state.Progress);
            if (state.Authenticated) return true;
            if (cancel.WaitHandle.WaitOne(250)) cancel.ThrowIfCancellationRequested();
        }
        throw new TimeoutException("Connection observation timed out. No reset or repeated join was attempted.");
    }
    public static ScanReport Scan(IDriver driver, string country, CancellationToken cancel, Action<string> progress)
    {
        using var lease = new OperationLease();
        if (!LiveState.Parse(driver.Call(0x126004)).Idle) throw new InvalidOperationException("Scanning is available only when disconnected and ready.");
        var previous = ScanReport.Parse(driver.Call(0x126018));
        if (previous.State is 1 or 2) throw new InvalidOperationException("A scan is already running.");
        uint generation = 0; bool accepted = false, complete = false;
        try
        {
            cancel.ThrowIfCancellationRequested(); driver.Call(0x12A014, Protocol.Scan(country)); accepted = true;
            var watch = Stopwatch.StartNew();
            while (watch.Elapsed < TimeSpan.FromSeconds(90))
            {
                var report = ScanReport.Parse(driver.Call(0x126018));
                if (report.Generation == previous.Generation) throw new InvalidDataException("A fresh scan was not observed.");
                if (generation == 0) generation = report.Generation;
                if (generation != report.Generation) throw new InvalidDataException("Scan ownership changed.");
                cancel.ThrowIfCancellationRequested(); progress($"Scanning nearby networks — {watch.Elapsed.TotalSeconds:F0}s...");
                if (report.State == 3 && report.Status == 0) { complete = true; return report; }
                if (report.State is 4 or 5 || report.Status is not (0 or 259))
                    throw new InvalidOperationException($"Scan failed: 0x{report.Status:X8}, firmware {report.FirmwareError}. No automatic retry.");
                if (cancel.WaitHandle.WaitOne(250)) cancel.ThrowIfCancellationRequested();
            }
            throw new TimeoutException("Scan timed out; cancellation requested.");
        }
        finally
        {
            if (accepted && !complete)
            {
                try
                {
                    if (generation == 0)
                    {
                        var report = ScanReport.Parse(driver.Call(0x126018));
                        if (report.Generation != previous.Generation && report.State is 1 or 2) generation = report.Generation;
                    }
                    if (generation != 0) { byte[] b = new byte[4]; Protocol.Put(b, 0, generation); driver.Call(0x12A01C, b); }
                }
                catch { /* Bounded driver cleanup remains authoritative; never guess another generation. */ }
            }
        }
    }
    public static void Disconnect(IDriver driver)
    {
        using var lease = new OperationLease(); driver.Call(0x12A008);
    }
}
