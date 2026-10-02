namespace SolderStation.Models;

/// <summary>Known station parameters with their Russian names and grouping.</summary>
public static class ParamCatalog
{
    public const string GroupTemp = "Температура";
    public const string GroupSleep = "Сон и датчик движения";
    public const string GroupClock = "Часы";
    public const string GroupDisplay = "Дисплей и звук";
    public const string GroupSystem = "Система";
    public const string GroupOther = "Прочее";

    public static readonly string[] GroupOrder =
        { GroupTemp, GroupSleep, GroupClock, GroupDisplay, GroupSystem, GroupOther };

    private static readonly ParamDef[] Defs =
    {
        new("temp_min",   GroupTemp, "Минимальная уставка", Unit: "°C", Step: 5),
        new("temp_max",   GroupTemp, "Максимальная уставка", Unit: "°C", Step: 5),
        new("temp_step",  GroupTemp, "Шаг уставки энкодером", Unit: "°C"),
        new("boost_add",  GroupTemp, "Добавка в режиме буст", Unit: "°C", Step: 5),
        new("boost_time", GroupTemp, "Длительность буста", Unit: "с", Step: 10),

        new("motion_en",   GroupSleep, "Датчик вибрации", ParamKind.Bool,
            Hint: "Пробуждение из сна движением ручки"),
        new("sleep_time",  GroupSleep, "Переход в сон через", Unit: "мин", ZeroText: "никогда",
            Hint: "Без движения ручки и действий с энкодером"),
        new("sleep_temp",  GroupSleep, "Температура сна", Unit: "°C", Step: 10),
        new("off_time",    GroupSleep, "Выключение после сна через", Unit: "мин", ZeroText: "никогда"),
        new("wake_on_enc", GroupSleep, "Вращение энкодера будит", ParamKind.Bool),
        new("start_mode",  GroupSleep, "При включении питания", ParamKind.Choice,
            Options: new[] { "Выключен", "Нагрев" }),

        new("clock_en",   GroupClock, "Показывать часы", ParamKind.Bool,
            Hint: "Часы чередуются с уставкой, когда паяльник выключен"),
        new("clock_24h",  GroupClock, "Формат времени", ParamKind.Choice, Options: new[] { "12 ч", "24 ч" }),
        new("clock_show", GroupClock, "Показ часов", Unit: "с"),
        new("set_show",   GroupClock, "Показ уставки", Unit: "с"),

        new("contrast",   GroupDisplay, "Яркость", Unit: "%", Step: 5),
        new("flip",       GroupDisplay, "Повернуть экран на 180°", ParamKind.Bool),
        new("dim_idle",   GroupDisplay, "Затемнять в простое", ParamKind.Bool),
        new("buzzer",     GroupDisplay, "Звук", ParamKind.Bool),
        new("enc_invert", GroupDisplay, "Направление энкодера", ParamKind.Choice,
            Options: new[] { "Обычное", "Обратное" }),

        new("pwm_period",  GroupSystem, "Период ШИМ / регулятора", Unit: "мс", Step: 10),
        new("adc_delay",   GroupSystem, "Пауза перед измерением", Unit: "мс", Decimals: 1,
            Hint: "Время от выключения нагревателя до запуска АЦП"),
        new("power_limit", GroupSystem, "Ограничение мощности", Unit: "Вт", Step: 5, ZeroText: "нет"),
        new("heater_res",  GroupSystem, "Сопротивление нагревателя", Unit: "Ом", Decimals: 1),
        new("low_volt",    GroupSystem, "Порог низкого напряжения", Unit: "В", Decimals: 1, ZeroText: "выкл"),
        new("adc_offset",  GroupSystem, "Смещение нуля усилителя", Unit: "ед. АЦП"),
    };

    private static readonly Dictionary<string, ParamDef> ByKey =
        Defs.ToDictionary(d => d.Key, StringComparer.OrdinalIgnoreCase);

    /// <summary>Definition for a key; unknown keys (newer firmware) get a generic one.</summary>
    public static ParamDef Get(string key) =>
        ByKey.TryGetValue(key, out var d) ? d : new ParamDef(key, GroupOther, key);

    public static int Order(string key)
    {
        int i = Array.FindIndex(Defs, d => d.Key.Equals(key, StringComparison.OrdinalIgnoreCase));
        return i < 0 ? int.MaxValue : i;
    }
}
