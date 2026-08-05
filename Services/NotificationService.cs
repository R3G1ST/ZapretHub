using System;
using System.IO;
using System.Net.Http;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;
using ZapretHub.Models;

namespace ZapretHub.Services;

public static class NotificationService
{
    private static readonly string StoreDir = Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData),
        "ZapretHub");
    private static readonly string StoreFile = Path.Combine(StoreDir, "notifications.json");

    private const string GitHubRepo = "R3G1ST/ZapretHub";
    private const string ApiUrl = $"https://api.github.com/repos/{GitHubRepo}/releases/latest";

    private static string Token => SettingsService.Load().GitHubToken;

    public static event Action? OnNewNotification;
    public static event Action? OnNotificationsChanged;

    private static NotificationStore? _store;
    private static System.Threading.Timer? _timer;

    public static NotificationStore GetStore()
    {
        if (_store != null) return _store;
        try
        {
            if (File.Exists(StoreFile))
            {
                var json = File.ReadAllText(StoreFile);
                _store = JsonSerializer.Deserialize<NotificationStore>(json) ?? new NotificationStore();
            }
            else
            {
                _store = new NotificationStore();
            }
        }
        catch
        {
            _store = new NotificationStore();
        }
        var seen = new System.Collections.Generic.HashSet<string>();
        var deduped = new System.Collections.Generic.List<AppNotification>();
        foreach (var n in _store.Notifications)
        {
            if (seen.Add(n.Version))
                deduped.Add(n);
        }
        if (deduped.Count != _store.Notifications.Count)
        {
            _store.Notifications = deduped;
            SaveStore();
        }
        return _store;
    }

    public static void SaveStore()
    {
        try
        {
            var store = GetStore();
            if (!Directory.Exists(StoreDir))
                Directory.CreateDirectory(StoreDir);
            var json = JsonSerializer.Serialize(store, new JsonSerializerOptions { WriteIndented = true });
            File.WriteAllText(StoreFile, json);
        }
        catch { }
    }

    public static void StartPolling()
    {
        _timer?.Dispose();
        _timer = new System.Threading.Timer(async _ => await PollAsync(), null, TimeSpan.FromSeconds(5), TimeSpan.FromSeconds(30));
    }

    public static void StopPolling()
    {
        _timer?.Dispose();
        _timer = null;
    }

    public static async Task PollAsync()
    {
        try
        {
            using var handler = new HttpClientHandler
            {
                ServerCertificateCustomValidationCallback = (_, _, _, _) => true
            };
            using var http = new HttpClient(handler) { Timeout = TimeSpan.FromSeconds(15) };
            http.DefaultRequestHeaders.UserAgent.ParseAdd("ZapretHub/1.0");
            http.DefaultRequestHeaders.Authorization = new System.Net.Http.Headers.AuthenticationHeaderValue("Bearer", Token);

            var json = await http.GetStringAsync(ApiUrl);
            var doc = JsonDocument.Parse(json);

            string latestTag = doc.RootElement.GetProperty("tag_name").GetString() ?? "";
            string latestVersion = latestTag.TrimStart('v');

            string releaseNotes = "";
            if (doc.RootElement.TryGetProperty("body", out var bodyElement))
                releaseNotes = bodyElement.GetString() ?? "";

            string downloadUrl = "";
            if (doc.RootElement.TryGetProperty("assets", out var assets))
            {
                foreach (var asset in assets.EnumerateArray())
                {
                    string name = asset.GetProperty("name").GetString() ?? "";
                    if (name.EndsWith(".exe", StringComparison.OrdinalIgnoreCase))
                    {
                        downloadUrl = asset.GetProperty("browser_download_url").GetString() ?? "";
                        break;
                    }
                }
            }

            string currentVersion = System.Reflection.Assembly
                .GetExecutingAssembly()
                .GetName()
                .Version?.ToString(3) ?? "0.0.0";

            var store = GetStore();

            if (new Version(latestVersion) > new Version(currentVersion) && store.LastCheckedVersion != latestVersion)
            {
                var cleanNotes = releaseNotes;
                var lines = releaseNotes.Split('\n');
                if (lines.Length > 0)
                {
                    var first = lines[0].TrimStart('#', ' ');
                    if (first.StartsWith("ZapretHub") || first.StartsWith("v"))
                        cleanNotes = string.Join("\n", lines.Skip(1)).TrimStart('\n', '\r');
                }

                var notification = new AppNotification
                {
                    Version = latestVersion,
                    TagName = latestTag,
                    ReleaseNotes = cleanNotes,
                    DownloadUrl = downloadUrl,
                    Timestamp = DateTime.Now,
                    IsRead = false
                };

                store.Notifications = store.Notifications
                    .Where(n => n.Version != latestVersion)
                    .ToList();

                store.Notifications.Insert(0, notification);
                store.LastCheckedVersion = latestVersion;
                SaveStore();

                OnNewNotification?.Invoke();
                OnNotificationsChanged?.Invoke();
            }
        }
        catch { }
    }

    public static void MarkAsRead(string id)
    {
        var store = GetStore();
        var n = store.Notifications.Find(x => x.Id == id);
        if (n != null)
        {
            n.IsRead = true;
            SaveStore();
            OnNotificationsChanged?.Invoke();
        }
    }

    public static void MarkAllAsRead()
    {
        var store = GetStore();
        foreach (var n in store.Notifications)
            n.IsRead = true;
        SaveStore();
        OnNotificationsChanged?.Invoke();
    }

    public static void ClearAll()
    {
        var store = GetStore();
        store.Notifications.Clear();
        SaveStore();
        OnNotificationsChanged?.Invoke();
    }

    public static int GetUnreadCount()
    {
        return GetStore().Notifications.FindAll(x => !x.IsRead).Count;
    }
}
