using System.IO;
using System.Text.Json;
using SolderStation.Models;

namespace SolderStation.Services;

/// <summary>Loads and saves <see cref="AppSettings"/> in the user's AppData folder.</summary>
public static class AppSettingsStore
{
    private static readonly JsonSerializerOptions JsonOptions = new() { WriteIndented = true };

    public static string Folder { get; } =
        Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "T12Station");

    public static string BackupFolder => Path.Combine(Folder, "backups");

    private static string FilePath => Path.Combine(Folder, "appsettings.json");

    public static AppSettings Load()
    {
        try
        {
            if (File.Exists(FilePath))
            {
                return JsonSerializer.Deserialize<AppSettings>(File.ReadAllText(FilePath), JsonOptions) ?? new AppSettings();
            }
        }
        catch (Exception)
        {
            // broken file: start with defaults
        }
        return new AppSettings();
    }

    public static void Save(AppSettings settings)
    {
        try
        {
            Directory.CreateDirectory(Folder);
            File.WriteAllText(FilePath, JsonSerializer.Serialize(settings, JsonOptions));
        }
        catch (Exception)
        {
            // not critical
        }
    }
}
