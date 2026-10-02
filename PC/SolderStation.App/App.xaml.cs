using System.Windows;
using System.Windows.Threading;
using SolderStation.Services;

namespace SolderStation;

public partial class App : Application
{
    protected override void OnStartup(StartupEventArgs e)
    {
        base.OnStartup(e);
        var settings = AppSettingsStore.Load();
        ThemeManager.Apply(settings.DarkTheme);
        Loc.Apply(settings.Language);
        DispatcherUnhandledException += OnUnhandledException;
    }

    private static void OnUnhandledException(object sender, DispatcherUnhandledExceptionEventArgs e)
    {
        Dialogs.Error(Loc.F("M.Unexpected", e.Exception.Message));
        e.Handled = true;
    }
}
