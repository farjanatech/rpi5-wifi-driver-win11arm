using System.Buffers.Binary;
using System.ComponentModel;
using System.Globalization;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text;
using Microsoft.Win32.SafeHandles;

[assembly: DefaultDllImportSearchPaths(DllImportSearchPath.System32)]
namespace Rpi5Wifi;

internal record LiveState(uint Phase, uint Status, bool Authenticated, uint Total, uint Uploaded, uint Verified)
{
    public bool Idle => Phase == 500 && Status == 0 && !Authenticated;
    public string Progress => Status != 0 ? $"Driver error 0x{Status:X8} at phase {Phase}. Run diagnostics." :
        Authenticated ? "Wi-Fi authenticated. IP/Internet connectivity is checked separately." : Phase switch
        {
            400 => "Preparing firmware files...",
            410 => "Preparing Wi-Fi chip and validating startup bus...",
            420 => $"Loading firmware: {Percent(Uploaded)}% ({Uploaded:N0}/{Total:N0} bytes).",
            421 => $"Verifying firmware: {Percent(Verified)}% ({Verified:N0}/{Total:N0} bytes).",
            422 or 430 or 440 or 450 => "Starting firmware and validating the operating bus...",
            500 => "Disconnected and ready. Scan networks or connect to your saved network.",
            510 or 520 => "Connecting and authenticating...",
            _ => $"Waiting for Wi-Fi driver (phase {Phase})..."
        };
    private uint Percent(uint n) => Total == 0 ? 0 : (uint)Math.Min(100UL, (ulong)n * 100 / Total);
    public static LiveState Parse(byte[] data)
    {
        if (data.Length < 32) throw new InvalidDataException("Incomplete driver status.");
        uint version = Protocol.U32(data, 0);
        if (version < 1 || version > 3 || (version == 2 && data.Length < 48) || (version == 3 && data.Length < 96))
            throw new InvalidDataException("Unsupported driver status.");
        return new(Protocol.U32(data, 4), Protocol.U32(data, 8), Protocol.U32(data, 28) == 1,
            version == 3 ? Protocol.U32(data, 48) : 0, version == 3 ? Protocol.U32(data, 52) : 0,
            version == 3 ? Protocol.U32(data, 56) : 0);
    }
}
internal record NetworkEntry(string Ssid, string Display, int Rssi, uint Channel, string Band, string Security, string Bssid, bool Supported);
internal record ScanReport(uint Generation, uint State, uint Status, int FirmwareError, bool Truncated, List<NetworkEntry> Networks)
{
    public static ScanReport Parse(byte[] data)
    {
        if (data.Length != 3616 || Protocol.U32(data, 0) != 1) throw new InvalidDataException("Scan requires driver .28 or newer.");
        uint generation = Protocol.U32(data, 4), state = Protocol.U32(data, 8), count = Protocol.U32(data, 16), flags = Protocol.U32(data, 20);
        uint country = Protocol.U32(data, 24);
        if (state > 5 || count > 64 || flags > 1 || (state != 0 && generation == 0) || country > 65535 ||
            (country != 0 && (!Protocol.Letter((byte)country) || !Protocol.Letter((byte)(country >> 8)))))
            throw new InvalidDataException("Invalid scan report.");
        List<NetworkEntry> entries = [];
        for (int i = 0; i < count; ++i)
        {
            int offset = 32 + i * 56;
            uint length = Protocol.U32(data, offset), security = Protocol.U32(data, offset + 48), channel = Protocol.U32(data, offset + 52);
            if (length > 32) throw new InvalidDataException("Invalid SSID length.");
            string ssid = "", display = "<Hidden network — type its name below>";
            if (length != 0)
            {
                try { ssid = Protocol.Utf8.GetString(data, offset + 4, (int)length); }
                catch (DecoderFallbackException) { }
                display = Protocol.ValidSsid(ssid) ? ssid : "<Non-displayable SSID>";
            }
            var mac = data.AsSpan(offset + 36, 6);
            if ((mac[0] & 1) != 0 || mac.SequenceEqual(new byte[6])) throw new InvalidDataException("Invalid BSSID.");
            string band = channel is >= 1 and <= 14 ? "2.4 GHz" : channel is >= 32 and <= 196 ? "5 GHz" : "Unknown";
            string securityText = security == 2 ? "WPA2-Personal / AES" : (security & 1) != 0 ? "Open (unsupported)" : "Unsupported security";
            entries.Add(new(ssid, display, BinaryPrimitives.ReadInt32LittleEndian(data.AsSpan(offset + 44)), channel, band,
                securityText, BitConverter.ToString(mac.ToArray()).Replace('-', ':'), security == 2 && band != "Unknown" && Protocol.ValidSsid(ssid)));
        }
        return new(generation, state, Protocol.U32(data, 12), BinaryPrimitives.ReadInt32LittleEndian(data.AsSpan(28)), flags != 0, entries);
    }
}
internal static class Protocol
{
    public static readonly UTF8Encoding Utf8 = new(false, true);
    public static uint U32(byte[] b, int o) => BinaryPrimitives.ReadUInt32LittleEndian(b.AsSpan(o, 4));
    public static void Put(byte[] b, int o, uint v) => BinaryPrimitives.WriteUInt32LittleEndian(b.AsSpan(o, 4), v);
    public static bool Letter(byte c) => c is >= (byte)'A' and <= (byte)'Z';
    public static bool ValidCountry(string value) => value.Length == 2 && value.All(c => c is >= 'A' and <= 'Z');
    public static bool ValidSsid(string value)
    {
        try { return Utf8.GetByteCount(value) is >= 1 and <= 32 && !value.Any(c => char.GetUnicodeCategory(c) is
            UnicodeCategory.Control or UnicodeCategory.Format or UnicodeCategory.LineSeparator or UnicodeCategory.ParagraphSeparator); }
        catch (EncoderFallbackException) { return false; }
    }
    public static byte[] Derive(string ssid, string password)
    {
        if (!ValidSsid(ssid) || password.Length is < 8 or > 63 || password.Any(c => c is < ' ' or > '~'))
            throw new ArgumentException("Use an exact SSID (1–32 UTF-8 bytes) and WPA2 password (8–63 printable ASCII characters).");
        byte[] bytes = Encoding.ASCII.GetBytes(password);
        try { return Rfc2898DeriveBytes.Pbkdf2(bytes, Utf8.GetBytes(ssid), 4096, HashAlgorithmName.SHA1, 32); }
        finally { CryptographicOperations.ZeroMemory(bytes); }
    }
    public static byte[] Connect(string country, string ssid, byte[] pmk)
    {
        if (!ValidCountry(country) || !ValidSsid(ssid) || pmk.Length != 32) throw new ArgumentException("Invalid connection profile.");
        byte[] b = new byte[76], name = Utf8.GetBytes(ssid);
        Put(b, 0, 1); Put(b, 4, (uint)name.Length); Encoding.ASCII.GetBytes(country).CopyTo(b, 8); name.CopyTo(b, 12); pmk.CopyTo(b, 44);
        return b;
    }
    public static byte[] Scan(string country)
    {
        if (!ValidCountry(country)) throw new ArgumentException("Confirm the country where the Pi is physically located.");
        byte[] b = new byte[8]; Put(b, 0, 1); Encoding.ASCII.GetBytes(country).CopyTo(b, 4); return b;
    }
}
internal interface IDriver { byte[] Call(uint code, byte[]? input = null); }
internal sealed class Driver : IDriver
{
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern SafeFileHandle CreateFile(string name, uint access, uint share, IntPtr security, uint creation, uint flags, IntPtr template);
    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool DeviceIoControl(SafeFileHandle file, uint code, byte[]? input, int inputLength, byte[] output, int outputLength, out int returned, IntPtr overlapped);
    public byte[] Call(uint code, byte[]? input = null)
    {
        int capacity = code == 0x126004 ? 96 : code == 0x126018 ? 3616 :
            code is 0x12A000 or 0x12A008 or 0x12A014 or 0x12A01C ? 0 : throw new ArgumentException("Unsupported control operation.");
        using var handle = CreateFile(@"\\.\Rpi5CywControl", capacity > 0 ? 0x80000000u : 0x40000000u, 3, IntPtr.Zero, 3, 0, IntPtr.Zero);
        if (handle.IsInvalid) throw new Win32Exception(Marshal.GetLastWin32Error());
        byte[] output = new byte[capacity];
        if (!DeviceIoControl(handle, code, input, input?.Length ?? 0, output, capacity, out int count, IntPtr.Zero)) throw new Win32Exception(Marshal.GetLastWin32Error());
        if (count < 0 || count > capacity || (code == 0x126018 && count != capacity)) throw new InvalidDataException("Incomplete driver response.");
        return output[..count];
    }
}
