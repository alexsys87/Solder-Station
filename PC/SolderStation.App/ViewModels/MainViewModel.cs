using System.Collections.ObjectModel;
using System.IO;
using System.Windows;
using System.Windows.Threading;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using SolderStation.Models;
using SolderStation.Services;

namespace SolderStation.ViewModels;

/// <summary>Main window: connection, live status and quick controls.</summary>
public sealed partial class MainViewModel : ObservableObject
{
    private readonly StationClient _client = new();
    private readonly Dispatcher _ui;
    private readonly AppSettings _settings;
    private readonly DispatcherTimer _reconnectTimer;
    private readonly DispatcherTimer _changeTimer;
    private bool _userDisconnected = true;
    private bool _connecting;

    public MainViewModel()
    {
        _ui = Application.Current.Dispatcher;
        _settings = AppSettingsStore.Load();

        BaudRate = _settings.BaudRate;
        IsDarkTheme = _settings.DarkTheme;
        LanguageCode = Loc.Language;
        Loc.Changed += OnLanguageChanged;
        AutoReconnect = _settings.AutoReconnect;
        AutoBackup = _settings.AutoBackup;
        Presets = new ObservableCollection<int>(_settings.Presets);

        _reconnectTimer = new DispatcherTimer { Interval = TimeSpan.FromSeconds(2) };
        _reconnectTimer.Tick += async (_, _) => await TryReconnectAsync();

        // "!C": settings changed with the encoder - reload once things calm down
        _changeTimer = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(600) };
        _changeTimer.Tick += async (_, _) =>
        {
            _changeTimer.Stop();
            await RunAsync(ReloadSettingsAsync, null, quiet: true);
        };

        _client.StatusReceived += s => _ui.BeginInvoke(() => ApplyStatus(s));
        _client.DeviceSettingsChanged += () => _ui.BeginInvoke(() => { _changeTimer.Stop(); _changeTimer.Start(); });
        _client.LineLogged += (line, tx) => _ui.BeginInvoke(() => AddConsoleLine(line, tx));
        _client.ConnectionLost += reason => _ui.BeginInvoke(() => OnConnectionLost(reason));

