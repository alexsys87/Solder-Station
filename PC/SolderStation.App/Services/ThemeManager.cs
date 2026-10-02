using System.Windows;

namespace SolderStation.Services;

/// <summary>Switches between the light and the dark color dictionaries.</summary>
public static class ThemeManager
{
    private static readonly Uri LightUri = new("Themes/Light.xaml", UriKind.Relative);
    private static readonly Uri DarkUri = new("Themes/Dark.xaml", UriKind.Relative);

    public static bool IsDark { get; private set; }

    public static void Apply(bool dark)
    {
        var dictionaries = Application.Current.Resources.MergedDictionaries;
        var colors = new ResourceDictionary { Source = dark ? DarkUri : LightUri };

        // the color dictionary is always the first one (see App.xaml)
        if (dictionaries.Count > 0) dictionaries[0] = colors;
        else dictionaries.Add(colors);
        IsDark = dark;
    }
}
