namespace SolderStation.Models;

/// <summary>Known station parameters, their grouping and display format.</summary>
public static class ParamCatalog
{
    public const string GroupTemp = "G.Temp";
    public const string GroupSleep = "G.Sleep";
    public const string GroupClock = "G.Clock";
    public const string GroupDisplay = "G.Display";
    public const string GroupSystem = "G.System";
    public const string GroupOther = "G.Other";

    public static readonly string[] GroupOrder =
        { GroupTemp, GroupSleep, GroupClock, GroupDisplay, GroupSystem, GroupOther };

    private static readonly ParamDef[] Defs =
    {
        new("temp_min",   GroupTemp, UnitKey: "U.C", Step: 5),
        new("temp_max",   GroupTemp, UnitKey: "U.C", Step: 5),
        new("temp_step",  GroupTemp, UnitKey: "U.C"),
        new("boost_add",  GroupTemp, UnitKey: "U.C", Step: 5),
        new("boost_time", GroupTemp, UnitKey: "U.s", Step: 10),

        new("motion_en",   GroupSleep, ParamKind.Bool),
        new("sleep_time",  GroupSleep, UnitKey: "U.min", ZeroKey: "Z.never"),
        new("sleep_temp",  GroupSleep, UnitKey: "U.C", Step: 10),
        new("off_time",    GroupSleep, UnitKey: "U.min", ZeroKey: "Z.never"),
        new("wake_on_enc", GroupSleep, ParamKind.Bool),
        new("start_mode",  GroupSleep, ParamKind.Choice, OptionCount: 2),

        new("clock_en",   GroupClock, ParamKind.Bool),
        new("clock_24h",  GroupClock, ParamKind.Choice, OptionCount: 2),
        new("clock_show", GroupClock, UnitKey: "U.s"),
        new("set_show",   GroupClock, UnitKey: "U.s"),

        new("contrast",   GroupDisplay, UnitKey: "U.Pct", Step: 5),
        new("flip",       GroupDisplay, ParamKind.Bool),
        new("dim_idle",   GroupDisplay, ParamKind.Bool),
        new("buzzer",     GroupDisplay, ParamKind.Bool),
        new("enc_invert", GroupDisplay, ParamKind.Choice, OptionCount: 2),
        new("lang",       GroupDisplay, ParamKind.Choice, OptionCount: 2),

        new("pwm_period",  GroupSystem, UnitKey: "U.ms", Step: 10),
        new("adc_delay",   GroupSystem, UnitKey: "U.ms", Decimals: 1),
        new("power_limit", GroupSystem, UnitKey: "U.W", Step: 5, ZeroKey: "Z.none"),
        new("heater_res",  GroupSystem, UnitKey: "U.Ohm", Decimals: 1),
        new("low_volt",    GroupSystem, UnitKey: "U.V", Decimals: 1, ZeroKey: "Z.off"),
        new("adc_offset",  GroupSystem, UnitKey: "U.Lsb"),
    };

    private static readonly Dictionary<string, ParamDef> ByKey =
        Defs.ToDictionary(d => d.Key, StringComparer.OrdinalIgnoreCase);

    /// <summary>Definition for a key; unknown keys (newer firmware) get a generic one.</summary>
    public static ParamDef Get(string key) =>
        ByKey.TryGetValue(key, out var d) ? d : new ParamDef(key, GroupOther);

    public static int Order(string key)
    {
        int i = Array.FindIndex(Defs, d => d.Key.Equals(key, StringComparison.OrdinalIgnoreCase));
        return i < 0 ? int.MaxValue : i;
    }
}
