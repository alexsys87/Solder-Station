using System.Windows;
using SolderStation.Services;
using SolderStation.ViewModels;

namespace SolderStation.Views;

public partial class CalibrationWindow : Window
{
    public CalibrationWindow(CalibrationViewModel vm)
    {
        InitializeComponent();
        Loc.ApplyTo(this);
        DataContext = vm;
        vm.CloseRequested += Close;
        Loaded += async (_, _) => await vm.StartAsync();
        Closing += async (_, _) => await vm.StopAsync();
    }
}
