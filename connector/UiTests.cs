using System.Diagnostics;
using System.Security.Cryptography;

namespace Rpi5Wifi;

internal static class UiTests
{
    private sealed class FakeSettings : IStartupSettings
    {
        public SavedNetwork? Profile;
        public bool Active;
        public int Saves, Disables, Forgets;
        public byte[]? Key;
        public SavedNetwork? Load() => Profile;
        public byte[] Unprotect(SavedNetwork profile) => Key?.ToArray() ?? throw new InvalidOperationException("No synthetic key.");
        public bool Enabled() => Active;
        public void Enable(string country, string ssid, byte[] key)
        {
            if (Key != null) CryptographicOperations.ZeroMemory(Key);
            Key = key.ToArray(); Profile = new(1, country, ssid, "synthetic-not-a-real-key"); Active = true; Saves++;
        }
        public void Disable(bool forget)
        {
            Active = false; Disables++;
            if (forget) { Profile = null; if (Key != null) CryptographicOperations.ZeroMemory(Key); Key = null; Forgets++; }
        }
    }
    private sealed class FakeDriver : IDriver
    {
        public bool Auth;
        public int Scans, Connects, Disconnects;
        public byte[] Call(uint code, byte[]? input = null)
        {
            if (code == 0x126004)
            {
                var state = new byte[32]; Protocol.Put(state, 0, 1); Protocol.Put(state, 4, Auth ? 600u : 500u); Protocol.Put(state, 28, Auth ? 1u : 0u); return state;
            }
            if (code == 0x12A000) { Connects++; Auth = true; return []; }
            if (code == 0x12A008) { Disconnects++; Auth = false; return []; }
            if (code == 0x12A014) { Scans++; return []; }
            if (code == 0x126018)
            {
                var b = new byte[3616]; Protocol.Put(b, 0, 1); Protocol.Put(b, 4, (uint)(Scans + 1)); Protocol.Put(b, 8, Scans == 0 ? 0u : 3u);
                if (Scans == 0) return b;
                Protocol.Put(b, 16, 3);
                for (int i = 0; i < 3; i++)
                {
                    int offset = 32 + i * 56; byte[] name = Protocol.Utf8.GetBytes(i < 2 ? "Synthetic dual band" : "Other network");
                    Protocol.Put(b, offset, (uint)name.Length); name.CopyTo(b, offset + 4);
                    b[offset + 36] = 2; b[offset + 41] = (byte)(i + 1);
                    Protocol.Put(b, offset + 44, unchecked((uint)(-40 - i * 10))); Protocol.Put(b, offset + 48, 2);
                    Protocol.Put(b, offset + 52, i == 1 ? 36u : 6u);
                }
                return b;
            }
            throw new InvalidOperationException("Unexpected GUI hardware request.");
        }
    }
    private static void PumpUntil(Func<bool> complete)
    {
        var watch = Stopwatch.StartNew();
        do { Application.DoEvents(); if (complete()) return; Thread.Sleep(10); } while (watch.Elapsed < TimeSpan.FromSeconds(10));
        throw new TimeoutException("Synthetic UI operation did not finish.");
    }
    public static int Run()
    {
        int checks = 0;
        void Check(bool ok, string why) { checks++; if (!ok) throw new InvalidOperationException(why); }
        var driver = new FakeDriver(); var settings = new FakeSettings();
        using var form = new MainForm(driver, settings: settings, confirmation: (_, _) => true);
        T Find<T>(string name) where T : Control => (T)form.Controls.Find(name, true).Single();
        Button Button(string name) => Find<Button>(name);
        form.Show(); Application.DoEvents();
        Find<TextBox>("country").Text = "BD"; Find<CheckBox>("confirm").Checked = true;
        Check(!Button("disable").Enabled && !Button("forget").Enabled, "absent startup/profile should not offer destructive actions");
        Check(Button("scan").Enabled, "ready disconnected scan disabled"); Button("scan").PerformClick();
        var list = Find<ListView>("networks");
        PumpUntil(() => list.Items.Count == 3 && Button("scan").Enabled);
        Check(driver.Scans == 1, "scan button did not submit one scan");
        list.Items[0].Selected = true; Application.DoEvents();
        var ssid = Find<TextBox>("ssid"); var password = Find<TextBox>("password");
        Check(ssid.Text == "Synthetic dual band", "row did not populate SSID");
        password.Text = "synthetic-password"; password.Focus(); Application.DoEvents();
        Check(list.Items[0].Text == "Selected" && list.Items[1].Text == "Selected" && list.Items[2].Text == "", "SSID markers lost on password focus");
        list.Items[1].Selected = true; Application.DoEvents();
        Check(password.Text == "synthetic-password", "same-SSID band row erased password");
        ssid.Text = "Other network"; Application.DoEvents();
        Check(list.Items[2].Text == "Selected" && list.Items[0].Text == "", "typed SSID did not synchronize markers");
        ssid.Text = "Manual hidden network"; Application.DoEvents();
        Check(list.Items.Cast<ListViewItem>().All(i => i.Text == "") && Find<Label>("selection").Text.Contains("manual/hidden"), "hidden/manual SSID misleadingly marked an AP");
        ssid.Text = "Synthetic dual band"; password.Focus(); Application.DoEvents();
        // Actual form, entered fields and unfocused selection rendered on CI.
        using (var bitmap = new Bitmap(form.Width, form.Height))
        {
            form.DrawToBitmap(bitmap, new Rectangle(0, 0, form.Width, form.Height));
            bitmap.Save(Path.Combine("ci-logs", "connector-selection.png"), System.Drawing.Imaging.ImageFormat.Png);
        }
        Button("connect").PerformClick(); PumpUntil(() => driver.Auth && Button("save").Enabled && !Button("connect").Enabled);
        Check(driver.Connects == 1 && password.Text == "", "connect did not clear password or submitted duplicate join");
        Check(!Button("scan").Enabled && Find<Label>("status").Text.Contains("Disconnect first"), "connected scanning restriction unclear");
        Button("save").PerformClick(); Application.DoEvents();
        Check(settings.Saves == 1 && settings.Active && settings.Profile?.Ssid == ssid.Text && settings.Key?.Length == 32,
            "save handler failed to reuse authenticated session key; saves=" + settings.Saves + "; UI=" + Find<Label>("status").Text);
        Check(Button("disable").Enabled && Button("forget").Enabled, "saved actions not enabled");
        Button("disable").PerformClick(); Application.DoEvents();
        Check(!settings.Active && settings.Profile != null && !Button("disable").Enabled && Button("forget").Enabled && driver.Auth, "disable should retain profile and current link");
        Button("save").PerformClick(); Application.DoEvents();
        Check(settings.Saves == 2 && settings.Active, "startup re-enable failed");
        Button("forget").PerformClick(); Application.DoEvents();
        Check(settings.Forgets == 1 && settings.Profile == null && !settings.Active && driver.Auth && !Button("forget").Enabled, "forget should remove saved key without disconnecting");
        Button("disconnect").PerformClick(); PumpUntil(() => !driver.Auth && Button("scan").Enabled);
        Check(driver.Disconnects == 1 && driver.Scans == 1 && driver.Connects == 1, "disconnect button or background-operation isolation failed");
        form.Close();
        Console.WriteLine($"PASS: {checks} real-form checks for scan, SSID selection, credentials, connect, save, disable, re-enable, forget and disconnect; fake driver/profile only.");
        return checks;
    }
}
