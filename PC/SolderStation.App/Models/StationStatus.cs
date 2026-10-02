using System.Globalization;

namespace SolderStation.Models;

/// <summary>Live state of the station, parsed from "STATUS" / "!S" lines.</summary>
public sealed class StationStatus
{
    public string Mode { get; init; } = "OFF";
    public bool AutoOff { get; init; }
    public int Setpoint { get; init; }
    public int Target { get; init; }
    public double TipTemp { get; init; }
    public int Raw { get; init; }
    public double Duty { get; init; }
    public double Power { get; init; }
    public double Vin { get; init; }
    public double ColdJunction { get; init; }
    public double McuTemp { get; init; }
    public int Errors { get; init; }
    public int TipIndex { get; init; }
    public int BoostLeft { get; init; }
    public bool Stable { get; init; }
    public bool Calibrating { get; init; }

    public const int ErrNoTip = 0x01;
    public const int ErrOverheat = 0x02;
    public const int ErrRunaway = 0x04;
    public const int ErrLowVoltage = 0x08;

    /// <summary>Parses "key=value key=value ..." pairs.</summary>
    public static StationStatus Parse(string text)
    {
        var d = KeyValueParser.Parse(text);
        return new StationStatus
        {
            Mode = d.GetValueOrDefault("mode", "OFF"),
            AutoOff = d.GetInt("auto") != 0,
            Setpoint = d.GetInt("set"),
            Target = d.GetInt("tgt"),
            TipTemp = d.GetDouble("tip"),
            Raw = d.GetInt("raw"),
            Duty = d.GetDouble("duty"),
            Power = d.GetDouble("pwr"),
            Vin = d.GetDouble("vin"),
            ColdJunction = d.GetDouble("cj"),
            McuTemp = d.GetDouble("mcu"),
            Errors = d.GetInt("err"),
            TipIndex = d.GetInt("tipn"),
            BoostLeft = d.GetInt("boost"),
            Stable = d.GetInt("stable") != 0,
            Calibrating = d.GetInt("cal") != 0,
        };
    }

    public string ErrorText
    {
        get
        {
            if (Errors == 0) return string.Empty;
            var list = new List<string>();
            if ((Errors & ErrNoTip) != 0) list.Add("нет жала");
            if ((Errors & ErrOverheat) != 0) list.Add("перегрев");
            if ((Errors & ErrRunaway) != 0) list.Add("нет роста температуры");
            if ((Errors & ErrLowVoltage) != 0) list.Add("низкое напряжение");
            return string.Join(", ", list);
        }
    }
}

/// <summary>Helpers for "key=value" lists used by the protocol.</summary>
public static class KeyValueParser
{
    public static Dictionary<string, string> Parse(string text)
    {
        var result = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        foreach (var token in text.Split(' ', StringSplitOptions.RemoveEmptyEntries))
        {
            int eq = token.IndexOf('=');
            if (eq > 0) result[token[..eq]] = token[(eq + 1)..];
        }
        return result;
    }

    public static int GetInt(this IReadOnlyDictionary<string, string> d, string key, int def = 0) =>
        d.TryGetValue(key, out var s) && int.TryParse(s, NumberStyles.Integer, CultureInfo.InvariantCulture, out int v) ? v : def;

    public static double GetDouble(this IReadOnlyDictionary<string, string> d, string key, double def = 0) =>
        d.TryGetValue(key, out var s) && double.TryParse(s, NumberStyles.Float, CultureInfo.InvariantCulture, out double v) ? v : def;
}
