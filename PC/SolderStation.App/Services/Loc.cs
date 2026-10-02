using System.Globalization;
using System.Windows;
using System.Windows.Markup;

namespace SolderStation.Services;

/// <summary>
/// Localization. Strings live in Lang/ru.xaml and Lang/en.xaml; XAML uses
/// them through DynamicResource, code through <see cref="T"/>. Switching the
/// language replaces the dictionary at run time, like the color themes.
/// </summary>
public static class Loc
{
    public sealed record LanguageInfo(string Code, string Name);

    public static IReadOnlyList<LanguageInfo> Languages { get; } = new[]
    {
        new LanguageInfo("ru", "Русский"),
        new LanguageInfo("en", "English"),
    };

    /// <summary>Index of the string dictionary in Application.Resources (see App.xaml).</summary>
    private const int DictionaryIndex = 2;

    public static string Language { get; private set; } = "ru";

    public static CultureInfo Culture { get; private set; } = new("ru-RU");

    /// <summary>Raised after the language was switched.</summary>
    public static event Action? Changed;

    /// <summary>Language of the operating system if it is supported, English otherwise.</summary>
    public static string DefaultLanguage =>
        CultureInfo.CurrentUICulture.TwoLetterISOLanguageName == "ru" ? "ru" : "en";

    public static void Apply(string? code)
    {
        code = Languages.Any(l => l.Code == code) ? code! : DefaultLanguage;

        var dict = new ResourceDictionary { Source = new Uri($"Lang/{code}.xaml", UriKind.Relative) };
        var merged = Application.Current.Resources.MergedDictionaries;
        if (merged.Count > DictionaryIndex) merged[DictionaryIndex] = dict;
        else merged.Add(dict);

        Language = code;
        Culture = new CultureInfo(code == "ru" ? "ru-RU" : "en-US");
        CultureInfo.CurrentCulture = Culture;
        CultureInfo.CurrentUICulture = Culture;
        CultureInfo.DefaultThreadCurrentCulture = Culture;
        CultureInfo.DefaultThreadCurrentUICulture = Culture;

        foreach (Window w in Application.Current.Windows) ApplyTo(w);
        Changed?.Invoke();
    }

    /// <summary>Number formats in bindings follow the selected language.</summary>
    public static void ApplyTo(FrameworkElement element) =>
        element.Language = XmlLanguage.GetLanguage(Culture.IetfLanguageTag);

    /// <summary>Localized string by key (the key itself if it is missing).</summary>
    public static string T(string key) =>
        Application.Current?.TryFindResource(key) as string ?? key;

    public static bool Has(string key) =>
        Application.Current?.TryFindResource(key) is string;

    /// <summary>Localized format string with arguments.</summary>
    public static string F(string key, params object?[] args) =>
        string.Format(Culture, T(key), args);
}