        RefreshPorts();
    }

    public double WindowWidth
    {
        get => _settings.WindowWidth;
        set => _settings.WindowWidth = value;
    }

    public double WindowHeight
    {
        get => _settings.WindowHeight;
        set => _settings.WindowHeight = value;
    }

    // ------------------------------------------------------------------
    // Connection
    // ------------------------------------------------------------------
    public ObservableCollection<PortInfo> Ports { get; } = new();
    public int[] BaudRates { get; } = { 9600, 19200, 38400, 57600, 115200, 230400 };

    [ObservableProperty] private PortInfo? _selectedPort;
    [ObservableProperty] private int _baudRate;

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(IsDisconnected))]
    [NotifyCanExecuteChangedFor(nameof(ConnectCommand), nameof(DisconnectCommand))]
    private bool _isConnected;

    public bool IsDisconnected => !IsConnected;

    partial void OnIsConnectedChanged(bool value)
    {
        AddTipCommand.NotifyCanExecuteChanged();
        ApplyTipCommand.NotifyCanExecuteChanged();
        DeleteTipCommand.NotifyCanExecuteChanged();
        ActivateTipCommand.NotifyCanExecuteChanged();
        RevertTipCommand.NotifyCanExecuteChanged();
        CalibrateTipCommand.NotifyCanExecuteChanged();
        ResetTipCalibrationCommand.NotifyCanExecuteChanged();
    }

    [ObservableProperty] private bool _isBusy;
    [ObservableProperty] private string _statusMessage = Loc.T("M.NotConnected");
    [ObservableProperty] private string _firmware = "";
    [ObservableProperty] private string _deviceId = "";
    [ObservableProperty] private string _deviceDetails = "";
    [ObservableProperty] private bool _autoReconnect;
    [ObservableProperty] private bool _autoBackup;
    [ObservableProperty] private bool _isDarkTheme;

    partial void OnIsDarkThemeChanged(bool value)
    {
        ThemeManager.Apply(value);
        _settings.DarkTheme = value;
    }

    // ------------------------------------------------------------------
    // Language
    // ------------------------------------------------------------------
    public IReadOnlyList<Loc.LanguageInfo> Languages => Loc.Languages;

    /// <summary>Selected UI language ("ru" / "en").</summary>
    [ObservableProperty] private string _languageCode = "ru";

    partial void OnLanguageCodeChanged(string value)
    {
        if (value == Loc.Language) return;
        _settings.Language = value;
        Loc.Apply(value);
    }

    /// <summary>Re-creates the texts produced in code for the new language.</summary>
    private void OnLanguageChanged()
    {
        if (_lastParams.Count > 0) BuildParamGroups(CurrentParamValues());
        if (_lastStatus != null) ApplyStatus(_lastStatus);
        StatusMessage = IsConnected
            ? Loc.F("M.Connected", _settings.LastPort ?? "", Firmware)
            : Loc.T("M.NotConnected");
        if (DeviceTime != "—") _ = RunAsync(ReadTimeAsync, null, quiet: true);
    }

    partial void OnAutoReconnectChanged(bool value) => _settings.AutoReconnect = value;
    partial void OnAutoBackupChanged(bool value) => _settings.AutoBackup = value;

    [RelayCommand]
    private void RefreshPorts()
    {
        string? keep = SelectedPort?.Name ?? _settings.LastPort;
        Ports.Clear();
        foreach (var p in PortScanner.Scan()) Ports.Add(p);
        SelectedPort = Ports.FirstOrDefault(p => p.Name == keep && p.IsStation)
                       ?? Ports.FirstOrDefault(p => p.IsStation)
                       ?? Ports.FirstOrDefault(p => p.Name == keep)
                       ?? Ports.FirstOrDefault();
    }

    /// <summary>Called by the window once it is shown.</summary>
    public async Task StartupAsync()
    {
        if (_settings.AutoConnect && SelectedPort != null
            && (SelectedPort.IsStation || SelectedPort.Name == _settings.LastPort))
        {
            await ConnectAsync();
        }
    }

    private bool CanConnect() => !IsConnected;
    private bool CanDisconnect() => IsConnected;

    [RelayCommand(CanExecute = nameof(CanConnect))]
    private async Task ConnectAsync()
    {
        if (SelectedPort == null)
        {
            StatusMessage = Loc.T("M.SelectPort");
            return;
        }
        await RunAsync(() => OpenAsync(SelectedPort.Name, BaudRate), Loc.F("M.Connecting", SelectedPort.Name));
    }

    [RelayCommand(CanExecute = nameof(CanDisconnect))]
    private async Task DisconnectAsync()
    {
        _userDisconnected = true;
        _reconnectTimer.Stop();
        try { await _client.StreamAsync(0); } catch { /* port may be gone */ }
        _client.Close();
        IsConnected = false;
        StatusMessage = Loc.T("M.Disconnected");
    }

    /// <summary>Tries every port and stays connected to the first station found.</summary>
    [RelayCommand]
    private async Task AutoFindAsync()
    {
        if (IsConnected) await DisconnectAsync();
        RefreshPorts();
        await RunAsync(async () =>
        {
            foreach (var port in Ports.ToList())
            {
                StatusMessage = Loc.F("M.Searching", port.Name);
                try
                {
                    _client.Open(port.Name, BaudRate);
                    await _client.PingAsync(700);
                    _client.Close();
                    SelectedPort = port;
                    await OpenAsync(port.Name, BaudRate);
                    return;
                }
                catch (Exception)
                {
                    _client.Close();
                }
            }
            StatusMessage = Loc.T("M.NotFound");
        }, Loc.T("M.SearchingStation"));
    }

    private async Task OpenAsync(string portName, int baud)
    {
        if (_connecting) return;
        _connecting = true;
        try
        {
            _client.Open(portName, baud);
            try
            {
                Firmware = await _client.PingAsync(1500);
            }
            catch (TimeoutException)
            {
                // the USB port may need a moment after enumeration
                await Task.Delay(300);
                Firmware = await _client.PingAsync(1500);
            }

            _userDisconnected = false;
            _settings.LastPort = portName;
            _settings.BaudRate = baud;
            IsConnected = true;

            await ReloadAllAsync();
            await _client.StreamAsync(_settings.StreamIntervalMs);
            StatusMessage = Loc.F("M.Connected", portName, Firmware);

            if (AutoBackup)
            {
                var file = await SettingsTransfer.ReadFromDeviceAsync(_client);
                if (SettingsTransfer.Backup(file) is { } path)
                    StatusMessage += Loc.F("M.BackupSaved", Path.GetFileName(path));
            }
        }
        catch
        {
            _client.Close();
            IsConnected = false;
            throw;
        }
        finally
        {
            _connecting = false;
        }
    }

    private void OnConnectionLost(string reason)
    {
        _client.Close();
        IsConnected = false;
        StatusMessage = Loc.F("M.ConnectionLost", reason);
        if (AutoReconnect && !_userDisconnected) _reconnectTimer.Start();
    }

    private async Task TryReconnectAsync()
    {
        if (IsConnected || _connecting || _userDisconnected)
        {
            _reconnectTimer.Stop();
            return;
        }
        var name = _settings.LastPort;
        if (name == null || !System.IO.Ports.SerialPort.GetPortNames().Contains(name)) return;
        try
        {
            await OpenAsync(name, _settings.BaudRate);
            _reconnectTimer.Stop();
            RefreshPorts();
        }
        catch (Exception)
        {
            // try again on the next tick
        }
    }

    private async Task ReloadAllAsync()
    {
        var info = await _client.GetInfoAsync();
        DeviceId = info.GetValueOrDefault("uid", "");
        DeviceDetails = string.Join("   ", info.Select(kv => $"{kv.Key}: {kv.Value}"));
        await ReloadSettingsAsync();
        ApplyStatus(await _client.GetStatusAsync());
        await ReadTimeAsync();
    }

    private async Task ReloadSettingsAsync()
    {
        if (!IsConnected) return;
        await LoadParamsAsync();
        await LoadTipsAsync();
    }

    /// <summary>Runs a device operation with busy indicator and error reporting.</summary>
    private async Task RunAsync(Func<Task> action, string? busyText, bool quiet = false)
    {
        if (busyText != null)
        {
            IsBusy = true;
            StatusMessage = busyText;
        }
        try
        {
            await action();
        }
        catch (StationException ex)
        {
            StatusMessage = Loc.F("M.StationError", ex.Message);
            if (!quiet) Dialogs.Error(StatusMessage);
        }
        catch (Exception ex)
        {
            StatusMessage = Loc.F("M.Error", ex.Message);
            if (!quiet && busyText != null) Dialogs.Error(StatusMessage);
        }
        finally
        {
            if (busyText != null) IsBusy = false;
        }
    }

    /// <summary>Saves preferences and closes the port (window closing).</summary>
    public void Shutdown()
    {
        _reconnectTimer.Stop();
        _settings.BaudRate = BaudRate;
        _settings.Presets = Presets.ToList();
        AppSettingsStore.Save(_settings);
        if (IsConnected)
        {
            try { _client.StreamAsync(0).Wait(300); } catch { /* ignore */ }
        }
        _client.Close();
    }
}
