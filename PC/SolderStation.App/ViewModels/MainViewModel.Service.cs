using System.Collections.ObjectModel;
using System.Diagnostics;
using System.IO;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using SolderStation.Services;

namespace SolderStation.ViewModels;

/// <summary>Service page: clock, import / export, reset, bootloader, console.</summary>
public sealed partial class MainViewModel
{
    private const int ConsoleLimit = 600;

    // ------------------------------------------------------------------
    // Clock
    // ------------------------------------------------------------------
    [ObservableProperty] private string _deviceTime = "—";

    [RelayCommand]
    private Task ReadTime() => RunAsync(ReadTimeAsync, null);

    private async Task ReadTimeAsync()
    {
        var t = await _client.GetTimeAsync();
        DeviceTime = t?.ToString("G", Loc.Culture) ?? "—";
    }

    [RelayCommand]
    private Task SyncTime() => RunAsync(async () =>
    {
        await _client.SetTimeAsync(DateTime.Now);
        await ReadTimeAsync();
        StatusMessage = Loc.T("M.ClockSynced");
    }, null);

    // ------------------------------------------------------------------
    // Import / export
    // ------------------------------------------------------------------
    [RelayCommand]
    private Task Export() => RunAsync(async () =>
    {
        var file = await SettingsTransfer.ReadFromDeviceAsync(_client);
        var path = Dialogs.SaveJson($"T12_settings_{DateTime.Now:yyyyMMdd_HHmm}.json");
        if (path == null) return;
        SettingsTransfer.SaveToFile(file, path);
        StatusMessage = Loc.F("M.Exported", path);
    }, Loc.T("M.ReadingStation"));

    [RelayCommand]
    private Task Import() => ImportFromAsync(null);

    [RelayCommand]
    private Task RestoreBackup() => ImportFromAsync(AppSettingsStore.BackupFolder);

    private async Task ImportFromAsync(string? folder)
    {
        if (folder != null) Directory.CreateDirectory(folder);
        var path = Dialogs.OpenJson(folder);
        if (path == null) return;

        Models.SettingsFile file;
        try
        {
            file = SettingsTransfer.LoadFromFile(path);
        }
        catch (Exception ex)
        {
            Dialogs.Error(Loc.F("M.FileReadError", ex.Message));
            return;
        }

        var tips = Dialogs.Ask(
            Loc.F("M.ImportAsk", file.Exported, file.Firmware, file.Parameters.Count, file.Tips.Count));
        if (tips == null) return;

        await RunAsync(async () =>
        {
            var progress = new Progress<string>(s => StatusMessage = Loc.F("M.ImportProgress", s));
            var warnings = await SettingsTransfer.WriteToDeviceAsync(_client, file, true, tips.Value, progress);
            await ReloadSettingsAsync();
            StatusMessage = Loc.T("M.ImportDone");
            if (warnings.Count > 0) Dialogs.Info(Loc.F("M.ImportWarnings", string.Join("\n", warnings)));
        }, Loc.T("M.Importing"));
    }

    [RelayCommand]
    private void OpenBackupFolder()
    {
        Directory.CreateDirectory(AppSettingsStore.BackupFolder);
        Process.Start(new ProcessStartInfo { FileName = AppSettingsStore.BackupFolder, UseShellExecute = true });
    }

    // ------------------------------------------------------------------
    // Device
    // ------------------------------------------------------------------
    [RelayCommand]
    private Task RebootDevice()
    {
        if (!Dialogs.Confirm(Loc.T("M.RebootConfirm"))) return Task.CompletedTask;
        return RunAsync(async () =>
        {
            await _client.ResetAsync();
            StatusMessage = Loc.T("M.Rebooting");
        }, null);
    }

    [RelayCommand]
    private Task EnterBootloader()
    {
        if (!Dialogs.Confirm(Loc.T("M.DfuConfirm")))
            return Task.CompletedTask;

        return RunAsync(async () =>
        {
            _userDisconnected = true;
            await _client.BootloaderAsync();
            _client.Close();
            IsConnected = false;
            StatusMessage = Loc.T("M.DfuDone");
        }, null);
    }

    // ------------------------------------------------------------------
    // Console
    // ------------------------------------------------------------------
    public ObservableCollection<string> ConsoleLines { get; } = new();

    [ObservableProperty] private string _consoleInput = "";
    [ObservableProperty] private bool _consoleShowStream;

    private void AddConsoleLine(string line, bool tx)
    {
        if (!tx && !ConsoleShowStream && line.StartsWith("!S", StringComparison.Ordinal)) return;
        ConsoleLines.Add($"{DateTime.Now:HH:mm:ss.fff} {(tx ? "→" : "←")} {line}");
        while (ConsoleLines.Count > ConsoleLimit) ConsoleLines.RemoveAt(0);
    }

    [RelayCommand]
    private async Task SendConsole()
    {
        var cmd = ConsoleInput.Trim();
        if (cmd.Length == 0) return;
        ConsoleInput = "";
        try
        {
            await _client.SendAsync(cmd, 3000);
        }
        catch (Exception ex)
        {
            ConsoleLines.Add("!! " + ex.Message);
        }
    }

    [RelayCommand]
    private void ClearConsole() => ConsoleLines.Clear();
}
