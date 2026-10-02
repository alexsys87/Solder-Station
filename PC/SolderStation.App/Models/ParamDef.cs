using SolderStation.Services;

namespace SolderStation.Models;

public enum ParamKind
{
    Number,
    Bool,
    Choice
}

/// <summary>
/// Description of a station parameter for the settings page. Texts are
/// resolved through <see cref="Loc"/>: title "P.key", hint "H.key",
/// options "O.key.N", unit and zero text by their keys.
/// </summary>
public sealed record ParamDef(
    string Key,
    string Group,
    ParamKind Kind = ParamKind.Number,
    string UnitKey = "",
    int Decimals = 0,
    int Step = 1,
    int OptionCount = 0,
    string? ZeroKey = null)
{
    public string Title => Loc.Has("P." + Key) ? Loc.T("P." + Key) : Key;
    public string GroupTitle => Loc.T(Group);
    public string Hint => Loc.Has("H." + Key) ? Loc.T("H." + Key) : "";
    public string Unit => UnitKey.Length == 0 ? "" : Loc.T(UnitKey);
    public string? ZeroText => ZeroKey == null ? null : Loc.T(ZeroKey);

    public IReadOnlyList<string> Options =>
        Enumerable.Range(0, OptionCount).Select(i => Loc.T($"O.{Key}.{i}")).ToList();
}

/// <summary>Raw parameter as reported by "PARAMS": key min max value.</summary>
public sealed record ParamValue(string Key, int Min, int Max, int Value);
