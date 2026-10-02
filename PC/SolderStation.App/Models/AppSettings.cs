namespace SolderStation.Models;

/// <summary>Application preferences, stored in %AppData%\T12Station\appsettings.json.</summary>
public sealed class AppSettings
{
    public bool DarkTheme { get; set; }
    public string? LastPort { get; set; }
    public int BaudRate { get; set; } = 115200;
    public bool AutoConnect { get; set; } = true;
    public bool AutoReconnect { get; set; } = true;
    public bool AutoBackup { get; set; } = true;
    public int StreamIntervalMs { get; set; } = 200;
    public List<int> Presets { get; set; } = new() { 250, 300, 320, 350, 380, 420 };
    public double WindowWidth { get; set; } = 1100;
    public double WindowHeight { get; set; } = 760;
}
