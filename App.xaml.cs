using System.Windows;
using System.Threading;

using Application = System.Windows.Application;

namespace ZapretHub;

public partial class App : Application
{
    private static Mutex? _mutex;

    protected override void OnStartup(StartupEventArgs e)
    {
        _mutex = new Mutex(true, "ZapretHub_SingleInstance", out bool isNewInstance);
        if (!isNewInstance)
        {
            System.Windows.MessageBox.Show("Приложение ZapretHub уже запущено!", "ZapretHub",
                MessageBoxButton.OK, MessageBoxImage.Information);
            Shutdown();
            return;
        }
        base.OnStartup(e);
    }
}
