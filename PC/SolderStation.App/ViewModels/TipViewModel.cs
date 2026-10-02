using CommunityToolkit.Mvvm.ComponentModel;
using SolderStation.Models;
using SolderStation.Services;

namespace SolderStation.ViewModels;

/// <summary>Editable tip profile. PID gains are shown as decimals (raw / 1000).</summary>
public sealed partial class TipViewModel : ObservableObject
{
    private bool _loading;

    public TipViewModel(TipData data)
    {
        Load(data);
    }

    public TipData Original { get; private set; } = new();
    public int Index => Original.Index;

    [ObservableProperty] private string _name = "";
    [ObservableProperty] private int _setpoint;
    [ObservableProperty] private double _kp;
    [ObservableProperty] private double _ki;
    [ObservableProperty] private double _kd;
    [ObservableProperty] private int _adc1;
    [ObservableProperty] private int _adc2;
    [ObservableProperty] private int _adc3;
    [ObservableProperty] private int _dt1;
    [ObservableProperty] private int _dt2;
    [ObservableProperty] private int _dt3;

    [ObservableProperty] private bool _isActive;
    [ObservableProperty] private bool _isDirty;

    public string Summary => $"{Setpoint} °C   Kp {Kp:0.000}";

    public void Load(TipData data)
    {
        _loading = true;
        Original = data;
        Name = data.Name;
        Setpoint = data.Setpoint;
        Kp = data.Kp / 1000.0;
        Ki = data.Ki / 1000.0;
        Kd = data.Kd / 1000.0;
        Adc1 = data.CalAdc[0]; Adc2 = data.CalAdc[1]; Adc3 = data.CalAdc[2];
        Dt1 = data.CalDt[0]; Dt2 = data.CalDt[1]; Dt3 = data.CalDt[2];
        IsDirty = false;
        _loading = false;
        OnPropertyChanged(nameof(Index));
        OnPropertyChanged(nameof(Summary));
    }

    public TipData ToData() => new()
    {
        Index = Original.Index,
        Name = (Name ?? "").Trim().ToUpperInvariant(),
        Setpoint = Setpoint,
        Kp = (int)Math.Round(Kp * 1000),
        Ki = (int)Math.Round(Ki * 1000),
        Kd = (int)Math.Round(Kd * 1000),
        CalAdc = new[] { Adc1, Adc2, Adc3 },
        CalDt = new[] { Dt1, Dt2, Dt3 },
    };

    /// <summary>Checks the values before writing them to the station.</summary>
    public string? Validate()
    {
        var d = ToData();
        if (string.IsNullOrWhiteSpace(d.Name)) return Loc.T("V.NameEmpty");
        if (d.Name.Length > 8) return Loc.T("V.NameLong");
        if (d.Kp is < 0 or > 2000 || d.Ki is < 0 or > 2000 || d.Kd is < 0 or > 2000)
            return Loc.T("V.Pid");
        if (!(d.CalAdc[0] < d.CalAdc[1] && d.CalAdc[1] < d.CalAdc[2]))
            return Loc.T("V.Adc");
        if (!(d.CalDt[0] > 0 && d.CalDt[0] < d.CalDt[1] && d.CalDt[1] < d.CalDt[2]))
            return Loc.T("V.Dt");
        return null;
    }

    protected override void OnPropertyChanged(System.ComponentModel.PropertyChangedEventArgs e)
    {
        base.OnPropertyChanged(e);
        if (_loading) return;
        if (e.PropertyName is nameof(IsDirty) or nameof(IsActive) or nameof(Summary)) return;
        IsDirty = true;
        if (e.PropertyName is nameof(Setpoint) or nameof(Kp)) OnPropertyChanged(nameof(Summary));
    }
}
