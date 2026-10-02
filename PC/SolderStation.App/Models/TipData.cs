using System.Globalization;

namespace SolderStation.Models;

/// <summary>Tip profile exactly as stored in the station (raw units).</summary>
public sealed record TipData
{
    public int Index { get; init; }
    public string Name { get; init; } = "TIP";
    public int Setpoint { get; init; } = 320;
    /// <summary>PID gains multiplied by 1000.</summary>
    public int Kp { get; init; } = 30;
    public int Ki { get; init; } = 10;
    public int Kd { get; init; } = 5;
    /// <summary>ADC readings at the three calibration points.</summary>
    public int[] CalAdc { get; init; } = new int[3];
    /// <summary>Tip minus cold junction temperature at those points, °C.</summary>
    public int[] CalDt { get; init; } = new int[3];

    /// <summary>
    /// Parses "TIP i set kp ki kd adc1 adc2 adc3 dt1 dt2 dt3 name".
    /// The name is the rest of the line and may contain spaces.
    /// </summary>
    public static TipData? Parse(string line)
    {
        var p = line.Split(' ', 13, StringSplitOptions.None);
        if (p.Length < 13 || p[0] != "TIP") return null;
        int N(int i) => int.Parse(p[i], CultureInfo.InvariantCulture);
        try
        {
            return new TipData
            {
                Index = N(1),
                Setpoint = N(2),
                Kp = N(3),
                Ki = N(4),
                Kd = N(5),
                CalAdc = new[] { N(6), N(7), N(8) },
                CalDt = new[] { N(9), N(10), N(11) },
                Name = p[12].Trim(),
            };
        }
        catch (FormatException)
        {
            return null;
        }
    }
}

/// <summary>Result of the "TIPS" command.</summary>
public sealed record TipList(int Active, int Max, IReadOnlyList<TipData> Tips);
