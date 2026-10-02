namespace SolderStation.Models;

public enum ParamKind
{
    Number,
    Bool,
    Choice
}

/// <summary>Description of a station parameter for the settings page.</summary>
public sealed record ParamDef(
    string Key,
    string Group,
    string Title,
    ParamKind Kind = ParamKind.Number,
    string Unit = "",
    int Decimals = 0,
    int Step = 1,
    string[]? Options = null,
    string Hint = "",
    string? ZeroText = null);

/// <summary>Raw parameter as reported by "PARAMS": key min max value.</summary>
public sealed record ParamValue(string Key, int Min, int Max, int Value);
