using System.Runtime.InteropServices;
using System.Security.AccessControl;
using System.Security.Cryptography;
using System.Security.Principal;
using System.Text.Json;
using System.Xml.Linq;

namespace Rpi5Wifi;

internal sealed record SavedNetwork(int Version, string Country, string Ssid, string ProtectedKey);
internal static class Startup
{
    public const string TaskName = "RPi5 WiFi Connector";
    public static readonly string DirectoryPath = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.CommonApplicationData), "RPi5WiFiConnector");
    public static string ExePath => Path.Combine(DirectoryPath, "RPi5-WiFi-Connector.exe");
    private static string ProfilePath => Path.Combine(DirectoryPath, "profile.json");
    private static readonly byte[] Entropy = System.Text.Encoding.UTF8.GetBytes("RPi5.WiFi.Connector.Profile.v1");
    public static void ValidatePath(string path)
    {
        for (string? current = Path.GetFullPath(path); current != null; current = Path.GetDirectoryName(current))
            if ((File.Exists(current) || Directory.Exists(current)) && (File.GetAttributes(current) & FileAttributes.ReparsePoint) != 0)
                throw new IOException("Refusing a redirected startup path.");
    }
    internal static DirectorySecurity PrivateAcl()
    {
        var acl = new DirectorySecurity(); acl.SetAccessRuleProtection(true, false);
        acl.SetOwner(new SecurityIdentifier("S-1-5-32-544"));
        foreach (string sid in new[] { "S-1-5-18", "S-1-5-32-544" })
            acl.AddAccessRule(new FileSystemAccessRule(new SecurityIdentifier(sid), FileSystemRights.FullControl,
                InheritanceFlags.ContainerInherit | InheritanceFlags.ObjectInherit, PropagationFlags.None, AccessControlType.Allow));
        return acl;
    }
    internal static void AssertPrivate(FileSystemInfo info)
    {
        ValidatePath(info.FullName);
        FileSystemSecurity acl = info is DirectoryInfo directory ? directory.GetAccessControl() : ((FileInfo)info).GetAccessControl();
        if (acl.GetOwner(typeof(SecurityIdentifier))?.Value is not ("S-1-5-18" or "S-1-5-32-544")) throw new IOException("Untrusted startup path owner.");
        foreach (FileSystemAccessRule rule in acl.GetAccessRules(true, true, typeof(SecurityIdentifier)))
            if (rule.AccessControlType == AccessControlType.Allow && rule.IdentityReference.Value is not ("S-1-5-18" or "S-1-5-32-544"))
                throw new IOException("Startup files must be accessible only to SYSTEM and Administrators.");
    }
    internal static void SealFile(string path)
    {
        ValidatePath(path);
        var acl = new FileSecurity(); acl.SetAccessRuleProtection(true, false);
        acl.SetOwner(new SecurityIdentifier("S-1-5-32-544"));
        foreach (string sid in new[] { "S-1-5-18", "S-1-5-32-544" })
            acl.AddAccessRule(new FileSystemAccessRule(new SecurityIdentifier(sid), FileSystemRights.FullControl, AccessControlType.Allow));
        var file = new FileInfo(path); file.SetAccessControl(acl); AssertPrivate(file);
    }
    private static void EnsureDirectory()
    {
        ValidatePath(DirectoryPath);
        var directory = new DirectoryInfo(DirectoryPath);
        if (directory.Exists) AssertPrivate(directory); else directory.Create(PrivateAcl());
        // Create can observe an existing directory if another creator won a race.
        AssertPrivate(directory);
        foreach (var item in directory.EnumerateFileSystemInfos())
        {
            if (item is not FileInfo || item.Name is not ("RPi5-WiFi-Connector.exe" or "profile.json" or "boot-status.json"))
                throw new IOException("Unexpected file in the protected connector folder.");
            AssertPrivate(item);
        }
    }
    public static SavedNetwork? Load()
    {
        if (!File.Exists(ProfilePath)) return null;
        AssertPrivate(new DirectoryInfo(DirectoryPath)); AssertPrivate(new FileInfo(ProfilePath));
        if (new FileInfo(ProfilePath).Length > 8192) throw new InvalidDataException("Invalid saved profile size.");
        var profile = JsonSerializer.Deserialize<SavedNetwork>(File.ReadAllText(ProfilePath));
        if (profile == null || profile.Version != 1 || !Protocol.ValidCountry(profile.Country) || !Protocol.ValidSsid(profile.Ssid) || profile.ProtectedKey.Length > 4096)
            throw new InvalidDataException("Invalid saved profile.");
        return profile;
    }
    public static byte[] Unprotect(SavedNetwork profile)
    {
        byte[] result = ProtectedData.Unprotect(Convert.FromBase64String(profile.ProtectedKey), Entropy, DataProtectionScope.LocalMachine);
        if (result.Length != 32) { CryptographicOperations.ZeroMemory(result); throw new CryptographicException("Invalid saved Wi-Fi key."); }
        return result;
    }
    internal static string TaskXml(string exe)
    {
        XNamespace ns = "http://schemas.microsoft.com/windows/2004/02/mit/task";
        XElement E(string name, object? content) => new(ns + name, content);
        return new XElement(ns + "Task", new XAttribute("version", "1.4"),
            E("RegistrationInfo", E("Description", "RPi5 WiFi Connector v1 — one protected saved network; no artificial boot delay.")),
            E("Triggers", new XElement(ns + "BootTrigger", E("Enabled", "true"), E("Delay", "PT0S"))),
            E("Principals", new XElement(ns + "Principal", new XAttribute("id", "System"), E("UserId", "S-1-5-18"), E("RunLevel", "HighestAvailable"))),
            E("Settings", new[] { E("MultipleInstancesPolicy", "IgnoreNew"), E("DisallowStartIfOnBatteries", "false"), E("StopIfGoingOnBatteries", "false"),
                E("StartWhenAvailable", "true"), E("RunOnlyIfNetworkAvailable", "false"), E("Enabled", "true"), E("ExecutionTimeLimit", "PT35M") }),
            new XElement(ns + "Actions", new XAttribute("Context", "System"), new XElement(ns + "Exec", E("Command", exe), E("Arguments", "--autoconnect"), E("WorkingDirectory", Path.GetDirectoryName(exe)))))
            .ToString(SaveOptions.DisableFormatting);
    }
    private static dynamic Scheduler()
    {
        dynamic service = Activator.CreateInstance(Type.GetTypeFromProgID("Schedule.Service", true)!)!; service.Connect(); return service;
    }
    internal static bool OwnedTask(string xml, string exe)
    {
        var root = XElement.Parse(xml); XNamespace ns = root.Name.Namespace;
        var actions = root.Element(ns + "Actions")?.Elements().ToArray();
        return actions?.Length == 1 && actions[0].Name == ns + "Exec" &&
            string.Equals((string?)actions[0].Element(ns + "Command"), exe, StringComparison.OrdinalIgnoreCase) &&
            (string?)actions[0].Element(ns + "Arguments") == "--autoconnect" &&
            (string?)root.Element(ns + "Principals")?.Element(ns + "Principal")?.Element(ns + "UserId") is "S-1-5-18" or "SYSTEM";
    }
    private static dynamic? FindTask(dynamic root, string name)
    {
        try { return root.GetTask(name); }
        catch (COMException ex) when ((uint)ex.HResult is 0x80070002 or 0x8004130F) { return null; }
    }
    public static void Enable(string country, string ssid, byte[] pmk)
    {
        if (!Protocol.ValidCountry(country) || !Protocol.ValidSsid(ssid) || pmk.Length != 32) throw new ArgumentException("Invalid profile.");
        using var lease = new OperationLease();
        dynamic service = Scheduler(); dynamic root = service.GetFolder(@"\");
        // Never silently replace the existing PowerShell startup mechanism.
        dynamic? legacy = FindTask(root, "RPi5WiFi-AutoConnect");
        if (legacy is not null && (bool)legacy.Enabled) throw new InvalidOperationException("Disable the old autoconnect task with its original utility before enabling this connector.");
        dynamic? current = FindTask(root, TaskName);
        if (current is not null && !OwnedTask((string)current.Xml, ExePath)) throw new IOException("A different task already uses this name.");
        if (current is not null && (int)current.State == 4) throw new IOException("Startup connection is still running. Wait before changing its files.");
        EnsureDirectory();
        string source = Environment.ProcessPath ?? throw new IOException("Executable location unavailable.");
        if (!string.Equals(Path.GetFullPath(source), ExePath, StringComparison.OrdinalIgnoreCase))
        {
            File.Copy(source, ExePath, true); SealFile(ExePath);
        }
        var profile = new SavedNetwork(1, country, ssid, Convert.ToBase64String(ProtectedData.Protect(pmk, Entropy, DataProtectionScope.LocalMachine)));
        File.WriteAllText(ProfilePath, JsonSerializer.Serialize(profile)); SealFile(ProfilePath);
        root.RegisterTask(TaskName, TaskXml(ExePath), 6, "SYSTEM", null, 5, "D:P(A;;FA;;;SY)(A;;FA;;;BA)");
    }
    public static void Disable(bool forget)
    {
        using var lease = new OperationLease();
        dynamic service = Scheduler(); dynamic root = service.GetFolder(@"\"); dynamic? task = FindTask(root, TaskName);
        if (task is not null)
        {
            if (!OwnedTask((string)task.Xml, ExePath)) throw new IOException("Refusing to modify a different startup task.");
            task.Enabled = false;
            // Existing operation lock means the startup runner is not joining.
            // Its next readiness poll also checks Enabled and exits without a join.
        }
        if (forget && File.Exists(ProfilePath)) { AssertPrivate(new FileInfo(ProfilePath)); File.Delete(ProfilePath); }
    }
    public static bool Enabled()
    {
        dynamic service = Scheduler(); dynamic root = service.GetFolder(@"\"); dynamic? task = FindTask(root, TaskName);
        return task is not null && (bool)task.Enabled && OwnedTask((string)task.Xml, ExePath);
    }
    private static void Receipt(string result)
    {
        EnsureDirectory();
        string path = Path.Combine(DirectoryPath, "boot-status.json");
        File.WriteAllText(path, JsonSerializer.Serialize(new { Version = 1, Utc = DateTime.UtcNow, Result = result })); SealFile(path);
    }
    internal static string? TryJoin(IDriver driver, Func<bool> enabled, Func<SavedNetwork> load, Func<SavedNetwork, byte[]> unprotect)
    {
        try
        {
            using var lease = new OperationLease();
            if (!enabled()) return "Disabled";
            // Recheck under the shared lease: the GUI might have connected
            // between the outer readiness observation and acquiring ownership.
            var current = LiveState.Parse(driver.Call(0x126004));
            if (current.Status != 0) throw new InvalidOperationException("Driver reported a startup error.");
            if (current.Authenticated) return "AlreadyAuthenticated";
            if (!current.Idle) return null;
            var profile = load(); byte[] pmk = unprotect(profile);
            try { return Operations.Connect(driver, profile.Country, profile.Ssid, pmk, CancellationToken.None, _ => { }) ? "Authenticated" : "AlreadyAuthenticated"; }
            finally { CryptographicOperations.ZeroMemory(pmk); }
        }
        catch (OperationBusyException) { return null; }
    }
    public static int Run(IDriver driver)
    {
        try
        {
            if (!string.Equals(Environment.ProcessPath, ExePath, StringComparison.OrdinalIgnoreCase)) return 2;
            EnsureDirectory();
            var watch = System.Diagnostics.Stopwatch.StartNew(); long nextTaskCheck = 0; bool enabled = false;
            while (watch.Elapsed < TimeSpan.FromMinutes(30))
            {
                if (watch.ElapsedMilliseconds >= nextTaskCheck) { enabled = Enabled(); nextTaskCheck = watch.ElapsedMilliseconds + 1000; }
                if (!enabled) { Receipt("Disabled"); return 0; }
                LiveState? state = null;
                try { state = LiveState.Parse(driver.Call(0x126004)); }
                catch (System.ComponentModel.Win32Exception ex) when (ex.NativeErrorCode is 2 or 3 or 21 or 1167) { }
                if (state?.Authenticated == true && state.Status == 0) { Receipt("AlreadyAuthenticated"); return 0; }
                if (state?.Status != null && state.Status != 0) throw new InvalidOperationException("Driver reported a startup error.");
                if (state?.Idle == true)
                {
                    // Acquire the shared operation lock only at readiness, not
                    // throughout firmware startup. Recheck task state under it.
                    string? outcome = TryJoin(driver, Enabled,
                        () => Load() ?? throw new InvalidOperationException("Saved profile is unavailable."), Unprotect);
                    if (outcome != null) { Receipt(outcome); return 0; }
                }
                Thread.Sleep(250); // Poll memory, no extra boot delay or radio request.
            }
            Receipt("DriverReadinessTimeout"); return 3;
        }
        catch (Exception ex)
        {
            // No profile, SSID, PMK, password or exception text is logged.
            try { Receipt("Failed:" + ex.GetType().Name); } catch { }
            return 1;
        }
    }
}
