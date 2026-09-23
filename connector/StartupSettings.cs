namespace Rpi5Wifi;

// Injectable only to exercise the real form on a GitHub runner without user
// profiles or a Pi. Production always uses the existing protected storage.
internal interface IStartupSettings
{
    SavedNetwork? Load();
    byte[] Unprotect(SavedNetwork profile);
    bool Enabled();
    void Enable(string country, string ssid, byte[] key);
    void Disable(bool forget);
}
internal sealed class StartupSettings : IStartupSettings
{
    public SavedNetwork? Load() => Startup.Load();
    public byte[] Unprotect(SavedNetwork profile) => Startup.Unprotect(profile);
    public bool Enabled() => Startup.Enabled();
    public void Enable(string country, string ssid, byte[] key) => Startup.Enable(country, ssid, key);
    public void Disable(bool forget) => Startup.Disable(forget);
}
