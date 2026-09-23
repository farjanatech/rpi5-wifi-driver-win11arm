using System.Runtime.InteropServices;
namespace Rpi5Wifi;
internal static class Program
{
    [STAThread]
    private static int Main(string[] args)
    {
        // GitHub-only tests/preview never instantiate Driver or touch saved
        // user profiles, network configuration or certificates. Self-tests use
        // and remove an inert, uniquely named CI task to check the scheduler.
        if (args.Length != 0 && args[0] is "--self-test" or "--preview")
        {
            if (Environment.GetEnvironmentVariable("GITHUB_ACTIONS") != "true") return 4;
            if (args[0] == "--self-test") { ApplicationConfiguration.Initialize(); return SelfTests.Run(); }
            if (args.Length != 2) return 4;
            string root = Path.GetFullPath(Path.Combine(Environment.CurrentDirectory, "ci-logs")) + Path.DirectorySeparatorChar;
            string file = Path.GetFullPath(args[1]);
            if (!file.StartsWith(root, StringComparison.OrdinalIgnoreCase) || Path.GetExtension(file) != ".png") return 4;
            ApplicationConfiguration.Initialize();
            using var form = new MainForm(new RejectDriver(), true);
            form.Show(); Application.DoEvents(); form.Refresh(); Application.DoEvents();
            using var bitmap = new Bitmap(form.Width, form.Height); form.DrawToBitmap(bitmap, new Rectangle(0, 0, form.Width, form.Height));
            bitmap.Save(file, System.Drawing.Imaging.ImageFormat.Png); form.Close(); return 0;
        }
        if (RuntimeInformation.ProcessArchitecture != Architecture.Arm64)
        {
            if (args.Length == 0) MessageBox.Show("Run this connector on the Raspberry Pi 5 with Windows ARM64. No action was taken.");
            return 4;
        }
        if (args.Length == 1 && args[0] == "--autoconnect") return Startup.Run(new Driver());
        if (args.Length != 0) return 4;
        ApplicationConfiguration.Initialize(); Application.Run(new MainForm(new Driver())); return 0;
    }
    private sealed class RejectDriver : IDriver
    {
        public byte[] Call(uint code, byte[]? input = null) => throw new InvalidOperationException("Offline preview must not call a driver.");
    }
}
