using System.Collections.Generic;
using System.Linq;

namespace ZapretHub.Models;

public class ZapretConfig
{
    public string Name { get; set; } = "";
    public Dictionary<string, ServiceTestResult> Tests { get; set; } = new();
    public Dictionary<string, ServiceTestResult> GameTests { get; set; } = new();
    public int SuccessCount { get; set; }
    public int GameSuccessCount { get; set; }
    public int ErrorCount { get; set; }
    public int AveragePing { get; set; }
    public int GameAveragePing { get; set; }
    public bool IsValid { get; set; }
    public bool IsGameValid { get; set; }
    public bool IsPartiallyUsable => !IsValid && SuccessCount > 0;
    public bool IsGamePartiallyUsable => !IsGameValid && GameSuccessCount > 0;
    public bool IsFromMod { get; set; } = false;
    public string? ModName { get; set; } = null;
}

public class ServiceTestResult
{
    public string ServiceName { get; set; } = "";
    public string HttpStatus { get; set; } = "";
    public string Tls12Status { get; set; } = "";
    public string Tls13Status { get; set; } = "";
    public int Ping { get; set; }

    public bool IsSuccess =>
        HttpStatus == "OK" &&
        (Tls12Status == "OK" || Tls13Status == "OK");
}

public class ZapretConfigCache
{
    public string LastTested { get; set; } = "";
    public string CurrentConfig { get; set; } = "";
    public List<ZapretConfig> ValidConfigs { get; set; } = new();
    public List<ZapretConfig> PartialConfigs { get; set; } = new();
    public Dictionary<string, ServiceTestResult>? LastGameTestResults { get; set; }
    public string? LastGameTestedConfig { get; set; }
    public DateTime? LastGameTestTime { get; set; }

    public bool HasAnyConfigs => ValidConfigs.Count > 0 || PartialConfigs.Count > 0;

    public List<ZapretConfig> GetSelectableConfigs()
    {
        var list = new List<ZapretConfig>();
        list.AddRange(ValidConfigs.OrderBy(c => c.AveragePing));
        list.AddRange(PartialConfigs.OrderByDescending(c => c.SuccessCount).ThenBy(c => c.AveragePing));
        return list;
    }
}
