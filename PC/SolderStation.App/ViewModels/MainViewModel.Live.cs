using System.Collections.ObjectModel;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using SolderStation.Models;
using SolderStation.Services;

namespace SolderStation.ViewModels;

/// <summary>Live values, operating mode and the quick setpoint controls.</summary>
public sealed partial class MainViewModel
{
    private bool _suppressSetpoint;
    private int _setpointVersion;
    private DateTime _setpointEditedAt = DateTime.MinValue;

    [ObservableProperty] private double _tipTemp;
    [ObservableProperty] private int _target;
    [ObservableProperty] private int _deviceSetpoint;
    [ObservableProperty] private double _power;
    [ObservableProperty] private double _duty;
    [ObservableProperty] private double _vin;
    [ObservableProperty] private double _coldJunction;
    [ObservableProperty] private double _mcuTemp;
    [ObservableProperty] private int _rawAdc;
    [ObservableProperty] private string _mode = "OFF";
    [ObservableProperty] private string _modeText = "—";
    [ObservableProperty] private string _errorText = "";
    [ObservableProperty] private bool _hasError;
    [ObservableProperty] private bool _noTip;
    [ObservableProperty] private bool _isHeating;
    [ObservableProperty] private bool _isStable;

    /// <summary>Setpoint slider / box; changes are sent to the station.</summary>
    [ObservableProperty] private int _setpoint = 320;
    [ObservableProperty] private int _setpointMin = 100;
    [ObservableProperty] private int _setpointMax = 480;

    public ObservableCollection<int> Presets { get; }

    private StationStatus? _lastStatus;

    private void ApplyStatus(StationStatus s)
    {
        _lastStatus = s;
        TipTemp = s.TipTemp;
        Target = s.Target;
        DeviceSetpoint = s.Setpoint;
        Power = s.Power;
        Duty = s.Duty;
        Vin = s.Vin;
        ColdJunction = s.ColdJunction;
        McuTemp = s.McuTemp;
        RawAdc = s.Raw;
        Mode = s.Mode;
        NoTip = (s.Errors & StationStatus.ErrNoTip) != 0;
        HasError = s.Errors != 0;
        ErrorText = s.ErrorText;
        IsHeating = s.Mode is "RUN" or "BOOST" or "SLEEP" or "CAL";
        IsStable = s.Stable;
        ModeText = s.Mode switch
        {
            "RUN" => Loc.T(s.Stable ? "M.ModeRunReady" : "M.ModeRunHeat"),
            "BOOST" => Loc.F("M.ModeBoost", s.BoostLeft),
            "SLEEP" => Loc.T("M.ModeSleep"),
            "CAL" => Loc.T("M.ModeCal"),
            _ => Loc.T(s.AutoOff ? "M.ModeOffAuto" : "M.ModeOff"),
        };

        // follow the encoder unless the user is moving the slider right now
        if ((DateTime.Now - _setpointEditedAt).TotalSeconds > 2 && Setpoint != s.Setpoint)
        {
            _suppressSetpoint = true;
            Setpoint = s.Setpoint;
            _suppressSetpoint = false;
        }

        if (s.TipIndex != ActiveTipIndex && s.TipIndex < Tips.Count)
        {
            SetActiveTipFromDevice(s.TipIndex);
        }
    }

    partial void OnSetpointChanged(int value)
    {
        if (_suppressSetpoint || !IsConnected) return;
        _setpointEditedAt = DateTime.Now;
        _ = SendSetpointAsync();
    }

    private async Task SendSetpointAsync()
    {
        int version = ++_setpointVersion;
        await Task.Delay(250);
        if (version != _setpointVersion) return;
        int v = Math.Clamp(Setpoint, SetpointMin, SetpointMax);
        await RunAsync(() => _client.SetTemperatureAsync(v), null, quiet: true);
        _setpointEditedAt = DateTime.Now;
    }

    [RelayCommand]
    private void ChangeSetpoint(string delta)
    {
        if (int.TryParse(delta, out int d))
            Setpoint = Math.Clamp(Setpoint + d, SetpointMin, SetpointMax);
    }

    [RelayCommand]
    private void ApplyPreset(int value) => Setpoint = Math.Clamp(value, SetpointMin, SetpointMax);

    /// <summary>Stores the current setpoint as a new quick preset.</summary>
    [RelayCommand]
    private void AddPreset()
    {
        if (!Presets.Contains(Setpoint) && Presets.Count < 10)
        {
            var sorted = Presets.Append(Setpoint).OrderBy(x => x).ToList();
            Presets.Clear();
            foreach (var p in sorted) Presets.Add(p);
        }
    }

    [RelayCommand]
    private void RemovePreset(int value) => Presets.Remove(value);

    [RelayCommand]
    private Task SetMode(string mode) => RunAsync(() => _client.SetModeAsync(mode), null);

    [RelayCommand]
    private Task ClearErrors() => RunAsync(() => _client.ClearErrorsAsync(), null);

    [RelayCommand]
    private Task Beep() => RunAsync(() => _client.BeepAsync(), null);
}
