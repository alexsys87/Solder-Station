using System.Windows;
using System.Windows.Threading;
using SolderStation.Services;

namespace SolderStation;

public partial class App : Application
{
    protected override void OnStartup(StartupEventArgs e)
    {
        base.OnStartup(e);
        ThemeManager.Apply(AppSettingsStore.Load().DarkTheme);
        DispatcherUnhandledException += OnUnhandledException;
    }

    private static void OnUnhandledException(object sender, DispatcherUnhandledExceptionEventArgs e)
    {
        Dialogs.Error("Непредвиденная ошибка: " + e.Exception.Message);
        e.Handled = true;
    }
}
