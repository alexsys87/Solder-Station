using System.Globalization;
using SolderStation.Models;

namespace SolderStation.Services;

/// <summary>Typed wrappers for the station commands (see README, "Протокол").</summary>
public sealed partial class StationClient
{
    private static string Inv(int v) => v.ToString(CultureInfo.InvariantCulture);

    /// <summary>"PING" → firmware version, throws if the device is not a T12 station.</summary>
    public async Task<string> PingAsync(int timeoutMs = 1000)
    {
        var data = await QueryAsync("PING", timeoutMs).ConfigureAwait(false);
        var line = data.FirstOrDefault() ?? "";
        if (!line.StartsWith("T12STATION", StringComparison.Ordinal))
            throw new StationException(0, Loc.T("X.UnknownDevice"));
        return KeyValueParser.Parse(line).GetValueOrDefault("fw", "?");
    }

    public async Task<Dictionary<string, string>> GetInfoAsync()
    {
        var data = await QueryAsync("INFO").ConfigureAwait(false);
        return KeyValueParser.Parse(string.Join(' ', data));
    }

    public async Task<StationStatus> GetStatusAsync()
    {
        var data = await QueryAsync("STATUS").ConfigureAwait(false);
        return StationStatus.Parse(data.FirstOrDefault() ?? "");
    }

    public Task StreamAsync(int intervalMs) => QueryAsync("STREAM " + Inv(intervalMs));

    public async Task<IReadOnlyList<ParamValue>> GetParamsAsync()
    {
        var data = await QueryAsync("PARAMS", 3000).ConfigureAwait(false);
        var list = new List<ParamValue>();
        foreach (var line in data)
        {
            var p = line.Split(' ', StringSplitOptions.RemoveEmptyEntries);
            if (p.Length == 4
                && int.TryParse(p[1], NumberStyles.Integer, CultureInfo.InvariantCulture, out int min)
                && int.TryParse(p[2], NumberStyles.Integer, CultureInfo.InvariantCulture, out int max)
                && int.TryParse(p[3], NumberStyles.Integer, CultureInfo.InvariantCulture, out int value))
            {
                list.Add(new ParamValue(p[0], min, max, value));
            }
        }
        return list;
    }

    public Task SetParamAsync(string key, int value) => QueryAsync($"SET {key} {Inv(value)}");

    public Task SaveAsync() => QueryAsync("SAVE", 5000);
    public Task DefaultsAsync() => QueryAsync("DEFAULTS");
    public Task SetModeAsync(string mode) => QueryAsync("MODE " + mode);
    public Task SetTemperatureAsync(int temp) => QueryAsync("TEMP " + Inv(temp));
    public Task ClearErrorsAsync() => QueryAsync("CLEAR");
    public Task BeepAsync() => QueryAsync("BEEP");
    public Task ResetAsync() => QueryAsync("RESET");
    public Task BootloaderAsync() => QueryAsync("DFU");

    public async Task<TipList> GetTipsAsync()
    {
        var data = await QueryAsync("TIPS", 3000).ConfigureAwait(false);
        int active = 0, max = 10;
        var tips = new List<TipData>();
        foreach (var line in data)
        {
            if (line.StartsWith("ACTIVE", StringComparison.Ordinal))
            {
                var p = line.Split(' ', StringSplitOptions.RemoveEmptyEntries);
                if (p.Length >= 4)
                {
                    active = int.Parse(p[1], CultureInfo.InvariantCulture);
                    max = int.Parse(p[3], CultureInfo.InvariantCulture);
                }
            }
            else if (TipData.Parse(line) is { } tip)
            {
                tips.Add(tip);
            }
        }
        return new TipList(active, max, tips);
    }

    public Task SelectTipAsync(int index) => QueryAsync("TIP SEL " + Inv(index));
    public Task DeleteTipAsync(int index) => QueryAsync("TIP DEL " + Inv(index));
    public Task ResetTipCalibrationAsync(int index) => QueryAsync("TIP CALRESET " + Inv(index));
    public Task RenameTipAsync(int index, string name) => QueryAsync($"TIP NAME {Inv(index)} {name}");
    public Task SetTipFieldAsync(int index, string field, int value) =>
        QueryAsync($"TIP SET {Inv(index)} {field} {Inv(value)}");

    /// <summary>Adds a tip with default values, returns its index.</summary>
    public async Task<int> AddTipAsync(string name)
    {
        var data = await QueryAsync("TIP ADD " + name).ConfigureAwait(false);
        return int.Parse(data.First(), CultureInfo.InvariantCulture);
    }

    /// <summary>Writes every field of a tip profile.</summary>
    public async Task WriteTipAsync(int index, TipData tip)
    {
        await RenameTipAsync(index, tip.Name).ConfigureAwait(false);
        await SetTipFieldAsync(index, "set", tip.Setpoint).ConfigureAwait(false);
        await SetTipFieldAsync(index, "kp", tip.Kp).ConfigureAwait(false);
        await SetTipFieldAsync(index, "ki", tip.Ki).ConfigureAwait(false);
        await SetTipFieldAsync(index, "kd", tip.Kd).ConfigureAwait(false);
        for (int i = 0; i < 3; i++)
        {
            await SetTipFieldAsync(index, $"adc{i + 1}", tip.CalAdc[i]).ConfigureAwait(false);
            await SetTipFieldAsync(index, $"dt{i + 1}", tip.CalDt[i]).ConfigureAwait(false);
        }
    }

    public async Task<DateTime?> GetTimeAsync()
    {
        var data = await QueryAsync("TIME").ConfigureAwait(false);
        return DateTime.TryParseExact(data.FirstOrDefault(), "yyyy-MM-dd HH:mm:ss",
            CultureInfo.InvariantCulture, DateTimeStyles.None, out var t) ? t : null;
    }

    public Task SetTimeAsync(DateTime time) =>
        QueryAsync("TIME " + time.ToString("yyyy-MM-dd HH:mm:ss", CultureInfo.InvariantCulture));

    // ---- remote calibration ----
    public Task CalibrationStartAsync() => QueryAsync("CAL START");
    public Task CalibrationAbortAsync() => QueryAsync("CAL ABORT");
    public Task CalibrationPointAsync(int measured) => QueryAsync("CAL POINT " + Inv(measured));

    public async Task<CalibrationState> GetCalibrationStateAsync()
    {
        var data = await QueryAsync("CAL").ConfigureAwait(false);
        var d = KeyValueParser.Parse(data.FirstOrDefault() ?? "");
        return new CalibrationState(
            d.GetInt("active") != 0, d.GetInt("step"), d.GetInt("target"),
            d.GetDouble("tip"), d.GetInt("stable") != 0, d.GetInt("done") != 0, d.GetInt("ok") != 0);
    }
}

public sealed record CalibrationState(bool Active, int Step, int Target, double TipTemp,
                                      bool Stable, bool Done, bool Ok);
