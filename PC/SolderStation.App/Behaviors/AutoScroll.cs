using System.Collections.Specialized;
using System.Windows;
using System.Windows.Controls;

namespace SolderStation.Behaviors;

/// <summary>Keeps a ListBox scrolled to its last item (console log).</summary>
public static class AutoScroll
{
    public static readonly DependencyProperty EnabledProperty = DependencyProperty.RegisterAttached(
        "Enabled", typeof(bool), typeof(AutoScroll), new PropertyMetadata(false, OnEnabledChanged));

    public static bool GetEnabled(DependencyObject obj) => (bool)obj.GetValue(EnabledProperty);

    public static void SetEnabled(DependencyObject obj, bool value) => obj.SetValue(EnabledProperty, value);

    private static void OnEnabledChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
    {
        if (d is not ListBox list || e.NewValue is not true) return;

        list.Loaded += (_, _) =>
        {
            if (list.ItemsSource is INotifyCollectionChanged source)
            {
                source.CollectionChanged += (_, args) =>
                {
                    if (args.Action == NotifyCollectionChangedAction.Add && list.Items.Count > 0)
                    {
                        list.ScrollIntoView(list.Items[^1]);
                    }
                };
            }
        };
    }
}
