using System;
using System.Diagnostics;
using System.IO;
using System.Threading.Tasks;

namespace ZapretHub.Services
{
    public static class VetoService
    {
        private static Process _process;
        private static bool _isRunning;

        public static event Action<bool> OnStateChanged;
        public static event Action<string> OnOutputReceived;
        public static event Action<string> OnErrorReceived;

        public static bool IsRunning => _isRunning;

        public static string GetVetoPath()
        {
            string appDir = AppDomain.CurrentDomain.BaseDirectory;
            string vetoPath = Path.Combine(appDir, "Veto", "veto.exe");
            if (File.Exists(vetoPath)) return vetoPath;

            vetoPath = Path.Combine(appDir, "veto.exe");
            if (File.Exists(vetoPath)) return vetoPath;

            string devPath = @"C:\Dev\Veto\bin\veto.exe";
            if (File.Exists(devPath)) return devPath;

            return null;
        }

        public static bool Start(string args = "")
        {
            if (_isRunning) return true;

            string vetoPath = GetVetoPath();
            if (vetoPath == null)
            {
                OnErrorReceived?.Invoke("veto.exe not found");
                return false;
            }

            try
            {
                var startInfo = new ProcessStartInfo
                {
                    FileName = vetoPath,
                    Arguments = args,
                    UseShellExecute = false,
                    RedirectStandardOutput = true,
                    RedirectStandardError = true,
                    CreateNoWindow = true,
                    WorkingDirectory = Path.GetDirectoryName(vetoPath)
                };

                _process = new Process { StartInfo = startInfo };
                _process.OutputDataReceived += (s, e) =>
                {
                    if (e.Data != null) OnOutputReceived?.Invoke(e.Data);
                };
                _process.ErrorDataReceived += (s, e) =>
                {
                    if (e.Data != null) OnErrorReceived?.Invoke(e.Data);
                };

                _process.Start();
                _process.BeginOutputReadLine();
                _process.BeginErrorReadLine();

                _isRunning = true;
                OnStateChanged?.Invoke(true);
                return true;
            }
            catch (Exception ex)
            {
                OnErrorReceived?.Invoke($"Failed to start: {ex.Message}");
                return false;
            }
        }

        public static bool Stop()
        {
            if (!_isRunning || _process == null) return true;

            try
            {
                if (!_process.HasExited)
                {
                    _process.Kill();
                    _process.WaitForExit(3000);
                }
                _process.Dispose();
                _process = null;
                _isRunning = false;
                OnStateChanged?.Invoke(false);
                return true;
            }
            catch (Exception ex)
            {
                OnErrorReceived?.Invoke($"Failed to stop: {ex.Message}");
                return false;
            }
        }

        public static bool Toggle(string args = "")
        {
            if (_isRunning) return Stop();
            else return Start(args);
        }

        public static string GetStatus()
        {
            if (!_isRunning) return "Stopped";
            if (_process != null && _process.HasExited) return "Crashed";
            return "Running";
        }

        public static void CheckVetoProcess()
        {
            if (!_isRunning) return;

            if (_process != null && _process.HasExited)
            {
                _isRunning = false;
                OnStateChanged?.Invoke(false);
            }
        }
    }
}
