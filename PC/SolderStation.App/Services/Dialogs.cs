using System.Windows;
using Microsoft.Win32;

namespace SolderStation.Services;

/// <summary>Message boxes and file dialogs owned by the main window.</summary>
public static class Dialogs
{
    private const string Caption = "T12 Station";

    private static Window? Owner => Application.Current?.MainWindow;

    public static bool Confirm(string text) =>
        Show(text, MessageBoxButton.YesNo, MessageBoxImage.Question) == MessageBoxResult.Yes;

    /// <summary>Yes / No / Cancel: returns null on cancel.</summary>
    public static bool? Ask(string text) =>
        Show(text, MessageBoxButton.YesNoCancel, MessageBoxImage.Question) switch
        {
            MessageBoxResult.Yes => true,
            MessageBoxResult.No => false,
            _ => null,
        };

    public static void Info(string text) => Show(text, MessageBoxButton.OK, MessageBoxImage.Information);

    public static void Error(string text) => Show(text, MessageBoxButton.OK, MessageBoxImage.Warning);

    private static MessageBoxResult Show(string text, MessageBoxButton buttons, MessageBoxImage icon) =>
        Owner != null
            ? MessageBox.Show(Owner, text, Caption, buttons, icon)
            : MessageBox.Show(text, Caption, buttons, icon);

    public static string? SaveJson(string fileName)
    {
        var dlg = new SaveFileDialog
        {
            Filter = Loc.T("M.JsonFilter"),
            FileName = fileName,
            DefaultExt = ".json",
        };
        return dlg.ShowDialog(Owner) == true ? dlg.FileName : null;
    }

    public static string? OpenJson(string? folder = null)
    {
        var dlg = new OpenFileDialog
        {
            Filter = Loc.T("M.JsonFilter"),
            DefaultExt = ".json",
        };
        if (folder != null) dlg.InitialDirectory = folder;
        return dlg.ShowDialog(Owner) == true ? dlg.FileName : null;
    }
}
