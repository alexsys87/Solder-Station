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

    private IReadOnlyList<ParamValue> _lastParams = Array.Empty<ParamValue>();

    private async Task LoadParamsAsync()
    {
        var values = await _client.GetParamsAsync();
        _lastParams = values;

        // same set of keys: just refresh the values (keeps scroll position)
        if (_params.Count == values.Count && values.All(v => _params.ContainsKey(v.Key)))
        {
            foreach (var v in values) _params[v.Key].SetFromDevice(v.Value);
        }
        else
        {
            BuildParamGroups(values);
        }
        UpdateSetpointLimits();
    }

    /// <summary>Creates the settings page (also after a language switch).</summary>
    private void BuildParamGroups(IReadOnlyList<ParamValue> values)
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
            if (inGroup.Count > 0) ParamGroups.Add(new ParamGroupViewModel(Loc.T(group), inGroup));
        }
    }

    /// <summary>Current values in raw units (for the rebuild after a language switch).</summary>
    private IReadOnlyList<ParamValue> CurrentParamValues() =>
        _lastParams.Select(v => _params.TryGetValue(v.Key, out var p) ? v with { Value = p.RawValue } : v).ToList();

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
            StatusMessage = Loc.F("M.ParamSent", p.Title, p.DisplayText);
            if (p.Key is "temp_min" or "temp_max") UpdateSetpointLimits();
        }
        catch (Exception ex)
        {
            StatusMessage = Loc.F("M.ParamError", p.Title, ex.Message);
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
    private Task ReloadParams() => RunAsync(ReloadSettingsAsync, Loc.T("M.Reading"));

    [RelayCommand]
    private Task SaveToFlash() => RunAsync(async () =>
    {
        await _client.SaveAsync();
        StatusMessage = Loc.T("M.SavedFlash");
    }, Loc.T("M.Saving"));

    [RelayCommand]
    private Task FactoryDefaults()
    {
        if (!Dialogs.Confirm(Loc.T("M.ConfirmFactory")))
            return Task.CompletedTask;

        return RunAsync(async () =>
        {
            var backup = await SettingsTransfer.ReadFromDeviceAsync(_client);
            var path = Dialogs.SaveJson($"T12_before_reset_{DateTime.Now:yyyyMMdd_HHmm}.json");
            if (path != null) SettingsTransfer.SaveToFile(backup, path);

            await _client.DefaultsAsync();
            await _client.SaveAsync();
            await ReloadSettingsAsync();
            StatusMessage = Loc.T("M.FactoryDone");
        }, Loc.T("M.FactoryBusy"));
    }
}
