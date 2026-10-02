using System.Windows.Threading;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using SolderStation.Services;

namespace SolderStation.ViewModels;

/// <summary>
/// Remote calibration: the station heats the tip to 250 / 350 / 450 °C,
/// the user measures the real temperature with a tip thermometer and
/// enters it here. The station screen shows the same wizard.
/// </summary>
public sealed partial class CalibrationViewModel : ObservableObject
{
    private readonly StationClient _client;
    private readonly DispatcherTimer _timer;
    private bool _polling;

    public CalibrationViewModel(StationClient client, string tipName)
    {
        _client = client;
        TipName = tipName;
        _timer = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(500) };
        _timer.Tick += async (_, _) => await PollAsync();
    }

    public event Action? CloseRequested;

    public string TipName { get; }

    [ObservableProperty] private int _step;
    [ObservableProperty] private int _target;
    [ObservableProperty] private double _tipTemp;
    [ObservableProperty] private bool _isStable;
    [ObservableProperty] private bool _isRunning;
    [ObservableProperty] private bool _isDone;
    [ObservableProperty] private bool _isOk;
    [ObservableProperty] private int _measured;
    [ObservableProperty] private string _message = Loc.T("C.Preparing");

    public string StepText => IsDone ? Loc.T("C.Done") : Loc.F("C.Step", Step + 1);
    public string TargetText => Loc.F("C.Target", Target);
    public bool CanRecord => IsRunning && !IsDone;

    partial void OnStepChanged(int value) => OnPropertyChanged(nameof(StepText));
    partial void OnTargetChanged(int value) => OnPropertyChanged(nameof(TargetText));
    partial void OnIsDoneChanged(bool value)
    {
        OnPropertyChanged(nameof(StepText));
        OnPropertyChanged(nameof(CanRecord));
    }
    partial void OnIsRunningChanged(bool value) => OnPropertyChanged(nameof(CanRecord));

    public async Task StartAsync()
    {
        try
        {
            await _client.CalibrationStartAsync();
            IsRunning = true;
            Message = Loc.T("C.Heating");
            await PollAsync();
            Measured = Target;
            _timer.Start();
        }
        catch (Exception ex)
        {
            Message = Loc.F("C.StartFailed", ex.Message);
        }
    }

    private async Task PollAsync()
    {
        if (_polling) return;
        _polling = true;
        try
        {
            var s = await _client.GetCalibrationStateAsync();
            int oldStep = Step;
            Step = Math.Min(s.Step, 2);
            Target = s.Target;
            TipTemp = s.TipTemp;
            IsStable = s.Stable;
            if (s.Step != oldStep && !s.Done) Measured = s.Target;

            if (s.Done)
            {
                IsDone = true;
                IsOk = s.Ok;
                IsRunning = false;
                _timer.Stop();
                Message = s.Ok
                    ? Loc.T("C.Saved")
                    : Loc.T("C.Rejected");
            }
            else if (!s.Active && IsRunning)
            {
                IsRunning = false;
                _timer.Stop();
                Message = Loc.T("C.Aborted");
            }
        }
        catch (Exception ex)
        {
            Message = Loc.F("C.CommError", ex.Message);
        }
        finally
        {
            _polling = false;
        }
    }

    [RelayCommand]
    private async Task RecordPointAsync()
    {
        if (Measured < 50 || Measured > 600)
        {
            Message = Loc.T("C.Range");
            return;
        }
        try
        {
            await _client.CalibrationPointAsync(Measured);
            Message = Loc.T(Step < 2 ? "C.PointNext" : "C.PointRecorded");
            await PollAsync();
        }
        catch (Exception ex)
        {
            Message = Loc.F("C.PointFailed", ex.Message);
        }
    }

    [RelayCommand]
    private void AdjustMeasured(string delta)
    {
        if (int.TryParse(delta, out int d)) Measured = Math.Clamp(Measured + d, 50, 600);
    }

    [RelayCommand]
    private async Task CloseAsync()
    {
        await StopAsync();
        CloseRequested?.Invoke();
    }

    /// <summary>Aborts a running calibration (window closed).</summary>
    public async Task StopAsync()
    {
        _timer.Stop();
        if (IsRunning && !IsDone)
        {
            IsRunning = false;
            try { await _client.CalibrationAbortAsync(); } catch { /* connection lost */ }
        }
    }
}
