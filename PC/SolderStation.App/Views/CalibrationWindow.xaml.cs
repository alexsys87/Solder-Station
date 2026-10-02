using System.Windows;
using SolderStation.ViewModels;

namespace SolderStation.Views;

public partial class CalibrationWindow : Window
{
    public CalibrationWindow(CalibrationViewModel vm)
    {
        InitializeComponent();
        DataContext = vm;
        vm.CloseRequested += Close;
        Loaded += async (_, _) => await vm.StartAsync();
        Closing += async (_, _) => await vm.StopAsync();
    }
}
