using System.Security.Cryptography;
using System.Security.AccessControl;
using System.Security.Principal;
using System.Xml.Linq;
namespace Rpi5Wifi;
internal static class SelfTests
{
    private static int assertions;
    private static void Check(bool good, string label) { ++assertions; if (!good) throw new InvalidOperationException(label); }
    private static void Throws(Action action, string label) { try { action(); } catch { ++assertions; return; } throw new InvalidOperationException(label); }
    private static byte[] Status(uint phase = 500, uint error = 0, bool auth = false)
    {
        var b = new byte[96]; Protocol.Put(b, 0, 3); Protocol.Put(b, 4, phase); Protocol.Put(b, 8, error); Protocol.Put(b, 28, auth ? 1u : 0u);
        Protocol.Put(b, 48, 631467); Protocol.Put(b, 52, 631467); Protocol.Put(b, 56, 315734); return b;
    }
    private static byte[] Report(uint generation, uint state)
    {
        var b = new byte[3616]; Protocol.Put(b, 0, 1); Protocol.Put(b, 4, generation); Protocol.Put(b, 8, state); return b;
    }
    private sealed class FakeDriver : IDriver
    {
        public bool Auth, Busy, RejectStart, BreakScanRead;
        public uint Generation = 10;
        public int Starts, Connects, Cancels;
        public byte[]? Request;
        public byte[] Call(uint code, byte[]? input = null)
        {
            if (code == 0x126004) return Status(Auth ? 600u : Busy ? 421u : 500u, 0, Auth);
            if (code == 0x12A000) { Request = input; Auth = true; ++Connects; return []; }
            if (code == 0x126018)
            {
                if (BreakScanRead && Starts > 0) throw new IOException("Synthetic read failure");
                return Report(Generation, Starts > 0 ? 3u : 0u);
            }
            if (code == 0x12A014) { if (RejectStart) throw new IOException("Synthetic busy"); ++Generation; ++Starts; return []; }
            if (code == 0x12A01C) { Check(input != null && Protocol.U32(input, 0) == Generation, "wrong scan cancelled"); ++Cancels; return []; }
            throw new InvalidOperationException("Unexpected mock operation");
        }
    }
    public static int Run()
    {
        try
        {
            using (var icon = Branding.LoadIcon()) Check(icon.Width > 0 && icon.Height > 0, "form icon cannot load");
            using (var iconBytes = Branding.OpenIcon()) Check(Convert.ToHexString(SHA256.HashData(iconBytes)) == "189F68A20CB9A8333E575281A14332C355426F43FBD565302FAF149FBC42D624", "user icon changed");
            Check(Protocol.ValidCountry("BD") && !Protocol.ValidCountry("bd") && !Protocol.ValidCountry("USA"), "country validation");
            Check(Protocol.ValidSsid("a") && !Protocol.ValidSsid("a\nb") && !Protocol.ValidSsid(new string('a', 33)), "SSID validation");
            byte[] pmk = Protocol.Derive("IEEE", "password");
            Check(Convert.ToHexString(pmk).Equals("F42C6FC52DF0EBEF9EBB4B90B38A5F902E83FE1B135A70E23AED762E9710A12E", StringComparison.Ordinal), "WPA2 PBKDF2 vector");
            var request = Protocol.Connect("BD", "IEEE", pmk);
            Check(request.Length == 76 && Protocol.U32(request, 0) == 1 && Protocol.U32(request, 4) == 4 && request[8] == 'B' && request[9] == 'D' && request.AsSpan(44).SequenceEqual(pmk), "connect ABI");
            CryptographicOperations.ZeroMemory(request);
            var scan = Protocol.Scan("BD"); Check(scan.Length == 8 && scan[6] == 0 && scan[7] == 0, "scan ABI");
            Check(LiveState.Parse(Status(421)).Progress.Contains("50%"), "verification progress");
            Check(LiveState.Parse(Status()).Idle && !LiveState.Parse(Status(421)).Idle && !LiveState.Parse(Status(500, 1)).Idle, "readiness gate");
            Throws(() => LiveState.Parse(new byte[32]), "zero status version accepted");
            Throws(() => ScanReport.Parse(new byte[3615]), "bad report length");
            var report = Report(1, 3); Protocol.Put(report, 16, 65); Throws(() => ScanReport.Parse(report), "unbounded count");
            report = Report(1, 3); Protocol.Put(report, 16, 1); Protocol.Put(report, 32, 4);
            "Test"u8.CopyTo(report.AsSpan(36)); report[68] = 2; Protocol.Put(report, 80, 2); Protocol.Put(report, 84, 36);
            Check(ScanReport.Parse(report).Networks[0].Supported, "supported WPA2 network rejected");
            Protocol.Put(report, 80, 34); Check(!ScanReport.Parse(report).Networks[0].Supported, "required PMF admitted");
            Protocol.Put(report, 32, 33); Throws(() => ScanReport.Parse(report), "oversized SSID admitted");
            var fake = new FakeDriver(); Check(Operations.Connect(fake, "BD", "IEEE", pmk, CancellationToken.None, _ => { }), "new join result");
            Check(fake.Connects == 1 && fake.Request != null && fake.Request.All(b => b == 0), "credential request cleanup");
            Check(!Operations.Connect(fake, "BD", "IEEE", pmk, CancellationToken.None, _ => { }), "existing link claimed as new join"); Check(fake.Connects == 1, "existing link was reset");
            using (var held = new ManualResetEventSlim())
            using (var release = new ManualResetEventSlim())
            {
                var owner = Task.Run(() => { using var lease = new OperationLease(); held.Set(); release.Wait(TimeSpan.FromSeconds(10)); });
                try
                {
                    Check(held.Wait(TimeSpan.FromSeconds(5)), "operation owner timed out");
                    bool busy = false; try { using var lease = new OperationLease(); } catch (OperationBusyException) { busy = true; }
                    Check(busy, "concurrent operation was admitted");
                    Check(Startup.TryJoin(new FakeDriver(), () => true, () => throw new Exception("must not load while busy"), _ => throw new Exception("must not decrypt while busy")) == null, "boot competed with manual operation");
                }
                finally { release.Set(); owner.GetAwaiter().GetResult(); }
                using var recovered = new OperationLease(); Check(true, "operation lease released");
            }
            int loads = 0, decrypts = 0;
            SavedNetwork SyntheticProfile() { ++loads; return new(1, "BD", "IEEE", "synthetic"); }
            byte[] bootKey = pmk.ToArray();
            byte[] SyntheticKey(SavedNetwork _) { ++decrypts; return bootKey; }
            fake = new() { Busy = true };
            Check(Startup.TryJoin(fake, () => true, SyntheticProfile, SyntheticKey) == null && loads == 0 && decrypts == 0 && fake.Connects == 0, "boot touched credentials before readiness");
            fake = new();
            Check(Startup.TryJoin(fake, () => false, SyntheticProfile, SyntheticKey) == "Disabled" && loads == 0, "disabled task tried to join");
            fake.Auth = true;
            Check(Startup.TryJoin(fake, () => true, SyntheticProfile, SyntheticKey) == "AlreadyAuthenticated" && loads == 0 && fake.Connects == 0, "boot replaced existing link");
            fake.Auth = false;
            Check(Startup.TryJoin(fake, () => true, SyntheticProfile, SyntheticKey) == "Authenticated" && loads == 1 && decrypts == 1 && fake.Connects == 1 && bootKey.All(b => b == 0), "boot join/key cleanup");
            fake = new() { Busy = true }; Throws(() => Operations.Connect(fake, "BD", "IEEE", pmk, CancellationToken.None, _ => { }), "startup join admitted");
            Check(fake.Connects == 0, "startup caused connect IOCTL");
            fake = new(); Check(Operations.Scan(fake, "BD", CancellationToken.None, _ => { }).Generation == 11 && fake.Starts == 1, "one explicit scan");
            fake = new() { Auth = true }; Throws(() => Operations.Scan(fake, "BD", CancellationToken.None, _ => { }), "connected scan admitted"); Check(fake.Starts == 0, "connected scan sent");
            fake = new() { RejectStart = true }; Throws(() => Operations.Scan(fake, "BD", CancellationToken.None, _ => { }), "rejected scan reported success"); Check(fake.Cancels == 0, "another scan cancelled");
            using var cts = new CancellationTokenSource(); cts.Cancel(); fake = new();
            Throws(() => Operations.Scan(fake, "BD", cts.Token, _ => { }), "cancelled scan admitted"); Check(fake.Starts == 0, "cancelled scan sent");
            fake = new() { BreakScanRead = true }; Throws(() => Operations.Scan(fake, "BD", CancellationToken.None, _ => { }), "failed read success"); Check(fake.Cancels == 0, "unknown generation guessed");
            string exe = @"C:\ProgramData\Example & Test\RPi5-WiFi-Connector.exe", xml = Startup.TaskXml(exe);
            Check(Startup.OwnedTask(xml, exe) && !Startup.OwnedTask(xml, @"C:\wrong.exe"), "task ownership gate");
            XNamespace ns = "http://schemas.microsoft.com/windows/2004/02/mit/task";
            var task = XElement.Parse(xml);
            Check(task.Descendants(ns + "BootTrigger").Single().Element(ns + "Delay")?.Value == "PT0S", "boot delay introduced");
            Check(task.Descendants(ns + "RunOnlyIfNetworkAvailable").Single().Value == "false", "network-ready circular dependency");
            Check(!xml.Contains("password", StringComparison.OrdinalIgnoreCase) && !xml.Contains("IEEE"), "credential in scheduled task");
            // Exercise Windows ownership/ACL APIs only on synthetic CI files.
            string aclPath = Path.Combine(Environment.CurrentDirectory, "ci-logs", "acl-" + Guid.NewGuid().ToString("N"));
            var privateDir = new DirectoryInfo(aclPath); privateDir.Create(Startup.PrivateAcl());
            try
            {
                Startup.AssertPrivate(privateDir);
                string file = Path.Combine(aclPath, "synthetic.txt"); File.WriteAllText(file, "no credentials"); Startup.SealFile(file);
                Startup.AssertPrivate(new FileInfo(file)); Check(true, "private profile ownership and ACL");
                var acl = new FileInfo(file).GetAccessControl();
                acl.AddAccessRule(new FileSystemAccessRule(new SecurityIdentifier("S-1-1-0"), FileSystemRights.Read, AccessControlType.Allow));
                new FileInfo(file).SetAccessControl(acl);
                Throws(() => Startup.AssertPrivate(new FileInfo(file)), "public-readable profile accepted");
                Startup.SealFile(file);
            }
            finally { privateDir.Delete(true); }
            // Schema validation ONLY: flag 1 does not register a task or run it.
            dynamic service = Activator.CreateInstance(Type.GetTypeFromProgID("Schedule.Service", true)!)!;
            service.Connect(); dynamic root = service.GetFolder(@"\"); root.RegisterTask("RPi5-Connector-ValidateOnly", xml, 1, "SYSTEM", null, 5, null);
            byte[] protectedKey = ProtectedData.Protect(pmk, null, DataProtectionScope.LocalMachine);
            byte[] restored = ProtectedData.Unprotect(protectedKey, null, DataProtectionScope.LocalMachine);
            Check(restored.SequenceEqual(pmk), "DPAPI roundtrip"); CryptographicOperations.ZeroMemory(restored); CryptographicOperations.ZeroMemory(pmk);
            Console.WriteLine($"PASS: {assertions} C# connector checks; no Pi device, task installation or saved user profile used."); return 0;
        }
        catch (Exception ex) { Console.Error.WriteLine($"FAIL: {ex.GetType().Name}: {ex.Message}"); return 1; }
    }
}
