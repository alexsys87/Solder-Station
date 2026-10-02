using System.Collections.ObjectModel;
using System.Globalization;
using CommunityToolkit.Mvvm.ComponentModel;
using SolderStation.Models;
using SolderStation.Services;

namespace SolderStation.ViewModels;

/// <summary>
/// One station parameter on the settings page. Changes are sent to the
/// device right away (debounced), the device stores them in flash a few
/// seconds later on its own.
/// </summary>
public sealed partial class ParamViewModel : ObservableObject
{
    private const int DebounceMs = 300;

    private readonly Func<ParamViewModel, Task> _send;
    private readonly double _scale;
    private bool _suppress;
    private int _version;

    public ParamViewModel(ParamDef def, ParamValue value, Func<ParamViewModel, Task> send)
    {
        Def = def;
        _send = send;
        _scale = Math.Pow(10, def.Decimals);
        RawMin = value.Min;
        RawMax = value.Max;
        Options = new ReadOnlyCollection<string>(def.Options.ToList());
        SetFromDevice(value.Value);
    }

    public ParamDef Def { get; }
    public string Key => Def.Key;
    public string Title => Def.Title;
    public string Unit => Def.Unit;
    public string Hint => Def.ZeroText != null
        ? (string.IsNullOrEmpty(Def.Hint) ? Loc.F("M.ZeroOnly", Def.ZeroText) : Loc.F("M.HintZero", Def.Hint, Def.ZeroText))
        : Def.Hint;
    public bool HasHint => !string.IsNullOrEmpty(Hint);

    public bool IsNumber => Def.Kind == ParamKind.Number;
    public bool IsBool => Def.Kind == ParamKind.Bool;
    public bool IsChoice => Def.Kind == ParamKind.Choice;
    public IReadOnlyList<string> Options { get; }

    public int RawMin { get; }
    public int RawMax { get; }
    public double Minimum => RawMin / _scale;
    public double Maximum => RawMax / _scale;
    public double SmallStep => Def.Step / _scale;
    public double LargeStep => Math.Max(SmallStep, (Maximum - Minimum) / 10.0);

    /// <summary>Value in display units (e.g. ms with one decimal).</summary>
    [ObservableProperty]
    private double _value;

    /// <summary>True while a change waits to be sent.</summary>
    [ObservableProperty]
    private bool _isSending;

    public int RawValue => (int)Math.Round(Value * _scale);

    public bool BoolValue
    {
        get => RawValue != 0;
        set => Value = value ? 1 : 0;
    }

    public int ChoiceIndex
    {
        get => RawValue;
        set => Value = value;
    }

    /// <summary>Text box binding: accepts both "2.5" and "2,5".</summary>
    public string ValueText
    {
        get
        {
            if (Def.ZeroText != null && RawValue == 0) return "0";
            return Value.ToString("F" + Def.Decimals, Loc.Culture);
        }
        set
        {
            var s = (value ?? "").Trim().Replace(',', '.');
            if (double.TryParse(s, NumberStyles.Float, CultureInfo.InvariantCulture, out double v))
            {
                Value = v;
            }
            OnPropertyChanged();
        }
    }

    public string DisplayText => Def.ZeroText != null && RawValue == 0
        ? Def.ZeroText
        : $"{Value.ToString("F" + Def.Decimals, Loc.Culture)} {Unit}".Trim();

    partial void OnValueChanged(double value)
    {
        double clamped = Math.Clamp(Math.Round(value * _scale), RawMin, RawMax) / _scale;
        if (Math.Abs(clamped - value) > 1e-9)
        {
            Value = clamped;          // re-enters with the clamped value
            return;
        }
        OnPropertyChanged(nameof(RawValue));
        OnPropertyChanged(nameof(BoolValue));
        OnPropertyChanged(nameof(ChoiceIndex));
        OnPropertyChanged(nameof(ValueText));
        OnPropertyChanged(nameof(DisplayText));
        if (!_suppress) _ = SendDebouncedAsync();
    }

    private async Task SendDebouncedAsync()
    {
        int version = ++_version;
        IsSending = true;
        await Task.Delay(DebounceMs);
        if (version != _version) return;      // a newer change follows
        try
        {
            await _send(this);
        }
        finally
        {
            if (version == _version) IsSending = false;
        }
    }

    /// <summary>Updates the value without sending it back.</summary>
    public void SetFromDevice(int raw)
    {
        if (IsSending) return;                // the user is changing it right now
        _suppress = true;
        Value = raw / _scale;
        _suppress = false;
    }
}

/// <summary>A titled group of parameters.</summary>
public sealed class ParamGroupViewModel
{
    public ParamGroupViewModel(string title, IEnumerable<ParamViewModel> items)
    {
        Title = title;
        Items = new ObservableCollection<ParamViewModel>(items);
    }

    public string Title { get; }
    public ObservableCollection<ParamViewModel> Items { get; }
}
