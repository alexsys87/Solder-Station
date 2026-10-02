using System.IO;
using System.Text.Json;
using SolderStation.Models;

namespace SolderStation.Services;

/// <summary>Reads all settings from the station into a file and writes them back.</summary>
public static class SettingsTransfer
{
    private static readonly JsonSerializerOptions JsonOptions = new() { WriteIndented = true };

    /// <summary>Reads parameters and tip profiles from the device.</summary>
    public static async Task<SettingsFile> ReadFromDeviceAsync(StationClient client)
    {
        var info = await client.GetInfoAsync();
        var parameters = await client.GetParamsAsync();
        var tips = await client.GetTipsAsync();

        return new SettingsFile
        {
            Firmware = info.GetValueOrDefault("fw", ""),
            DeviceId = info.GetValueOrDefault("uid", ""),
            Exported = DateTime.Now,
            Parameters = parameters.ToDictionary(p => p.Key, p => p.Value),
            ActiveTip = tips.Active,
            Tips = tips.Tips.Select(SettingsFileTip.From).ToList(),
        };
    }

    /// <summary>
    /// Writes a settings file into the device. Values are clamped to the
    /// device ranges, unknown parameters are skipped. Returns warnings.
    /// </summary>
    public static async Task<IReadOnlyList<string>> WriteToDeviceAsync(
        StationClient client, SettingsFile file, bool parameters, bool tips, IProgress<string>? progress = null)
    {
        var warnings = new List<string>();

        if (parameters)
        {
            var device = (await client.GetParamsAsync()).ToDictionary(p => p.Key, StringComparer.OrdinalIgnoreCase);
            foreach (var (key, value) in file.Parameters)
            {
                if (!device.TryGetValue(key, out var p))
                {
                    warnings.Add(Loc.F("T.Unsupported", key));
                    continue;
                }
                int v = Math.Clamp(value, p.Min, p.Max);
                if (v != value) warnings.Add(Loc.F("T.Clamped", key, value, v));
                progress?.Report(Loc.F("T.Param", key, v));
                await client.SetParamAsync(p.Key, v);
            }
        }

        if (tips && file.Tips.Count > 0)
        {
            var list = await client.GetTipsAsync();
            int wanted = Math.Min(file.Tips.Count, list.Max);
            if (file.Tips.Count > list.Max) warnings.Add(Loc.F("T.TooManyTips", file.Tips.Count, list.Max));

            int count = list.Tips.Count;
            while (count < wanted)
            {
                progress?.Report(Loc.T("T.AddTip"));
                await client.AddTipAsync("TIP");
                count++;
            }
            while (count > wanted)
            {
                progress?.Report(Loc.T("T.DelTip"));
                await client.DeleteTipAsync(count - 1);
                count--;
            }

            // the setpoint must respect the (possibly just imported) limits
            var limits = (await client.GetParamsAsync()).ToDictionary(p => p.Key, StringComparer.OrdinalIgnoreCase);
            int tMin = limits.TryGetValue("temp_min", out var pMin) ? pMin.Value : 100;
            int tMax = limits.TryGetValue("temp_max", out var pMax) ? pMax.Value : 480;

            for (int i = 0; i < wanted; i++)
            {
                var t = file.Tips[i];
                bool calOk = t.CalAdc.Length == 3 && t.CalDt.Length == 3;
                if (!calOk) warnings.Add(Loc.F("T.NoCal", t.Name));
                progress?.Report(Loc.F("T.Tip", i + 1, t.Name));
                await client.WriteTipAsync(i, new TipData
                {
                    Index = i,
                    Name = string.IsNullOrWhiteSpace(t.Name) ? "TIP" : t.Name,
                    Setpoint = Math.Clamp(t.Setpoint, tMin, tMax),
                    Kp = Math.Clamp(t.Kp, 0, 2000),
                    Ki = Math.Clamp(t.Ki, 0, 2000),
                    Kd = Math.Clamp(t.Kd, 0, 2000),
                    CalAdc = calOk ? t.CalAdc.Select(v => Math.Clamp(v, 1, 4095)).ToArray() : new[] { 1254, 1811, 2368 },
                    CalDt = calOk ? t.CalDt.Select(v => Math.Clamp(v, 1, 700)).ToArray() : new[] { 225, 325, 425 },
                });
            }
            await client.SelectTipAsync(Math.Clamp(file.ActiveTip, 0, wanted - 1));
        }

        progress?.Report(Loc.T("T.Saving"));
        await client.SaveAsync();
        return warnings;
    }

    public static void SaveToFile(SettingsFile file, string path) =>
        File.WriteAllText(path, JsonSerializer.Serialize(file, JsonOptions));

    public static SettingsFile LoadFromFile(string path)
    {
        var file = JsonSerializer.Deserialize<SettingsFile>(File.ReadAllText(path), JsonOptions)
                   ?? throw new InvalidDataException(Loc.T("T.EmptyFile"));
        if (file.Format != SettingsFile.FormatId)
            throw new InvalidDataException(Loc.T("T.NotSettings"));
        return file;
    }

    /// <summary>Automatic backup on connect: keeps the last 20 files per device.</summary>
    public static string? Backup(SettingsFile file)
    {
        try
        {
            Directory.CreateDirectory(AppSettingsStore.BackupFolder);
            string id = string.IsNullOrEmpty(file.DeviceId) ? "station" : file.DeviceId;
            string path = Path.Combine(AppSettingsStore.BackupFolder, $"{id}_{DateTime.Now:yyyyMMdd_HHmmss}.json");
            SaveToFile(file, path);

            foreach (var old in Directory.GetFiles(AppSettingsStore.BackupFolder, id + "_*.json")
                                         .OrderByDescending(f => f).Skip(20))
            {
                File.Delete(old);
            }
            return path;
        }
        catch (Exception)
        {
            return null;
        }
    }
}
