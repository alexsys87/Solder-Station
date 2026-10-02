using System.Collections.ObjectModel;
using CommunityToolkit.Mvvm.Input;
using SolderStation.Models;
using SolderStation.Services;

namespace SolderStation.ViewModels;

/// <summary>Settings page: all station parameters grouped.</summary>
public sealed partial class MainViewModel
{
    public ObservableCollection<ParamGroupViewModel> ParamGroups { get; } = new();

    private readonly Dictionary<string, ParamViewModel> _params = new(StringComparer.OrdinalIgnoreCase);

    private async Task LoadParamsAsync()
    {
        var values = await _client.GetParamsAsync();

        // same set of keys: just refresh the values (keeps scroll position)
        if (_params.Count == values.Count && values.All(v => _params.ContainsKey(v.Key)))
        {
            foreach (var v in values) _params[v.Key].SetFromDevice(v.Value);
        }
        else
        {
            _params.Clear();
            ParamGroups.Clear();
            var items = values
                .Select(v => new ParamViewModel(ParamCatalog.Get(v.Key), v, SendParamAsync))
                .ToList();
            foreach (var p in items) _params[p.Key] = p;

            foreach (var group in ParamCatalog.GroupOrder)
            {
                var inGroup = items.Where(p => p.Def.Group == group)
                                   .OrderBy(p => ParamCatalog.Order(p.Key))
                                   .ToList();
                if (inGroup.Count > 0) ParamGroups.Add(new ParamGroupViewModel(group, inGroup));
            }
        }
        UpdateSetpointLimits();
    }

    private void UpdateSetpointLimits()
    {
        if (_params.TryGetValue("temp_min", out var min)) SetpointMin = min.RawValue;
        if (_params.TryGetValue("temp_max", out var max)) SetpointMax = max.RawValue;
    }

    private async Task SendParamAsync(ParamViewModel p)
    {
        if (!IsConnected) return;
        try
        {
            await _client.SetParamAsync(p.Key, p.RawValue);
            StatusMessage = $"{p.Title}: {p.DisplayText} (сохранится автоматически)";
            if (p.Key is "temp_min" or "temp_max") UpdateSetpointLimits();
        }
        catch (Exception ex)
        {
            StatusMessage = $"{p.Title}: {ex.Message}";
            // show what the station really has
            try
            {
                var v = (await _client.GetParamsAsync()).FirstOrDefault(x => x.Key == p.Key);
                if (v != null)
                {
                    p.IsSending = false;
                    p.SetFromDevice(v.Value);
                }
            }
            catch
            {
                // connection problem is reported elsewhere
            }
        }
    }

    [RelayCommand]
    private Task ReloadParams() => RunAsync(ReloadSettingsAsync, "Чтение настроек…");

    [RelayCommand]
    private Task SaveToFlash() => RunAsync(async () =>
    {
        await _client.SaveAsync();
        StatusMessage = "Настройки записаны во флеш станции";
    }, "Сохранение…");

    [RelayCommand]
    private Task FactoryDefaults()
    {
        if (!Dialogs.Confirm("Сбросить все настройки и профили жал станции к заводским?\n" +
                             "Перед этим будет предложено сохранить резервную копию."))
            return Task.CompletedTask;

        return RunAsync(async () =>
        {
            var backup = await SettingsTransfer.ReadFromDeviceAsync(_client);
            var path = Dialogs.SaveJson($"T12_before_reset_{DateTime.Now:yyyyMMdd_HHmm}.json");
            if (path != null) SettingsTransfer.SaveToFile(backup, path);

            await _client.DefaultsAsync();
            await _client.SaveAsync();
            await ReloadSettingsAsync();
            StatusMessage = "Заводские настройки восстановлены";
        }, "Сброс настроек…");
    }
}
