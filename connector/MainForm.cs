using System.Diagnostics;
using System.Security.Cryptography;

namespace Rpi5Wifi;

internal sealed class MainForm : Form
{
    private readonly IDriver driver;
    private readonly bool preview;
    private readonly IStartupSettings startup;
    private readonly Func<string, string, bool> ask;
    private readonly Icon appIcon = Branding.LoadIcon();
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
    private readonly Label selection = new() { Name = "selection", Dock = DockStyle.Fill, AutoSize = false, ForeColor = Color.Navy };
    private readonly ToolTip hints = new();
    private readonly System.Windows.Forms.Timer timer = new() { Interval = 500 };
    private readonly CancellationTokenSource closing = new();
    private LiveState? state;
    private bool busy, closePending, synchronizingSelection;
    private bool? startupEnabled;
    private bool hasSavedProfile;
    private string message = "", startupText = "Startup connection is not enabled.";
    private byte[]? sessionKey;
    private string keySsid = "", keyCountry = "", lastProgressKey = "";
    private readonly Stopwatch observation = Stopwatch.StartNew();
    private long lastProgressMs;
    public MainForm(IDriver device, bool offline = false, IStartupSettings? settings = null, Func<string, string, bool>? confirmation = null)
    {
        driver = device; preview = offline;
        startup = settings ?? new StartupSettings();
        ask = confirmation ?? ((text, title) => MessageBox.Show(this, text, title, MessageBoxButtons.YesNo, MessageBoxIcon.Question) == DialogResult.Yes);
        Text = $"RPi5 Wi-Fi Connector • {Branding.Version}"; Font = new("Segoe UI", 10);
        Icon = appIcon;
        AutoScaleMode = AutoScaleMode.Dpi; ClientSize = new(920, 650); MinimumSize = new(900, 670); StartPosition = FormStartPosition.CenterScreen;
        var layout = new TableLayoutPanel { Dock = DockStyle.Fill, Padding = new(16), ColumnCount = 1, RowCount = 9 };
        foreach (var style in new[] { new RowStyle(SizeType.Absolute, 38), new RowStyle(SizeType.Absolute, 42), new RowStyle(SizeType.Percent, 100),
            new RowStyle(SizeType.Absolute, 38), new RowStyle(SizeType.Absolute, 82), new RowStyle(SizeType.Absolute, 42),
            new RowStyle(SizeType.Absolute, 24), new RowStyle(SizeType.Absolute, 100), new RowStyle(SizeType.Absolute, 42) }) layout.RowStyles.Add(style);
        layout.Controls.Add(new Label { Text = "Connect to nearby Wi-Fi • WPA2-Personal / AES • Ethernet-style driver", Dock = DockStyle.Fill, AutoSize = false }, 0, 0);
        var location = new FlowLayoutPanel { Dock = DockStyle.Fill, WrapContents = false };
        location.Controls.Add(new Label { Text = "Country:", AutoSize = true, Padding = new(0, 5, 0, 0) });
        location.Controls.Add(country); location.Controls.Add(confirm); location.Controls.Add(scan); layout.Controls.Add(location, 0, 1);
        foreach (var col in new[] { ("Use", 65), ("Network (SSID)", 205), ("Signal", 65), ("Band", 75), ("Channel", 65), ("Security", 165), ("BSSID", 165) }) networks.Columns.Add(col.Item1, col.Item2);
        layout.Controls.Add(networks, 0, 2);
        layout.Controls.Add(selection, 0, 3);
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
        // Stable names support tests of the actual UI handlers, not a parallel UI model.
        foreach (var pair in new (Control Control, string Name)[] { (country, "country"), (confirm, "confirm"), (ssid, "ssid"), (password, "password"), (networks, "networks"),
            (scan, "scan"), (connect, "connect"), (disconnect, "disconnect"), (save, "save"), (disable, "disable"), (forget, "forget"), (status, "status") }) pair.Control.Name = pair.Name;
        hints.SetToolTip(scan, "Scanning requires a disconnected, ready adapter. Disconnect first; connected scanning is deliberately disabled.");
        hints.SetToolTip(networks, "Rows marked Selected have the chosen SSID. Both bands may be marked; the driver selects the band when connecting.");
        if (preview)
        {
            country.Text = "BD"; confirm.Checked = true; ssid.Text = "Example network";
            RenderNetworks(new(1, 3, 0, 0, false, [new("Example network", "Example network", -48, 36, "5 GHz", "WPA2-Personal / AES", "02:00:00:00:00:01", true)]));
            progress.Value = 68; status.Text = "Offline preview — synthetic data, no hardware access.\nVerifying firmware: 68%. Startup connection waits for readiness, not an arbitrary delay.";
            foreach (Control control in actions.Controls) control.Enabled = false; scan.Enabled = false; return;
        }
        try
        {
            var saved = startup.Load(); if (saved != null) { country.Text = saved.Country; ssid.Text = saved.Ssid; }
            RefreshStartupText();
        }
        catch (Exception ex) { startupText = "Saved profile/startup status unavailable: " + ex.Message; }
        country.TextChanged += (_, _) => { confirm.Checked = false; Buttons(); };
        confirm.CheckedChanged += (_, _) => Buttons(); ssid.TextChanged += (_, _) => { MarkSelection(); Buttons(); }; password.TextChanged += (_, _) => Buttons();
        networks.SelectedIndexChanged += (_, _) =>
        {
            if (synchronizingSelection || networks.SelectedItems.Count != 1) return;
            var entry = (NetworkEntry)networks.SelectedItems[0].Tag!;
            if (entry.Supported)
            {
                if (!string.Equals(ssid.Text, entry.Ssid, StringComparison.Ordinal)) { password.Clear(); ssid.Text = entry.Ssid; }
                message = "Selected SSID: " + entry.Ssid + ". Band selection remains automatic; this does not force a listed access point.";
            }
            else message = "Unsupported/hidden row was not selected for connection. Type an exact known WPA2/AES SSID below if needed.";
            MarkSelection();
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
                }, () =>
                {
                    // Retain the key before Work releases the buttons. A fast
                    // Save click must not race the async connect continuation.
                    if (joined && !IsDisposed && !closePending) { ClearKey(); sessionKey = pendingKey.ToArray(); keySsid = chosenSsid; keyCountry = chosenCountry; }
                });
            }
            finally { CryptographicOperations.ZeroMemory(pendingKey); if (!IsDisposed) Buttons(); }
        };
        disconnect.Click += async (_, _) => await Work(() => Operations.Disconnect(driver));
        save.Click += (_, _) =>
        {
            if (!Confirmed()) return;
            if (!ask("Save this network and enable connection before sign-in? A protected copy of this EXE and an encrypted Wi-Fi key will be stored on this Pi. The startup task uses SYSTEM. You can disable or forget it here.", "Enable startup connection")) return;
            byte[]? key = null;
            try { key = GetKey(); startup.Enable(country.Text, ssid.Text, key); RefreshStartupText(); message = "Saved. Connection will start automatically when the driver is ready after reboot."; }
            catch (Exception ex) { message = "Could not enable startup: " + ex.Message; }
            finally { if (key != null) CryptographicOperations.ZeroMemory(key); password.Clear(); Poll(); }
        };
        disable.Click += (_, _) => SetDisabled(false);
        forget.Click += (_, _) =>
        {
            if (ask("Disable automatic startup and delete the saved Wi-Fi key? The current connection will remain connected.", "Forget network")) SetDisabled(true);
        };
        timer.Tick += (_, _) => Poll(); Shown += (_, _) => { Poll(); timer.Start(); };
        FormClosing += (_, e) =>
        {
            timer.Stop(); closing.Cancel(); password.Clear(); ClearKey();
            if (busy) { e.Cancel = true; closePending = true; message = "Cancelling the app's pending operation..."; }
        };
        MarkSelection();
    }
    private bool Confirmed() => confirm.Checked && Protocol.ValidCountry(country.Text);
    private void RefreshStartupText()
    {
        startupEnabled = startup.Enabled(); hasSavedProfile = startup.Load() != null;
        startupText = startupEnabled == true ? "Automatic connection is enabled at Windows startup." :
            hasSavedProfile ? "Network saved; automatic startup is disabled." : "No saved network; automatic startup is disabled. Use Save & enable to set it up.";
    }
    private void SetDisabled(bool remove)
    {
        try { startup.Disable(remove); if (remove) ClearKey(); RefreshStartupText(); message = remove ? "Saved key removed; startup disabled. Current connection unchanged." : "Startup disabled. Current connection unchanged."; }
        catch (Exception ex) { message = ex.Message; }
        Poll();
    }
    private byte[] GetKey()
    {
        if (!Protocol.ValidSsid(ssid.Text)) throw new ArgumentException("Enter the exact Wi-Fi network name.");
        if (password.Text.Length != 0) return Protocol.Derive(ssid.Text, password.Text);
        if (sessionKey != null && keySsid == ssid.Text && keyCountry == country.Text) return sessionKey.ToArray();
        var saved = startup.Load();
        if (saved != null && saved.Ssid == ssid.Text && saved.Country == country.Text) return startup.Unprotect(saved);
        throw new ArgumentException("Enter the WPA2 password. There is no matching saved network.");
    }
    private void ClearKey() { if (sessionKey != null) CryptographicOperations.ZeroMemory(sessionKey); sessionKey = null; keySsid = keyCountry = ""; }
    private void Buttons()
    {
        bool free = !busy, valid = Confirmed() && Protocol.ValidSsid(ssid.Text);
        scan.Enabled = free && Confirmed() && state?.Idle == true;
        connect.Enabled = free && valid && state != null && state.Status == 0 && !state.Authenticated;
        disconnect.Enabled = free && state != null && state.Phase >= 500 && !state.Idle;
        save.Enabled = free && valid && state != null; disable.Enabled = free && startupEnabled == true; forget.Enabled = free && hasSavedProfile;
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
            if (state.Authenticated) status.Text += Environment.NewLine + "To scan again or choose another network, Disconnect first.";
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
            var item = new ListViewItem(["", entry.Display, entry.Rssi is < 0 and >= -127 ? $"{entry.Rssi} dBm" : "Unknown", entry.Band, entry.Channel.ToString(), entry.Security, entry.Bssid]) { Tag = entry };
            if (!entry.Supported) item.ForeColor = Color.DimGray; networks.Items.Add(item);
        }
        message = $"Found {report.Networks.Count} network entries." + (report.Truncated ? " Result limit reached." : "");
        MarkSelection();
    }
    private void MarkSelection()
    {
        if (synchronizingSelection) return;
        synchronizingSelection = true;
        try
        {
            ListViewItem? first = null, retained = null;
            foreach (ListViewItem item in networks.Items)
            {
                var entry = (NetworkEntry)item.Tag!;
                bool match = entry.Supported && string.Equals(entry.Ssid, ssid.Text, StringComparison.Ordinal);
                item.Text = match ? "Selected" : "";
                item.BackColor = match ? Color.LightBlue : SystemColors.Window;
                if (match) { first ??= item; if (item.Selected) retained = item; }
                else item.Selected = false;
            }
            var chosen = retained ?? first;
            if (chosen != null) chosen.Selected = true;
            selection.Text = !Protocol.ValidSsid(ssid.Text) ? "Choose a WPA2/AES network, or type an exact hidden SSID below." :
                "Selected SSID: " + ssid.Text + ". " + (first == null ? "Not in the current scan; manual/hidden network." : "Marked rows share this name. Band selection is automatic.");
        }
        finally { synchronizingSelection = false; }
    }
    private async Task Work(Action action, Action? completed = null)
    {
        if (busy) return; busy = true; message = ""; Buttons();
        try { await Task.Run(action); completed?.Invoke(); }
        catch (OperationCanceledException) { message = "App operation cancelled. An established connection is not disconnected."; }
        catch (Exception ex) { message = ex.Message; }
        finally { busy = false; Poll(); if (closePending) Close(); }
    }
    protected override void Dispose(bool disposing)
    {
        if (disposing) { timer.Dispose(); closing.Dispose(); ClearKey(); hints.Dispose(); appIcon.Dispose(); }
        base.Dispose(disposing);
    }
}
