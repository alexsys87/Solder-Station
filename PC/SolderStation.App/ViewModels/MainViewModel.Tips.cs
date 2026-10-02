using System.Collections.ObjectModel;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using SolderStation.Services;
using SolderStation.Views;

namespace SolderStation.ViewModels;

/// <summary>Tip profiles: selection, editing, calibration.</summary>
public sealed partial class MainViewModel
{
    private bool _suppressTipSelect;

    public ObservableCollection<TipViewModel> Tips { get; } = new();

    [ObservableProperty]
    [NotifyCanExecuteChangedFor(nameof(ApplyTipCommand), nameof(DeleteTipCommand),
                                nameof(CalibrateTipCommand), nameof(ResetTipCalibrationCommand),
                                nameof(RevertTipCommand), nameof(ActivateTipCommand))]
    private TipViewModel? _selectedTip;

    /// <summary>Index of the active tip, bound to the quick selector.</summary>
    [ObservableProperty] private int _activeTipIndex = -1;

    [ObservableProperty] private int _maxTips = 10;

    partial void OnActiveTipIndexChanged(int value)
    {
        for (int i = 0; i < Tips.Count; i++) Tips[i].IsActive = i == value;
        if (_suppressTipSelect || value < 0 || !IsConnected) return;
        _ = RunAsync(async () =>
        {
            await _client.SelectTipAsync(value);
            ApplyStatus(await _client.GetStatusAsync());
            StatusMessage = $"Активное жало: {Tips[value].Name}";
        }, null);
    }

    private void SetActiveTipFromDevice(int index)
    {
        _suppressTipSelect = true;
        ActiveTipIndex = index;
        _suppressTipSelect = false;
    }

    private async Task LoadTipsAsync()
    {
        var list = await _client.GetTipsAsync();
        int selected = SelectedTip?.Index ?? list.Active;

        // keep an unsaved edit of the selected tip
        if (SelectedTip is { IsDirty: true } && SelectedTip.Index < list.Tips.Count
            && list.Tips.Count == Tips.Count)
        {
            for (int i = 0; i < list.Tips.Count; i++)
            {
                if (Tips[i] != SelectedTip) Tips[i].Load(list.Tips[i]);
            }
        }
        else
        {
            Tips.Clear();
            foreach (var t in list.Tips) Tips.Add(new TipViewModel(t));
            SelectedTip = Tips.ElementAtOrDefault(Math.Min(selected, Tips.Count - 1));
        }

        MaxTips = list.Max;
        SetActiveTipFromDevice(list.Active);
        AddTipCommand.NotifyCanExecuteChanged();
    }

    private bool HasSelectedTip() => SelectedTip != null && IsConnected;
    private bool CanAddTip() => IsConnected && Tips.Count < MaxTips;

    [RelayCommand(CanExecute = nameof(HasSelectedTip))]
    private Task ApplyTip() => RunAsync(async () =>
    {
        var tip = SelectedTip!;
        if (tip.Validate() is { } error)
        {
            Dialogs.Error(error);
            return;
        }
        var data = tip.ToData();
        var old = tip.Original;
        if (data.Name != old.Name) await _client.RenameTipAsync(tip.Index, data.Name);
        if (data.Setpoint != old.Setpoint) await _client.SetTipFieldAsync(tip.Index, "set", data.Setpoint);
        if (data.Kp != old.Kp) await _client.SetTipFieldAsync(tip.Index, "kp", data.Kp);
        if (data.Ki != old.Ki) await _client.SetTipFieldAsync(tip.Index, "ki", data.Ki);
        if (data.Kd != old.Kd) await _client.SetTipFieldAsync(tip.Index, "kd", data.Kd);
        for (int i = 0; i < 3; i++)
        {
            if (data.CalAdc[i] != old.CalAdc[i]) await _client.SetTipFieldAsync(tip.Index, $"adc{i + 1}", data.CalAdc[i]);
            if (data.CalDt[i] != old.CalDt[i]) await _client.SetTipFieldAsync(tip.Index, $"dt{i + 1}", data.CalDt[i]);
        }
        tip.IsDirty = false;
        await LoadTipsAsync();
        StatusMessage = $"Жало «{data.Name}» сохранено";
    }, "Запись профиля жала…");

    [RelayCommand(CanExecute = nameof(HasSelectedTip))]
    private Task RevertTip() => RunAsync(async () =>
    {
        if (SelectedTip != null) SelectedTip.IsDirty = false;
        await LoadTipsAsync();
    }, null);

    [RelayCommand(CanExecute = nameof(HasSelectedTip))]
    private void ActivateTip()
    {
        if (SelectedTip != null) ActiveTipIndex = SelectedTip.Index;
    }

    [RelayCommand(CanExecute = nameof(CanAddTip))]
    private Task AddTip() => RunAsync(async () =>
    {
        int index = await _client.AddTipAsync($"TIP{Tips.Count + 1}");
        await LoadTipsAsync();
        SelectedTip = Tips.ElementAtOrDefault(index);
        StatusMessage = "Добавлен профиль жала — задайте имя и откалибруйте его";
    }, "Добавление жала…");

    [RelayCommand(CanExecute = nameof(HasSelectedTip))]
    private Task DeleteTip()
    {
        var tip = SelectedTip;
        if (tip == null) return Task.CompletedTask;
        if (Tips.Count <= 1)
        {
            Dialogs.Error("Нельзя удалить последний профиль жала");
            return Task.CompletedTask;
        }
        if (!Dialogs.Confirm($"Удалить профиль жала «{tip.Name}»?")) return Task.CompletedTask;

        return RunAsync(async () =>
        {
            await _client.DeleteTipAsync(tip.Index);
            SelectedTip = null;
            await LoadTipsAsync();
            StatusMessage = $"Профиль «{tip.Name}» удалён";
        }, "Удаление…");
    }

    [RelayCommand(CanExecute = nameof(HasSelectedTip))]
    private Task ResetTipCalibration()
    {
        var tip = SelectedTip;
        if (tip == null || !Dialogs.Confirm($"Сбросить калибровку жала «{tip.Name}» к значениям по умолчанию?"))
            return Task.CompletedTask;
        return RunAsync(async () =>
        {
            await _client.ResetTipCalibrationAsync(tip.Index);
            tip.IsDirty = false;
            await LoadTipsAsync();
        }, "Сброс калибровки…");
    }

    [RelayCommand(CanExecute = nameof(HasSelectedTip))]
    private async Task CalibrateTipAsync()
    {
        var tip = SelectedTip;
        if (tip == null) return;
        if (!Dialogs.Confirm($"Калибровка жала «{tip.Name}».\n\n" +
                             "Жало будет нагрето до 250, 350 и 450 °C. Понадобится термометр для жал " +
                             "(например Hakko FG-100). Продолжить?"))
            return;

        await RunAsync(async () =>
        {
            if (ActiveTipIndex != tip.Index)
            {
                await _client.SelectTipAsync(tip.Index);
                SetActiveTipFromDevice(tip.Index);
            }
        }, null);

        var vm = new CalibrationViewModel(_client, tip.Name);
        var window = new CalibrationWindow(vm) { Owner = System.Windows.Application.Current.MainWindow };
        window.ShowDialog();

        await RunAsync(LoadTipsAsync, null, quiet: true);
    }
}
