using System.Text.Json.Serialization;

namespace SolderStation.Models;

/// <summary>Export / import file (JSON). Values are in raw device units.</summary>
public sealed class SettingsFile
{
    public const string FormatId = "T12Station.Settings";

    [JsonPropertyName("format")] public string Format { get; set; } = FormatId;
    [JsonPropertyName("version")] public int Version { get; set; } = 1;
    [JsonPropertyName("exported")] public DateTime Exported { get; set; } = DateTime.Now;
    [JsonPropertyName("firmware")] public string Firmware { get; set; } = "";
    [JsonPropertyName("deviceId")] public string DeviceId { get; set; } = "";
    [JsonPropertyName("parameters")] public Dictionary<string, int> Parameters { get; set; } = new();
    [JsonPropertyName("activeTip")] public int ActiveTip { get; set; }
    [JsonPropertyName("tips")] public List<SettingsFileTip> Tips { get; set; } = new();
}

public sealed class SettingsFileTip
{
    [JsonPropertyName("name")] public string Name { get; set; } = "TIP";
    [JsonPropertyName("setpoint")] public int Setpoint { get; set; } = 320;
    [JsonPropertyName("kp")] public int Kp { get; set; } = 30;
    [JsonPropertyName("ki")] public int Ki { get; set; } = 10;
    [JsonPropertyName("kd")] public int Kd { get; set; } = 5;
    [JsonPropertyName("calAdc")] public int[] CalAdc { get; set; } = new int[3];
    [JsonPropertyName("calDt")] public int[] CalDt { get; set; } = new int[3];

    public static SettingsFileTip From(TipData t) => new()
    {
        Name = t.Name,
        Setpoint = t.Setpoint,
        Kp = t.Kp,
        Ki = t.Ki,
        Kd = t.Kd,
        CalAdc = (int[])t.CalAdc.Clone(),
        CalDt = (int[])t.CalDt.Clone(),
    };
}
