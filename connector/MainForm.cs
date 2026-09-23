using System.Diagnostics;
using System.Security.Cryptography;

namespace Rpi5Wifi;

internal sealed class MainForm : Form
{
    private readonly IDriver driver;
    private readonly bool preview;
    private readonly TextBox country = new() { MaxLength = 2, CharacterCasing = CharacterCasing.Upper, Width = 56 };
    private readonly CheckBox confirm = new() { Text = "I confirm the Pi is physically in this country", AutoSize = true };
    private readonly TextBox ssid = new() { MaxLength = 32, Dock = DockStyle.Fill };
    private readonly TextBox password = new() { MaxLength = 63, UseSystemPasswordChar = true, Dock = DockStyle.Fill };
    private readonly Button scan = new() { Text = "Scan networks", AutoSize = true };
    private readonly Button connect = new() { Text = "Connect", AutoSize = true };
    private readonly Button disconnect = new() { Text = "Disconnect", AutoSize = true };
    private readonly Button save = new() { Text = "Save & enable at Windows startup", AutoSize = true };
    private readonly Button disable = new() { Text = "Disable startup", AutoSize = true };
    private readonly Button forget = new() { Text = "Forget saved network", AutoSize = true };
    private readonly Label status = new() { Dock = DockStyle.Fill, AutoSize = false, Padding = new(10), BorderStyle = BorderStyle.FixedSingle };
    private readonly ProgressBar progress = new() { Dock = DockStyle.Fill };
    private readonly ListView networks = new() { Dock = DockStyle.Fill, View = View.Details, FullRowSelect = true, MultiSelect = false, HideSelection = false };
    private readonly System.Windows.Forms.Timer timer = new() { Interval = 500 };
    private readonly CancellationTokenSource closing = new();
    private LiveState? state;
    private bool busy, closePending;
    private string message = "", startupText = "Startup connection is not enabled.";
    private byte[]? sessionKey;
    private string keySsid = "", keyCountry = "", lastProgressKey = "";
    private readonly Stopwatch observation = Stopwatch.StartNew();
    private long lastProgressMs;
    public MainForm(IDriver device, bool offline = false)
    {
        driver = device; preview = offline;
        Text = "RPi5 Wi-Fi Connector • 0.6.29"; Font = new("Segoe UI", 10);
        AutoScaleMode = AutoScaleMode.Dpi; ClientSize = new(920, 650); MinimumSize = new(900, 670); StartPosition = FormStartPosition.CenterScreen;
        var layout = new TableLayoutPanel { Dock = DockStyle.Fill, Padding = new(16), ColumnCount = 1, RowCount = 9 };
        foreach (var style in new[] { new RowStyle(SizeType.Absolute, 38), new RowStyle(SizeType.Absolute, 42), new RowStyle(SizeType.Percent, 100),
            new RowStyle(SizeType.Absolute, 38), new RowStyle(SizeType.Absolute, 82), new RowStyle(SizeType.Absolute, 42),
            new RowStyle(SizeType.Absolute, 24), new RowStyle(SizeType.Absolute, 100), new RowStyle(SizeType.Absolute, 42) }) layout.RowStyles.Add(style);
        layout.Controls.Add(new Label { Text = "Connect to nearby Wi-Fi • WPA2-Personal / AES • Ethernet-style driver", Dock = DockStyle.Fill, AutoSize = false }, 0, 0);
        var location = new FlowLayoutPanel { Dock = DockStyle.Fill, WrapContents = false };
        location.Controls.Add(new Label { Text = "Country:", AutoSize = true, Padding = new(0, 5, 0, 0) });
        location.Controls.Add(country); location.Controls.Add(confirm); location.Controls.Add(scan); layout.Controls.Add(location, 0, 1);
        foreach (var col in new[] { ("Network (SSID)", 225), ("Signal", 75), ("Band", 75), ("Channel", 75), ("Security", 205), ("BSSID", 165) }) networks.Columns.Add(col.Item1, col.Item2);
        layout.Controls.Add(networks, 0, 2);
        layout.Controls.Add(new Label { Text = "Scanning is disconnected-only. Selecting an SSID keeps automatic band selection. Hidden network? Type its name below.", Dock = DockStyle.Fill }, 0, 3);
        var credentials = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 2, RowCount = 2 };
        credentials.ColumnStyles.Add(new(SizeType.Absolute, 145)); credentials.ColumnStyles.Add(new(SizeType.Percent, 100));
        credentials.RowStyles.Add(new(SizeType.Percent, 50)); credentials.RowStyles.Add(new(SizeType.Percent, 50));
        credentials.Controls.Add(new Label { Text = "Network name:", AutoSize = true }, 0, 0); credentials.Controls.Add(ssid, 1, 0);
        credentials.Controls.Add(new Label { Text = "WPA2 password:", AutoSize = true }, 0, 1); credentials.Controls.Add(password, 1, 1); layout.Controls.Add(credentials, 0, 4);
        var actions = new FlowLayoutPanel { Dock = DockStyle.Fill, WrapContents = false };
        actions.Controls.AddRange([connect, disconnect, save, disable, forget]); layout.Controls.Add(actions, 0, 5);
        layout.Controls.Add(progress, 0, 6); layout.Controls.Add(status, 0, 7);
        layout.Controls.Add(new Label { Text = "Saved keys are encrypted on this PC and restricted to Administrators/SYSTEM. Closing the app does not disconnect Wi-Fi.", Dock = DockStyle.Fill }, 0, 8);
        Controls.Add(layout);
        if (preview)
        {
            country.Text = "BD"; confirm.Checked = true; ssid.Text = "Example network";
            RenderNetworks(new(1, 3, 0, 0, false, [new("Example network", "Example network", -48, 36, "5 GHz", "WPA2-Personal / AES", "02:00:00:00:00:01", true)]));
            progress.Value = 68; status.Text = "Offline preview — synthetic data, no hardware access.\nVerifying firmware: 68%. Startup connection waits for readiness, not an arbitrary delay.";
            foreach (Control control in actions.Controls) control.Enabled = false; scan.Enabled = false; return;
        }
        try
        {
            var saved = Startup.Load(); if (saved != null) { country.Text = saved.Country; ssid.Text = saved.Ssid; }
            RefreshStartupText();
        }
        catch { startupText = "Saved profile/startup status unavailable. No startup changes were made."; }
        country.TextChanged += (_, _) => { confirm.Checked = false; Buttons(); };
        confirm.CheckedChanged += (_, _) => Buttons(); ssid.TextChanged += (_, _) => Buttons(); password.TextChanged += (_, _) => Buttons();
        networks.SelectedIndexChanged += (_, _) =>
        {
            if (networks.SelectedItems.Count != 1) return;
            var entry = (NetworkEntry)networks.SelectedItems[0].Tag!; password.Clear(); ssid.Clear();
            if (entry.Supported) ssid.Text = entry.Ssid; else message = "Unsupported/hidden entry. Enter a known WPA2/AES hidden network manually if needed.";
            Poll();
        };
        scan.Click += async (_, _) => { string chosen = country.Text; await Work(() => RenderOnUi(Operations.Scan(driver, chosen, closing.Token, Tell))); };
        connect.Click += async (_, _) =>
        {
            if (!Confirmed()) return;
            string chosenCountry = country.Text, chosenSsid = ssid.Text;
            byte[]? key = null;
            try { key = GetKey(); }
            catch (Exception ex) { message = ex.Message; Poll(); return; }
            password.Clear();
            byte[] pendingKey = key;
            bool joined = false;
            try
            {
                await Work(() =>
                {
                    var watch = Stopwatch.StartNew();
                    while (watch.Elapsed < TimeSpan.FromMinutes(30))
                    {
                        closing.Token.ThrowIfCancellationRequested(); var current = LiveState.Parse(driver.Call(0x126004)); Tell(current.Progress);
                        if (current.Status != 0) throw new InvalidOperationException(current.Progress);
                        if (current.Authenticated) { Tell("Already connected. Disconnect explicitly before selecting another network."); return; }
                        if (current.Idle) { joined = Operations.Connect(driver, chosenCountry, chosenSsid, pendingKey, closing.Token, Tell); return; }
                        if (closing.Token.WaitHandle.WaitOne(250)) closing.Token.ThrowIfCancellationRequested();
                    }
                    throw new TimeoutException("Driver startup observation timed out. Run diagnostics before rebooting.");
                });
                if (joined && !IsDisposed && !closePending && state?.Authenticated == true) { ClearKey(); sessionKey = pendingKey.ToArray(); keySsid = chosenSsid; keyCountry = chosenCountry; }
            }
            finally { CryptographicOperations.ZeroMemory(pendingKey); if (!IsDisposed) Buttons(); }
        };
        disconnect.Click += async (_, _) => await Work(() => Operations.Disconnect(driver));
        save.Click += (_, _) =>
        {
            if (!Confirmed()) return;
            if (MessageBox.Show(this, "Save this network and enable connection before sign-in? A protected copy of this EXE and an encrypted Wi-Fi key will be stored on this Pi. The startup task uses SYSTEM. You can disable or forget it here.", "Enable startup connection", MessageBoxButtons.YesNo, MessageBoxIcon.Question) != DialogResult.Yes) return;
            byte[]? key = null;
            try { key = GetKey(); Startup.Enable(country.Text, ssid.Text, key); message = "Saved. Connection will start automatically when the driver is ready after reboot."; RefreshStartupText(); }
            catch (Exception ex) { message = ex.Message; }
            finally { if (key != null) CryptographicOperations.ZeroMemory(key); password.Clear(); Poll(); }
        };
        disable.Click += (_, _) => SetDisabled(false);
        forget.Click += (_, _) =>
        {
            if (MessageBox.Show(this, "Disable automatic startup and delete the saved Wi-Fi key? The current connection will remain connected.", "Forget network", MessageBoxButtons.YesNo) == DialogResult.Yes) SetDisabled(true);
        };
        timer.Tick += (_, _) => Poll(); Shown += (_, _) => { Poll(); timer.Start(); };
        FormClosing += (_, e) =>
        {
            timer.Stop(); closing.Cancel(); password.Clear(); ClearKey();
            if (busy) { e.Cancel = true; closePending = true; message = "Cancelling the app's pending operation..."; }
        };
    }
    private bool Confirmed() => confirm.Checked && Protocol.ValidCountry(country.Text);
    private void RefreshStartupText() => startupText = Startup.Enabled() ? "Automatic connection is enabled at Windows startup." : "Automatic connection is disabled.";
    private void SetDisabled(bool remove)
    {
        try { Startup.Disable(remove); if (remove) ClearKey(); RefreshStartupText(); message = remove ? "Saved key removed; startup disabled. Current connection unchanged." : "Startup disabled. Current connection unchanged."; }
        catch (Exception ex) { message = ex.Message; }
        Poll();
    }
    private byte[] GetKey()
    {
        if (!Protocol.ValidSsid(ssid.Text)) throw new ArgumentException("Enter the exact Wi-Fi network name.");
        if (password.Text.Length != 0) return Protocol.Derive(ssid.Text, password.Text);
        if (sessionKey != null && keySsid == ssid.Text && keyCountry == country.Text) return sessionKey.ToArray();
        var saved = Startup.Load();
        if (saved != null && saved.Ssid == ssid.Text && saved.Country == country.Text) return Startup.Unprotect(saved);
        throw new ArgumentException("Enter the WPA2 password. There is no matching saved network.");
    }
    private void ClearKey() { if (sessionKey != null) CryptographicOperations.ZeroMemory(sessionKey); sessionKey = null; keySsid = keyCountry = ""; }
    private void Buttons()
    {
        bool free = !busy, valid = Confirmed() && Protocol.ValidSsid(ssid.Text);
        scan.Enabled = free && Confirmed() && state?.Idle == true;
        connect.Enabled = free && valid && state != null && state.Status == 0 && !state.Authenticated;
        disconnect.Enabled = free && state != null && state.Phase >= 500 && !state.Idle;
        save.Enabled = free && valid && state != null; disable.Enabled = forget.Enabled = free;
        country.Enabled = confirm.Enabled = ssid.Enabled = password.Enabled = networks.Enabled = free;
    }
    private void Poll()
    {
        if (preview || closePending) return;
        try
        {
            state = LiveState.Parse(driver.Call(0x126004));
            string key = $"{state.Phase}/{state.Total}/{state.Uploaded}/{state.Verified}";
            if (key != lastProgressKey) { lastProgressKey = key; lastProgressMs = observation.ElapsedMilliseconds; }
            string stall = state.Phase < 500 && observation.ElapsedMilliseconds - lastProgressMs >= 120000 ? " No byte/phase progress for two minutes; collect diagnostics. This is not proof of a hardware hang." : "";
            status.Text = state.Progress + stall + Environment.NewLine + startupText + Environment.NewLine + message;
            uint bytes = state.Phase == 421 ? state.Verified : state.Uploaded;
            progress.Value = state.Total == 0 ? 0 : (int)Math.Min(100UL, (ulong)bytes * 100 / state.Total);
        }
        catch { state = null; status.Text = "Waiting for the installed Pi Wi-Fi driver. No scan/reset was requested.\n" + startupText + "\n" + message; }
        Buttons();
    }
    private void Tell(string value)
    {
        if (!IsDisposed && IsHandleCreated) BeginInvoke(() => { if (!IsDisposed) { message = value; Poll(); } });
    }
    private void RenderOnUi(ScanReport report) => BeginInvoke(() => { if (!IsDisposed) RenderNetworks(report); });
    private void RenderNetworks(ScanReport report)
    {
        networks.Items.Clear();
        foreach (var entry in report.Networks.OrderByDescending(n => n.Rssi))
        {
            var item = new ListViewItem([entry.Display, entry.Rssi is < 0 and >= -127 ? $"{entry.Rssi} dBm" : "Unknown", entry.Band, entry.Channel.ToString(), entry.Security, entry.Bssid]) { Tag = entry };
            if (!entry.Supported) item.ForeColor = Color.DimGray; networks.Items.Add(item);
        }
        message = $"Found {report.Networks.Count} network entries." + (report.Truncated ? " Result limit reached." : "");
    }
    private async Task Work(Action action)
    {
        if (busy) return; busy = true; Buttons();
        try { await Task.Run(action); }
        catch (OperationCanceledException) { message = "App operation cancelled. An established connection is not disconnected."; }
        catch (Exception ex) { message = ex.Message; }
        finally { busy = false; Poll(); if (closePending) Close(); }
    }
    protected override void Dispose(bool disposing)
    {
        if (disposing) { timer.Dispose(); closing.Dispose(); ClearKey(); }
        base.Dispose(disposing);
    }
}
