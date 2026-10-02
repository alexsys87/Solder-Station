using System.Windows;
using SolderStation.ViewModels;

namespace SolderStation.Views;

public partial class MainWindow : Window
{
    private readonly MainViewModel _vm;

    public MainWindow()
    {
        InitializeComponent();
        _vm = new MainViewModel();
        DataContext = _vm;
        Loaded += async (_, _) => await _vm.StartupAsync();
        Closing += (_, _) => _vm.Shutdown();
    }
}
