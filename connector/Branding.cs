namespace Rpi5Wifi;

internal static class Branding
{
    public const string Version = "0.6.29.2";
    public static Stream OpenIcon() => typeof(Branding).Assembly.GetManifestResourceStream("Rpi5Wifi.ConnectorIcon")
        ?? throw new InvalidOperationException("The embedded connector icon is missing.");

    public static Icon LoadIcon()
    {
        using var stream = OpenIcon();
        using var icon = new Icon(stream);
        return (Icon)icon.Clone();
    }
}
