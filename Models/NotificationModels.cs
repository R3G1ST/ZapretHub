using System;
using System.Collections.Generic;

namespace ZapretHub.Models;

public class AppNotification
{
    public string Id { get; set; } = Guid.NewGuid().ToString("N")[..8];
    public string Version { get; set; } = "";
    public string TagName { get; set; } = "";
    public string ReleaseNotes { get; set; } = "";
    public string DownloadUrl { get; set; } = "";
    public DateTime Timestamp { get; set; } = DateTime.Now;
    public bool IsRead { get; set; } = false;
}

public class NotificationStore
{
    public List<AppNotification> Notifications { get; set; } = new();
    public string LastCheckedVersion { get; set; } = "";
}
