using System;
using System.Diagnostics;
using System.IO;
using System.Net.Http;
using System.Threading.Tasks;
using ZapretHub.Models;

namespace ZapretHub.Services;

public class UpdateService
{
    private const string GitHubRepo = "R3G1ST/ZapretHub";
    private const string ApiUrl = $"https://api.github.com/repos/{GitHubRepo}/releases/latest";

    private static bool _isDownloading;
    private static string? _downloadingUrl;
    public static bool IsDownloading => _isDownloading;
    public static int DownloadProgress { get; private set; }
    public static string? DownloadingUrl => _downloadingUrl;
    public static event Action<int>? OnProgress;
    public static event Action? OnDownloadStarted;
    public static event Action? OnDownloadFinished;

    public static async Task<(bool hasUpdate, string newVersion, string downloadUrl, string error)> CheckAsync()
    {
        try
        {
            using var handler = new HttpClientHandler
            {
                ServerCertificateCustomValidationCallback = (_, _, _, _) => true,
                AllowAutoRedirect = true,
                MaxAutomaticRedirections = 10
            };
            using var http = new HttpClient(handler) { Timeout = TimeSpan.FromSeconds(30) };
            http.DefaultRequestHeaders.UserAgent.ParseAdd("ZapretHub/1.0");

            var json = await http.GetStringAsync(ApiUrl);
            var doc = System.Text.Json.JsonDocument.Parse(json);

            string latestTag = doc.RootElement.GetProperty("tag_name").GetString() ?? "";
            string latestVersion = latestTag.TrimStart('v');

            string downloadUrl = "";
            foreach (var asset in doc.RootElement.GetProperty("assets").EnumerateArray())
            {
                string name = asset.GetProperty("name").GetString() ?? "";
                if (name.EndsWith(".exe", StringComparison.OrdinalIgnoreCase))
                {
                    downloadUrl = asset.GetProperty("browser_download_url").GetString() ?? "";
                    break;
                }
            }

            string currentVersion = System.Reflection.Assembly
                .GetExecutingAssembly()
                .GetName()
                .Version?.ToString(3) ?? "0.0.0";

            bool hasUpdate = ParseVersion(latestVersion) > ParseVersion(currentVersion);
            return (hasUpdate, latestVersion, downloadUrl, "");
        }
        catch (Exception ex)
        {
            return (false, "", "", ex.Message);
        }
    }

    public static async Task DownloadAndInstallAsync(string downloadUrl, Action<int>? onProgress = null)
    {
        if (_isDownloading)
            throw new InvalidOperationException("Загрузка уже выполняется");

        _isDownloading = true;
        _downloadingUrl = downloadUrl;
        DownloadProgress = 0;
        OnDownloadStarted?.Invoke();
        try
        {
            string tempPath = Path.Combine(Path.GetTempPath(), "ZapretHub_Setup.exe");

            using var handler = new HttpClientHandler
            {
                ServerCertificateCustomValidationCallback = (_, _, _, _) => true,
                AllowAutoRedirect = true,
                MaxAutomaticRedirections = 10
            };
            using var http = new HttpClient(handler) { Timeout = TimeSpan.FromMinutes(10) };
            http.DefaultRequestHeaders.UserAgent.ParseAdd("ZapretHub/1.0");

            using var response = await http.GetAsync(downloadUrl, HttpCompletionOption.ResponseHeadersRead);
            response.EnsureSuccessStatusCode();
            var totalBytes = response.Content.Headers.ContentLength ?? 0;
            using var stream = await response.Content.ReadAsStreamAsync();
            using var fileStream = File.Create(tempPath);

            var buffer = new byte[8192];
            long downloaded = 0;
            int read;

            while ((read = await stream.ReadAsync(buffer)) > 0)
            {
                await fileStream.WriteAsync(buffer.AsMemory(0, read));
                downloaded += read;
                if (totalBytes > 0)
                {
                    var pct = (int)(downloaded * 100 / totalBytes);
                    DownloadProgress = pct;
                    onProgress?.Invoke(pct);
                    OnProgress?.Invoke(pct);
                }
            }

        fileStream.Close();

        await Task.Delay(500);

        var fi = new FileInfo(tempPath);
        if (!fi.Exists || fi.Length < 1_000_000)
        {
            throw new IOException($"Файл повреждён: {fi.Length} байт");
        }

        System.Diagnostics.Process.Start(new System.Diagnostics.ProcessStartInfo
        {
            FileName = tempPath,
            UseShellExecute = true
        });

            System.Windows.Application.Current.Shutdown();
        }
        finally
        {
            _isDownloading = false;
            _downloadingUrl = null;
            DownloadProgress = 0;
            OnDownloadFinished?.Invoke();
        }
    }

    private static Version ParseVersion(string ver)
    {
        var match = System.Text.RegularExpressions.Regex.Match(ver, @"^(\d+\.\d+\.\d+)");
        return new Version(match.Success ? match.Groups[1].Value : "0.0.0");
    }
}
