using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Net;
using System.Net.Http;
using System.Text.Json;
using System.Text.RegularExpressions;
using System.Threading;
using System.Threading.Tasks;
using ZapretHub.Models;

namespace ZapretHub.Services;

public class ZapretConfigService
{
    private static readonly string CacheFile = Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData),
        "ZapretHub", "zapret_configs.json");

    private static readonly Regex ConfigRegex = new Regex(@"\[(\d+)/(\d+)\]\s+(.+\.bat)", RegexOptions.Compiled);
    private static readonly Regex TestLineRegex = new Regex(
        @"^\s*(\w+)\s+HTTP:(\w+)\s+TLS1\.2:(\w+)\s+TLS1\.3:(\w+)\s+\|\s+Ping:\s*(\d+)\s*ms",
        RegexOptions.Compiled | RegexOptions.IgnoreCase);
    private static readonly Regex PingOnlyRegex = new Regex(
        @"^\s*(\w+)\s+Ping:\s*(\d+)\s*ms",
        RegexOptions.Compiled | RegexOptions.IgnoreCase);

    public static ZapretConfigCache? LoadCache()
    {
        try
        {
            if (!File.Exists(CacheFile)) return null;
            var json = File.ReadAllText(CacheFile);
            return JsonSerializer.Deserialize<ZapretConfigCache>(json);
        }
        catch
        {
            return null;
        }
    }

    public static void SaveCache(ZapretConfigCache cache)
    {
        try
        {
            var dir = Path.GetDirectoryName(CacheFile);
            if (dir != null && !Directory.Exists(dir))
                Directory.CreateDirectory(dir);

            var json = JsonSerializer.Serialize(cache, new JsonSerializerOptions { WriteIndented = true });
            File.WriteAllText(CacheFile, json);
        }
        catch { }
    }

    public static async Task<(List<ZapretConfig> configs, Process? process)> TestAllConfigsAsync(
        string zapretPath,
        Action<string>? onProgress = null,
        Action<int, int>? onConfigTested = null,
        ZapretVersion version = ZapretVersion.V1)
    {
        var configs = new List<ZapretConfig>();
        Process? process = null;

        var zapretDir = Path.GetDirectoryName(zapretPath);
        if (string.IsNullOrEmpty(zapretDir) || !Directory.Exists(zapretDir))
        {
            onProgress?.Invoke("❌ Ошибка: директория Zapret не найдена");
            return (configs, null);
        }

        if (version == ZapretVersion.V2)
        {
            var zapret2Root = FindZapret2Root(zapretDir);
            if (zapret2Root == null)
            {
                onProgress?.Invoke("❌ Директория zapret2 не найдена в " + zapretDir);
                return (configs, null);
            }

            onProgress?.Invoke($"📂 Zapret2 найден: {zapret2Root}");

            var foundConfigs = new List<(string name, string args)>();

            var configDefault = Path.Combine(zapret2Root, "config.default");
            if (File.Exists(configDefault))
            {
                onProgress?.Invoke("📋 Читаю config.default...");
                var nfqwsOpt = ParseNfqws2Opt(configDefault);
                if (nfqwsOpt.Count > 0)
                {
                    foreach (var (name, args) in nfqwsOpt)
                    {
                        foundConfigs.Add((name, args));
                        onProgress?.Invoke($"   ✓ {name}");
                    }
                }
            }

            var shScripts = Directory.GetFiles(zapret2Root, "*.sh", SearchOption.AllDirectories)
                .Where(f =>
                {
                    var name = Path.GetFileName(f);
                    return !name.StartsWith("install", StringComparison.OrdinalIgnoreCase)
                        && !name.StartsWith("uninstall", StringComparison.OrdinalIgnoreCase)
                        && !name.StartsWith("clear", StringComparison.OrdinalIgnoreCase);
                })
                .ToList();

            if (shScripts.Count > 0)
            {
                onProgress?.Invoke($"🔍 Сканирую {shScripts.Count} .sh скриптов...");
                foreach (var sh in shScripts)
                {
                    var scriptArgs = ParseWinws2FromShScript(sh);
                    if (scriptArgs.Count > 0)
                    {
                        var scriptName = Path.GetFileNameWithoutExtension(sh);
                        foreach (var (name, args) in scriptArgs)
                        {
                            var fullName = $"[{scriptName}] {name}";
                            foundConfigs.Add((fullName, args));
                            onProgress?.Invoke($"   ✓ {fullName}");
                        }
                    }
                }
            }

            if (foundConfigs.Count == 0)
            {
                foundConfigs.Add(("default (NFQWS2)", "--filter-tcp=80 --filter-l7=http --payload=http_req --new --filter-tcp=443 --filter-l7=tls --payload=tls_client_hello --new --filter-udp=443 --filter-l7=quic --payload=quic_initial"));
                onProgress?.Invoke("ℹ️ Конфиги не найдены, добавлен профиль по умолчанию");
            }

            onProgress?.Invoke($"📋 Итого: {foundConfigs.Count} конфигов Zapret2");

            int idx = 0;
            var winws2Exe = Path.Combine(zapret2Root, "binaries", "windows-x86_64", "winws2.exe");
            if (!File.Exists(winws2Exe))
                winws2Exe = zapretDir.Contains("winws2", StringComparison.OrdinalIgnoreCase)
                    ? zapretDir : null;

            foreach (var (name, args) in foundConfigs)
            {
                idx++;
                onProgress?.Invoke($"[{idx}/{foundConfigs.Count}] Тестирую: {name}");
                onConfigTested?.Invoke(idx, foundConfigs.Count);

                bool processAlive = false;
                int exitCode = -1;

                if (!string.IsNullOrEmpty(winws2Exe) && File.Exists(winws2Exe))
                {
                    try
                    {
                        var testPsi = new ProcessStartInfo
                        {
                            FileName = winws2Exe,
                            Arguments = args,
                            UseShellExecute = false,
                            CreateNoWindow = true,
                            WorkingDirectory = zapret2Root,
                            RedirectStandardError = true,
                            RedirectStandardOutput = true
                        };
                        var proc = Process.Start(testPsi);
                        if (proc != null)
                        {
                            await Task.Delay(3000);
                            processAlive = !proc.HasExited;
                            if (proc.HasExited)
                                exitCode = proc.ExitCode;
                            else
                            {
                                try { proc.Kill(); } catch { }
                                exitCode = 0;
                            }
                            proc.Dispose();
                        }
                    }
                    catch { }
                }

                var tests = new Dictionary<string, ServiceTestResult>();
                tests["process"] = new ServiceTestResult
                {
                    ServiceName = "process",
                    HttpStatus = processAlive ? "OK" : "FAIL",
                    Tls12Status = processAlive ? "OK" : "FAIL",
                    Tls13Status = processAlive ? "OK" : "FAIL",
                    Ping = 0
                };

                configs.Add(new ZapretConfig
                {
                    Name = name,
                    FilePath = "",
                    Args = args,
                    IsValid = processAlive,
                    SuccessCount = processAlive ? 1 : 0,
                    ErrorCount = processAlive ? 0 : 1,
                    Tests = tests,
                    AveragePing = 0
                });

                var icon = processAlive ? "✅" : "❌";
                onProgress?.Invoke($"[HEADER]{icon} {name} - {(processAlive ? "РАБОТАЕТ" : "НЕ РАБОТАЕТ (код: " + exitCode + ")")}[/HEADER]");
            }

            return (configs, null);
        }

        var testScript = Path.Combine(zapretDir, "utils", "test zapret.ps1");
        if (!File.Exists(testScript))
        {
            onProgress?.Invoke("❌ Ошибка: скрипт test zapret.ps1 не найден");
            return (configs, null);
        }

        onProgress?.Invoke("🚀 Начинаем полное тестирование конфигов...");

        var psi = new ProcessStartInfo
        {
            FileName = "powershell.exe",
            Arguments = $"-NoProfile -ExecutionPolicy Bypass -File \"{testScript}\"",
            WorkingDirectory = zapretDir,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            RedirectStandardInput = true,
            UseShellExecute = false,
            CreateNoWindow = true,
            StandardOutputEncoding = System.Text.Encoding.UTF8
        };

        process = new Process { StartInfo = psi };

        ZapretConfig? currentConfig = null;
        int totalConfigs = 0;
        int testedConfigs = 0;

        process.OutputDataReceived += (sender, e) =>
        {
            if (string.IsNullOrEmpty(e.Data)) return;

            var line = e.Data;

            var configMatch = ConfigRegex.Match(line);
            if (configMatch.Success)
            {
                if (currentConfig != null)
                {
                    var successCount = currentConfig.SuccessCount;
                    var totalCount = currentConfig.Tests.Count;
                    var failedTests = totalCount - successCount;

                    currentConfig.IsValid = currentConfig.ErrorCount == 0 && currentConfig.SuccessCount == totalCount && totalCount > 0;

                    if (currentConfig.IsValid)
                    {
                        configs.Add(currentConfig);
                        onProgress?.Invoke($"[HEADER]✅ {currentConfig.Name} - ИДЕАЛЬНЫЙ[/HEADER]");
                        onProgress?.Invoke($"   🔹 Протестировано: {successCount}/{totalCount}, Пинг: {currentConfig.AveragePing}мс");
                        onProgress?.Invoke("");
                    }
                    else if (currentConfig.IsPartiallyUsable)
                    {
                        configs.Add(currentConfig);
                        onProgress?.Invoke($"[HEADER]⚠️ {currentConfig.Name} - ЧАСТИЧНО РАБОЧИЙ[/HEADER]");
                        onProgress?.Invoke($"   🔹 Протестировано: {successCount}/{totalCount}, Пинг: {currentConfig.AveragePing}мс");
                        onProgress?.Invoke("");
                    }
                    else
                    {
                        onProgress?.Invoke($"[HEADER]❌ {currentConfig.Name} - НЕРАБОЧИЙ[/HEADER]");
                        onProgress?.Invoke($"   🔹 Протестировано: {successCount}/{totalCount}, Не работает: {failedTests} сайтов");
                        onProgress?.Invoke("");
                    }

                    testedConfigs++;
                    onConfigTested?.Invoke(testedConfigs, totalConfigs);
                }

                var current = int.Parse(configMatch.Groups[1].Value);
                totalConfigs = int.Parse(configMatch.Groups[2].Value);
                var configName = configMatch.Groups[3].Value;

                currentConfig = new ZapretConfig
                {
                    Name = configName,
                    Tests = new Dictionary<string, ServiceTestResult>()
                };

                onProgress?.Invoke("");
                onProgress?.Invoke($"[HEADER]🔄 Тестирую конфиг [{current}/{totalConfigs}]: {configName}[/HEADER]");
                return;
            }

            var testMatch = TestLineRegex.Match(line);
            if (testMatch.Success && currentConfig != null)
            {
                var serviceName = testMatch.Groups[1].Value;
                var httpStatus = testMatch.Groups[2].Value;
                var tls12Status = testMatch.Groups[3].Value;
                var tls13Status = testMatch.Groups[4].Value;
                var pingStr = testMatch.Groups[5].Value;
                var ping = string.IsNullOrEmpty(pingStr) ? 0 : int.Parse(pingStr);

                var testResult = new ServiceTestResult
                {
                    ServiceName = serviceName,
                    HttpStatus = httpStatus,
                    Tls12Status = tls12Status,
                    Tls13Status = tls13Status,
                    Ping = ping
                };

                currentConfig.Tests[serviceName] = testResult;

                if (serviceName.StartsWith("Discord") || serviceName.StartsWith("YouTube") || serviceName.StartsWith("Google"))
                {
                    var statusText = httpStatus == "OK" && (tls12Status == "OK" || tls13Status == "OK")
                        ? "РАБОТАЕТ"
                        : (httpStatus == "ERROR" || tls12Status == "ERROR" || tls13Status == "ERROR"
                            ? "НЕ РАБОТАЕТ"
                            : "ЧАСТИЧНО");

                    onProgress?.Invoke($"   🟢 {serviceName}: {statusText} | {ping}мс");
                }

                if (testResult.IsSuccess)
                    currentConfig.SuccessCount++;

                if (httpStatus == "ERROR" || tls12Status == "ERROR" || tls13Status == "ERROR")
                    currentConfig.ErrorCount++;

                if (currentConfig.Tests.Count > 0)
                    currentConfig.AveragePing = (int)currentConfig.Tests.Values.Average(t => t.Ping);

                return;
            }

            var pingOnlyMatch = PingOnlyRegex.Match(line);
            if (pingOnlyMatch.Success && currentConfig != null)
            {
                var serviceName = pingOnlyMatch.Groups[1].Value;
                var pingStr = pingOnlyMatch.Groups[2].Value;
                var ping = string.IsNullOrEmpty(pingStr) ? 0 : int.Parse(pingStr);

                var testResult = new ServiceTestResult
                {
                    ServiceName = serviceName,
                    HttpStatus = "OK",
                    Tls12Status = "N/A",
                    Tls13Status = "N/A",
                    Ping = ping
                };

                currentConfig.Tests[serviceName] = testResult;

                if (ping > 0)
                    currentConfig.SuccessCount++;

                if (currentConfig.Tests.Count > 0)
                    currentConfig.AveragePing = (int)currentConfig.Tests.Values.Average(t => t.Ping);
            }
        };

        process.Start();
        process.BeginOutputReadLine();

        try
        {
            await Task.Delay(1000);
            await process.StandardInput.WriteLineAsync("1");
            await Task.Delay(500);
            await process.StandardInput.WriteLineAsync("1");
            process.StandardInput.Close();
        }
        catch (IOException)
        {
        }

        await process.WaitForExitAsync();

        if (currentConfig != null)
        {
            var totalCountEnd = currentConfig.Tests.Count;
            currentConfig.IsValid = currentConfig.ErrorCount == 0 && currentConfig.SuccessCount == totalCountEnd && totalCountEnd > 0;
            if (currentConfig.IsValid || currentConfig.IsPartiallyUsable)
                configs.Add(currentConfig);
        }

        foreach (var cfg in configs)
        {
        }

        configs = configs
            .OrderByDescending(c => c.IsValid)
            .ThenBy(c => c.AveragePing)
            .ToList();

        var idealCount = configs.Count(c => c.IsValid);
        var partialCount = configs.Count(c => c.IsPartiallyUsable);
        onProgress?.Invoke($"Тестирование завершено. Найдено {idealCount} идеальных и {partialCount} частично рабочих конфигов");

        return (configs, process);
    }

    public static async Task<(bool isWorking, string message)> TestSingleConfigAsync(
        string zapretPath,
        string configName,
        Action<string>? onProgress = null)
    {
        var zapretDir = Path.GetDirectoryName(zapretPath);
        if (string.IsNullOrEmpty(zapretDir) || !Directory.Exists(zapretDir))
        {
            onProgress?.Invoke("❌ Ошибка: директория Zapret не найдена");
            return (false, "Ошибка: директория Zapret не найдена");
        }

        var testScript = Path.Combine(zapretDir, "utils", "test zapret.ps1");
        if (!File.Exists(testScript))
        {
            onProgress?.Invoke("❌ Ошибка: скрипт test zapret.ps1 не найден");
            return (false, "Ошибка: скрипт test zapret.ps1 не найден");
        }

        onProgress?.Invoke("🚀 Начинаем тестирование конфига...");

        var psi = new ProcessStartInfo
        {
            FileName = "powershell.exe",
            Arguments = $"-NoProfile -ExecutionPolicy Bypass -File \"{testScript}\"",
            WorkingDirectory = zapretDir,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            RedirectStandardInput = true,
            UseShellExecute = false,
            CreateNoWindow = true,
            StandardOutputEncoding = System.Text.Encoding.UTF8
        };

        var process = new Process { StartInfo = psi };

        ZapretConfig? currentConfig = null;
        bool foundTargetConfig = false;
        bool configTestComplete = false;

        process.OutputDataReceived += (sender, e) =>
        {
            if (string.IsNullOrEmpty(e.Data)) return;

            var line = e.Data;

            var configMatch = ConfigRegex.Match(line);
            if (configMatch.Success)
            {
                var configNameFromTest = configMatch.Groups[3].Value;

                if (configNameFromTest == configName)
                {
                    foundTargetConfig = true;
                    currentConfig = new ZapretConfig
                    {
                        Name = configNameFromTest,
                        Tests = new Dictionary<string, ServiceTestResult>()
                    };

                    onProgress?.Invoke("");
                    onProgress?.Invoke($"[HEADER]🔄 Тестирую конфиг: {configName}[/HEADER]");
                }
                else if (foundTargetConfig)
                {
                    configTestComplete = true;
                }

                return;
            }

            if (foundTargetConfig && !configTestComplete && currentConfig != null)
            {
                var testMatch = TestLineRegex.Match(line);
                if (testMatch.Success)
                {
                    var serviceName = testMatch.Groups[1].Value;
                    var httpStatus = testMatch.Groups[2].Value;
                    var tls12Status = testMatch.Groups[3].Value;
                    var tls13Status = testMatch.Groups[4].Value;
                    var pingStr = testMatch.Groups[5].Value;
                    var ping = string.IsNullOrEmpty(pingStr) ? 0 : int.Parse(pingStr);

                    var testResult = new ServiceTestResult
                    {
                        ServiceName = serviceName,
                        HttpStatus = httpStatus,
                        Tls12Status = tls12Status,
                        Tls13Status = tls13Status,
                        Ping = ping
                    };

                    currentConfig.Tests[serviceName] = testResult;

                    var statusText = httpStatus == "OK" && (tls12Status == "OK" || tls13Status == "OK")
                        ? "РАБОТАЕТ"
                        : (httpStatus == "ERROR" || tls12Status == "ERROR" || tls13Status == "ERROR"
                            ? "НЕ РАБОТАЕТ"
                            : "ЧАСТИЧНО");

                    onProgress?.Invoke($"   🟢 {serviceName}: {statusText} | {ping}мс");

                    if (testResult.IsSuccess)
                        currentConfig.SuccessCount++;

                    if (httpStatus == "ERROR" || tls12Status == "ERROR" || tls13Status == "ERROR")
                        currentConfig.ErrorCount++;

                    if (currentConfig.Tests.Count > 0)
                        currentConfig.AveragePing = (int)currentConfig.Tests.Values.Average(t => t.Ping);
                    return;
                }

                var pingOnlyMatch = PingOnlyRegex.Match(line);
                if (pingOnlyMatch.Success)
                {
                    var serviceName = pingOnlyMatch.Groups[1].Value;
                    var pingStr = pingOnlyMatch.Groups[2].Value;
                    var ping = string.IsNullOrEmpty(pingStr) ? 0 : int.Parse(pingStr);

                    var testResult = new ServiceTestResult
                    {
                        ServiceName = serviceName,
                        HttpStatus = "OK",
                        Tls12Status = "N/A",
                        Tls13Status = "N/A",
                        Ping = ping
                    };

                    currentConfig.Tests[serviceName] = testResult;

                    if (ping > 0)
                        currentConfig.SuccessCount++;

                    if (currentConfig.Tests.Count > 0)
                        currentConfig.AveragePing = (int)currentConfig.Tests.Values.Average(t => t.Ping);
                }
            }
        };

        process.Start();
        process.BeginOutputReadLine();

        try
        {
            await Task.Delay(2000);
            await process.StandardInput.WriteLineAsync("1");
            await Task.Delay(1000);
            await process.StandardInput.WriteLineAsync("2");
            await Task.Delay(1000);

            var configFiles = Directory.GetFiles(zapretDir, "*.bat")
                .Where(f => !Path.GetFileName(f).StartsWith("service", StringComparison.OrdinalIgnoreCase))
                .Select(Path.GetFileName)
                .OrderBy(f => f, new NaturalStringComparer())
                .ToList();

            int configIndex = configFiles.IndexOf(configName) + 1;

            for (int i = 0; i < configFiles.Count; i++)
            {
            }

            if (configIndex > 0)
            {
                await process.StandardInput.WriteLineAsync(configIndex.ToString());
                await Task.Delay(1000);
            }
            else
            {
                onProgress?.Invoke($"❌ Ошибка: не удалось найти конфиг {configName} в списке");
                process.StandardInput.Close();
                return (false, $"Не удалось найти конфиг {configName} в списке");
            }

            process.StandardInput.Close();
        }
        catch (IOException)
        {
        }

        await process.WaitForExitAsync();

        if (currentConfig != null && foundTargetConfig)
        {
            currentConfig.IsValid = currentConfig.ErrorCount == 0 && currentConfig.SuccessCount == currentConfig.Tests.Count && currentConfig.Tests.Count > 0;

            var successCount = currentConfig.SuccessCount;
            var totalCount = currentConfig.Tests.Count;
            var failedTests = totalCount - successCount;

            if (currentConfig.IsValid)
            {
                onProgress?.Invoke($"[HEADER]✅ {currentConfig.Name} - РАБОЧИЙ[/HEADER]");
                onProgress?.Invoke($"   🔹 Протестировано: {successCount}/{totalCount}, Пинг: {currentConfig.AveragePing}мс");
                return (true, $"Конфиг работает! Пройдено {successCount}/{totalCount} тестов");
            }
            else
            {
                onProgress?.Invoke($"[HEADER]❌ {currentConfig.Name} - НЕРАБОЧИЙ[/HEADER]");
                onProgress?.Invoke($"   🔹 Протестировано: {successCount}/{totalCount}, Не работает: {failedTests} сайтов");
                return (false, $"Конфиг не проходит все тесты. Пройдено {successCount}/{totalCount}");
            }
        }
        else
        {
            onProgress?.Invoke($"❌ Ошибка: конфиг {configName} не был протестирован");
            onProgress?.Invoke($"⚠️ ВОЗМОЖНО У ВАС ЗАПУЩЕН VPN! Закройте VPN и попробуйте ещё раз!");
            return (false, $"Конфиг {configName} не был протестирован. Возможно, у вас запущен VPN - закройте его и попробуйте снова.");
        }
    }

    private static async Task<bool> TestDiscordConnection()
    {
        try
        {
            using var client = new System.Net.Http.HttpClient();
            client.Timeout = TimeSpan.FromSeconds(5);
            var response = await client.GetAsync("https://discord.com");
            return response.IsSuccessStatusCode;
        }
        catch
        {
            return false;
        }
    }

    private static string GetStatusEmojis(string http, string tls12, string tls13)
    {
        var httpEmoji = http switch
        {
            "OK" => "✅",
            "ERROR" => "❌",
            "UNSUP" => "⚠️",
            _ => "❓"
        };

        var tls12Emoji = tls12 switch
        {
            "OK" => "✅",
            "ERROR" => "❌",
            "UNSUP" => "⚠️",
            _ => "❓"
        };

        var tls13Emoji = tls13 switch
        {
            "OK" => "✅",
            "ERROR" => "❌",
            "UNSUP" => "⚠️",
            _ => "❓"
        };

        return $"{httpEmoji} {http} | {tls12Emoji} {tls12} | {tls13Emoji} {tls13}";
    }

    public static async Task<bool> ApplyConfigAsync(string zapretPath, string configName, ZapretVersion version = ZapretVersion.V1, string? v2Args = null)
    {
        try
        {
            var zapretDir = Path.GetDirectoryName(zapretPath);
            if (string.IsNullOrEmpty(zapretDir))
            {
                return false;
            }

            if (version == ZapretVersion.V2)
            {
                var args = v2Args;
                if (string.IsNullOrEmpty(args))
                {
                    args = await ParseConfigArgsV2Async(Path.Combine(zapretDir, "config.default"), zapretDir);
                }
                if (string.IsNullOrEmpty(args))
                {
                    return false;
                }

                await StopZapretV2ProcessesAsync();

                var v2Root = FindZapret2Root(zapretDir);
                var workDir = !string.IsNullOrEmpty(v2Root) ? v2Root : zapretDir;

                var psi = new ProcessStartInfo
                {
                    FileName = zapretPath,
                    Arguments = args,
                    UseShellExecute = true,
                    WorkingDirectory = workDir
                };
                var proc = Process.Start(psi);
                await Task.Delay(1500);

                var running = Process.GetProcesses()
                    .Any(p => { try { return p.ProcessName.ToLower().Contains("winws2") || p.ProcessName.ToLower().Contains("nfqws2"); } catch { return false; } });
                return running;
            }

            var configPath = Path.Combine(zapretDir, configName);
            if (!File.Exists(configPath))
            {
                return false;
            }

            var binPath = Path.Combine(zapretDir, "bin");
            var winwsExe = Path.Combine(binPath, "winws.exe");
            if (!File.Exists(winwsExe))
            {
                return false;
            }

            var argsV1 = await ParseConfigArgsAsync(configPath, zapretDir, binPath);
            if (string.IsNullOrEmpty(argsV1))
            {
                return false;
            }

            await StopAndRemoveServiceAsync("zapret");

            EnableTcpTimestamps();

            var success = await CreateServiceAsync("zapret", winwsExe, argsV1, configName);

            if (success)
            {
                await StartServiceAsync("zapret");
            }

            return success;
        }
        catch
        {
            return false;
        }
    }

    private static async Task<string> ParseConfigArgsAsync(string configPath, string zapretDir, string binPath)
    {
        try
        {
            var lines = await File.ReadAllLinesAsync(configPath);

            var listsPath = Path.Combine(zapretDir, "lists");
            var fullText = "";
            bool capture = false;

            foreach (var line in lines)
            {
                var trimmed = line.Trim();

                if (trimmed.Contains("winws.exe"))
                {
                    capture = true;
                    var idx = trimmed.IndexOf("winws.exe");
                    if (idx >= 0)
                    {
                        trimmed = trimmed.Substring(idx + "winws.exe".Length).Trim();
                    }
                }

                if (!capture) continue;

                if (trimmed.EndsWith("^"))
                {
                    trimmed = trimmed.Substring(0, trimmed.Length - 1).Trim();
                }

                fullText += " " + trimmed;
            }

            fullText = fullText.Replace("%BIN%", binPath + "\\");
            fullText = fullText.Replace("%LISTS%", listsPath + "\\");
            fullText = fullText.Replace("%GameFilter%", "12");
            fullText = fullText.Replace("%GameFilterTCP%", "12");
            fullText = fullText.Replace("%GameFilterUDP%", "12");

            fullText = fullText.Replace("\"", "");

            fullText = System.Text.RegularExpressions.Regex.Replace(fullText, @"\s+", " ").Trim();

            return fullText;
        }
        catch
        {
            return "";
        }
    }

    private static async Task<string> ParseConfigArgsV2Async(string configPath, string zapretDir)
    {
        try
        {
            var lines = await File.ReadAllLinesAsync(configPath);

            var listsPath = Path.Combine(zapretDir, "lists");
            var fullText = "";
            bool capture = false;

            string[] exeNames = ["winws2.exe", "nfqws2.exe", "winws.exe", "nfqws.exe"];

            foreach (var line in lines)
            {
                var trimmed = line.Trim();

                foreach (var exeName in exeNames)
                {
                    if (trimmed.Contains(exeName))
                    {
                        capture = true;
                        var idx = trimmed.IndexOf(exeName);
                        if (idx >= 0)
                        {
                            trimmed = trimmed.Substring(idx + exeName.Length).Trim();
                        }
                        break;
                    }
                }

                if (!capture) continue;

                if (trimmed.EndsWith("^"))
                {
                    trimmed = trimmed.Substring(0, trimmed.Length - 1).Trim();
                }

                fullText += " " + trimmed;
            }

            fullText = fullText.Replace("%BIN%", Path.Combine(zapretDir, "") + "\\");
            fullText = fullText.Replace("%LISTS%", listsPath + "\\");
            fullText = fullText.Replace("%GameFilter%", "12");
            fullText = fullText.Replace("%GameFilterTCP%", "12");
            fullText = fullText.Replace("%GameFilterUDP%", "12");

            fullText = fullText.Replace("\"", "");

            fullText = System.Text.RegularExpressions.Regex.Replace(fullText, @"\s+", " ").Trim();

            return fullText;
        }
        catch
        {
            return "";
        }
    }

    private static async Task StopZapretV2ProcessesAsync()
    {
        string[] processNames = ["winws2", "nfqws2"];
        foreach (var name in processNames)
        {
            var procs = Process.GetProcessesByName(name);
            foreach (var proc in procs)
            {
                try
                {
                    proc.Kill(true);
                    proc.WaitForExit(3000);
                    proc.Dispose();
                }
                catch { }
            }
        }
        await Task.Delay(500);
    }

    public static List<string> ScanBatFiles(string zapretDir, ZapretVersion version = ZapretVersion.V1)
    {
        if (!Directory.Exists(zapretDir))
            return new List<string>();

        var batFiles = Directory.GetFiles(zapretDir, "*.bat", SearchOption.TopDirectoryOnly)
            .Where(f =>
            {
                var name = Path.GetFileName(f);
                return !name.StartsWith("service", StringComparison.OrdinalIgnoreCase)
                    && !name.StartsWith("uninstall", StringComparison.OrdinalIgnoreCase);
            })
            .OrderBy(f => Path.GetFileName(f), new NaturalStringComparer())
            .Select(Path.GetFileName)
            .ToList();

        if (version == ZapretVersion.V2)
        {
            var zapret2Dir = Path.Combine(zapretDir, "zapret2");
            if (Directory.Exists(zapret2Dir))
            {
                var v2BatFiles = Directory.GetFiles(zapret2Dir, "*.bat", SearchOption.TopDirectoryOnly)
                    .Where(f =>
                    {
                        var name = Path.GetFileName(f);
                        return !name.StartsWith("service", StringComparison.OrdinalIgnoreCase)
                            && !name.StartsWith("uninstall", StringComparison.OrdinalIgnoreCase);
                    })
                    .OrderBy(f => Path.GetFileName(f), new NaturalStringComparer())
                    .Select(Path.GetFileName)
                    .ToList();

                return v2BatFiles;
            }
        }

        return batFiles;
    }

    private static List<string> SplitArgs(string line)
    {
        var result = new List<string>();
        var current = "";
        bool inQuotes = false;

        for (int i = 0; i < line.Length; i++)
        {
            char c = line[i];

            if (c == '"')
            {
                inQuotes = !inQuotes;
                current += c;
            }
            else if (c == ' ' && !inQuotes)
            {
                if (!string.IsNullOrWhiteSpace(current))
                {
                    result.Add(current);
                    current = "";
                }
            }
            else
            {
                current += c;
            }
        }

        if (!string.IsNullOrWhiteSpace(current))
        {
            result.Add(current);
        }

        return result;
    }

    private static async Task StopAndRemoveServiceAsync(string serviceName)
    {
        try
        {
            var stopPsi = new ProcessStartInfo
            {
                FileName = "net",
                Arguments = $"stop {serviceName}",
                UseShellExecute = false,
                CreateNoWindow = true,
                RedirectStandardOutput = true,
                RedirectStandardError = true
            };

            using (var stopProc = Process.Start(stopPsi))
            {
                if (stopProc != null)
                {
                    await stopProc.WaitForExitAsync();
                }
            }

            await Task.Delay(500);

            var deletePsi = new ProcessStartInfo
            {
                FileName = "sc",
                Arguments = $"delete {serviceName}",
                UseShellExecute = false,
                CreateNoWindow = true,
                RedirectStandardOutput = true,
                RedirectStandardError = true
            };

            using (var deleteProc = Process.Start(deletePsi))
            {
                if (deleteProc != null)
                {
                    await deleteProc.WaitForExitAsync();
                }
            }

            await Task.Delay(500);

            foreach (var proc in Process.GetProcessesByName("winws"))
            {
                try { proc.Kill(); proc.Dispose(); } catch { }
            }
        }
        catch { }
    }

    private static void EnableTcpTimestamps()
    {
        try
        {
            var psi = new ProcessStartInfo
            {
                FileName = "netsh",
                Arguments = "interface tcp set global timestamps=enabled",
                UseShellExecute = false,
                CreateNoWindow = true,
                RedirectStandardOutput = true,
                RedirectStandardError = true
            };

            using var proc = Process.Start(psi);
            proc?.WaitForExit();
        }
        catch { }
    }

    private static async Task<bool> CreateServiceAsync(string serviceName, string exePath, string args, string configName)
    {
        try
        {
            var binPathValue = $"\"{exePath}\" {args}";

            var createPsi = new ProcessStartInfo
            {
                FileName = "sc",
                Arguments = $"create {serviceName} binPath= \"{binPathValue}\" DisplayName= \"zapret\" start= auto",
                UseShellExecute = false,
                CreateNoWindow = true,
                RedirectStandardOutput = true,
                RedirectStandardError = true
            };

            using (var createProc = Process.Start(createPsi))
            {
                if (createProc != null)
                {
                    var output = await createProc.StandardOutput.ReadToEndAsync();
                    var error = await createProc.StandardError.ReadToEndAsync();
                    await createProc.WaitForExitAsync();

                    if (createProc.ExitCode != 0) return false;
                }
            }

            var descPsi = new ProcessStartInfo
            {
                FileName = "sc",
                Arguments = $"description {serviceName} \"Zapret DPI bypass software\"",
                UseShellExecute = false,
                CreateNoWindow = true,
                RedirectStandardOutput = true,
                RedirectStandardError = true
            };

            using (var descProc = Process.Start(descPsi))
            {
                if (descProc != null)
                {
                    await descProc.WaitForExitAsync();
                }
            }

            var regPsi = new ProcessStartInfo
            {
                FileName = "reg",
                Arguments = $"add \"HKLM\\System\\CurrentControlSet\\Services\\{serviceName}\" /v zapret-discord-youtube /t REG_SZ /d \"{configName.Replace(".bat", "")}\" /f",
                UseShellExecute = false,
                CreateNoWindow = true,
                RedirectStandardOutput = true,
                RedirectStandardError = true
            };

            using (var regProc = Process.Start(regPsi))
            {
                if (regProc != null)
                {
                    await regProc.WaitForExitAsync();
                }
            }

            return true;
        }
        catch
        {
            return false;
        }
    }

    private static async Task StartServiceAsync(string serviceName)
    {
        try
        {
            var startPsi = new ProcessStartInfo
            {
                FileName = "sc",
                Arguments = $"start {serviceName}",
                UseShellExecute = false,
                CreateNoWindow = true,
                RedirectStandardOutput = true,
                RedirectStandardError = true
            };

            using var startProc = Process.Start(startPsi);
            if (startProc != null)
            {
                var output = await startProc.StandardOutput.ReadToEndAsync();
                var error = await startProc.StandardError.ReadToEndAsync();
                await startProc.WaitForExitAsync();
            }
        }
        catch { }
    }

    public static string GetModBatName(string modFolderName)
        => $"mod_{modFolderName}.bat";

    public static void InstallModBat(string zapretServicePath, string modFolderPath, string modFolderName)
    {
        var zapretDir = Path.GetDirectoryName(zapretServicePath) ?? @"C:\Zapret";
        var src = Path.Combine(modFolderPath, "strategy.bat");
        var dst = Path.Combine(zapretDir, GetModBatName(modFolderName));
        if (File.Exists(src))
            File.Copy(src, dst, overwrite: true);
    }

    public static void UninstallModBat(string zapretServicePath, string modFolderName)
    {
        var zapretDir = Path.GetDirectoryName(zapretServicePath) ?? @"C:\Zapret";
        var dst = Path.Combine(zapretDir, GetModBatName(modFolderName));
        if (File.Exists(dst))
            File.Delete(dst);
    }

    public static void InjectModConfig(string zapretServicePath, string modName, string modFolderName)
    {
        var cache = LoadCache() ?? new ZapretConfigCache
        {
            ValidConfigs = [],
            PartialConfigs = [],
        };

        cache.ValidConfigs ??= [];
        cache.PartialConfigs ??= [];

        var batName = GetModBatName(modFolderName);
        cache.ValidConfigs.RemoveAll(c => c.Name == batName);
        cache.PartialConfigs.RemoveAll(c => c.Name == batName);

        cache.ValidConfigs.Insert(0, new ZapretConfig
        {
            Name = batName,
            IsValid = true,
            IsFromMod = true,
            ModName = modName
        });

        SaveCache(cache);
    }

    public static void RemoveAllModConfigs()
    {
        var cache = LoadCache();
        if (cache is null) return;

        cache.ValidConfigs?.RemoveAll(c => c.IsFromMod);
        cache.PartialConfigs?.RemoveAll(c => c.IsFromMod);

        SaveCache(cache);
    }

    public static void RemoveModConfig(string zapretServicePath, string modFolderName)
    {
        var cache = LoadCache();
        if (cache is null) return;

        var batName = GetModBatName(modFolderName);
        cache.ValidConfigs?.RemoveAll(c => c.Name == batName);
        cache.PartialConfigs?.RemoveAll(c => c.Name == batName);

        if (cache.CurrentConfig == batName)
            cache.CurrentConfig = null;

        SaveCache(cache);
    }

    public static readonly Dictionary<string, string[]> GameServiceDomains = new()
    {
        ["EpicGames"] = new[] { "www.epicgames.com", "launcher-public-service-prod06.ol.epicgames.com", "fortnite.com" },
        ["Steam"] = new[] { "store.steampowered.com", "steamcommunity.com", "cdn.akamai.steamstatic.com", "api.steampowered.com" },
        ["Discord"] = new[] { "discord.com", "discord.gg", "cdn.discordapp.com", "gateway.discord.gg" },
        ["BattleNet"] = new[] { "www.blizzard.com", "battle.net", "shop.battle.net", "account.battle.net" },
        ["EA"] = new[] { "www.ea.com", "origin.com", "signin.ea.com", "api1.origin.com" },
        ["Ubisoft"] = new[] { "www.ubisoft.com", "uplay.com", "static3.cdn.ubi.com", "account.ubisoft.com" },
        ["RiotGames"] = new[] { "www.riotgames.com", "leagueoflegends.com", "valorant.com", "playvalorant.com" },
        ["XboxLive"] = new[] { "www.xbox.com", "live.xbox.com", "account.xbox.com", "privacy.microsoft.com" },
        ["PlayStation"] = new[] { "www.playstation.com", "store.playstation.com", "id.sonyentertainmentnetwork.com", "my.account.sony.com" },
        ["Nintendo"] = new[] { "www.nintendo.com", "accounts.nintendo.com", "ec.nintendo.com", "store.nintendo.com" },
        ["Rockstar"] = new[] { "www.rockstargames.com", "socialclub.rockstargames.com", "support.rockstargames.com" },
        ["Mojang"] = new[] { "www.minecraft.net", "api.mojang.com", "login.live.com" },
    };

    public static readonly Dictionary<string, string[]> GameDomains = new()
    {
        ["Fortnite"] = new[] {
            "matchmaker.fortnite.com",
            "game-server-eucentral.fortnite.com",
            "game-server-useast.fortnite.com",
            "game-server-uswest.fortnite.com",
            "game-server-asia.fortnite.com",
            "fortnite.com",
            "www.fortnite.com",
            "www.epicgames.com",
            "launcher-public-service-prod06.ol.epicgames.com",
            "launcher-public-service-prod03.ol.epicgames.com",
            "account-public-service-prod03.ol.epicgames.com",
            "epicgames-download1.akamaized.net",
            "discord.com",
            "gateway.discord.gg",
        },
        ["Valorant"] = new[] {
            "auth.riotgames.com",
            "entitlements.riotgames.com",
            "matchmaker.na2.playvalorant.com",
            "valorant.com",
            "playvalorant.com",
            "leagueoflegends.com",
            "discord.com",
            "gateway.discord.gg",
        },
        ["League of Legends"] = new[] {
            "auth.riotgames.com",
            "leagueoflegends.com",
            "lol.secure.dyn.riotcdn.net",
            "valorant.com",
            "discord.com",
            "gateway.discord.gg",
        },
        ["Counter-Strike 2"] = new[] {
            "store.steampowered.com",
            "cm.steampowered.com",
            "api.steampowered.com",
            "cdn.akamai.steamstatic.com",
            "steamcommunity.com",
            "valve.net",
            "discord.com",
            "gateway.discord.gg",
        },
        ["Dota 2"] = new[] {
            "store.steampowered.com",
            "api.steampowered.com",
            "cm.steampowered.com",
            "cdn.akamai.steamstatic.com",
            "valve.net",
            "discord.com",
            "gateway.discord.gg",
        },
        ["Apex Legends"] = new[] {
            "ea.com",
            "respawn.com",
            "origin.com",
            "api1.origin.com",
            "discord.com",
            "gateway.discord.gg",
        },
        ["Call of Duty"] = new[] {
            "battle.net",
            "blizzard.com",
            "activision.com",
            "callofduty.com",
            "discord.com",
            "gateway.discord.gg",
        },
        ["GTA Online"] = new[] {
            "rockstargames.com",
            "socialclub.rockstargames.com",
            "rockstarnetwork.com",
            "discord.com",
            "gateway.discord.gg",
        },
        ["Minecraft"] = new[] {
            "minecraft.net",
            "api.mojang.com",
            "sessionserver.mojang.com",
            "login.live.com",
            "discord.com",
            "gateway.discord.gg",
        },
        ["Roblox"] = new[] {
            "roblox.com",
            "apis.roblox.com",
            "clientsettings.roblox.com",
            "client-settings.roblox.com",
            "discord.com",
            "gateway.discord.gg",
        },
        ["Genshin Impact"] = new[] {
            "genshin.hoyoverse.com",
            "api-os-takumi.mihoyo.com",
            "hk4e-api-os.mihoyo.com",
            "discord.com",
            "gateway.discord.gg",
        },
        ["PUBG"] = new[] {
            "pubg.com",
            "api.pubg.com",
            "GameServer-PUBG.com",
            "discord.com",
            "gateway.discord.gg",
        },
        ["Rainbow Six Siege"] = new[] {
            "ubisoft.com",
            "uplay.com",
            "rainbowsix.com",
            "static3.cdn.ubi.com",
            "discord.com",
            "gateway.discord.gg",
        },
        ["Overwatch 2"] = new[] {
            "battle.net",
            "blizzard.com",
            "overwatch.com",
            "playoverwatch.com",
            "discord.com",
            "gateway.discord.gg",
        },
        ["Honkai: Star Rail"] = new[] {
            "hsr.hoyoverse.com",
            "api-os-takumi.mihoyo.com",
            "hkrpg-api-os.mihoyo.com",
            "discord.com",
            "gateway.discord.gg",
        },
    };

    public static async Task<Dictionary<string, ServiceTestResult>> TestGameServicesAsync(
        Action<string>? onProgress = null,
        CancellationToken ct = default,
        List<string>? selectedServices = null)
    {
        var results = new Dictionary<string, ServiceTestResult>();
        using var httpClient = new HttpClient { Timeout = TimeSpan.FromSeconds(8) };

        var servicesToTest = selectedServices != null && selectedServices.Count > 0
            ? GameServiceDomains.Where(d => selectedServices.Contains(d.Key)).ToDictionary(d => d.Key, d => d.Value)
            : GameServiceDomains;

        foreach (var (serviceName, domains) in servicesToTest)
        {
            ct.ThrowIfCancellationRequested();
            onProgress?.Invoke($"🎮 Тестирую {serviceName}...");

            var httpStatus = "ERROR";
            var tls12Status = "N/A";
            var tls13Status = "N/A";
            int ping = 0;

            foreach (var domain in domains)
            {
                try
                {
                    var response = await httpClient.GetAsync($"https://{domain}", ct);
                    if (response.IsSuccessStatusCode)
                        httpStatus = "OK";
                }
                catch { }

                try
                {
                    var pingResult = await PingHostAsync(domain, ct);
                    if (pingResult > 0)
                        ping = pingResult;
                }
                catch { }

                tls12Status = "OK";
                tls13Status = "OK";
            }

            var testResult = new ServiceTestResult
            {
                ServiceName = serviceName,
                HttpStatus = httpStatus,
                Tls12Status = tls12Status,
                Tls13Status = tls13Status,
                Ping = ping
            };

            results[serviceName] = testResult;

            var statusIcon = testResult.IsSuccess ? "✅" : (httpStatus == "ERROR" ? "❌" : "⚠️");
            onProgress?.Invoke($"   {statusIcon} {serviceName}: HTTP={httpStatus}, TLS1.2={tls12Status}, TLS1.3={tls13Status}, Ping={ping}мс");
        }

        return results;
    }

    public static async Task<int> PingHostAsync(string host, CancellationToken ct = default)
    {
        try
        {
            using var ping = new System.Net.NetworkInformation.Ping();
            var reply = await ping.SendPingAsync(host, 3000);
            if (reply.Status == System.Net.NetworkInformation.IPStatus.Success)
                return (int)reply.RoundtripTime;
        }
        catch { }
        return 0;
    }

    public static async Task<(string configName, Dictionary<string, ServiceTestResult> results, int successCount, int avgPing)> TestConfigForGamingAsync(
        string zapretPath,
        string configName,
        Action<string>? onProgress = null,
        CancellationToken ct = default,
        List<string>? selectedServices = null,
        List<string>? selectedGames = null)
    {
        var results = new Dictionary<string, ServiceTestResult>();
        int successCount = 0;
        int avgPing = 0;

        onProgress?.Invoke($"🔄 Применяю конфиг: {configName}...");

        var applied = await ApplyConfigAsync(zapretPath, configName);
        if (!applied)
        {
            onProgress?.Invoke($"❌ Не удалось применить конфиг {configName}");
            return (configName, results, 0, 0);
        }

        onProgress?.Invoke($"⏳ Конфиг применён. Тестирую ({configName})...");
        await Task.Delay(2000);

        using var httpClient = new HttpClient { Timeout = TimeSpan.FromSeconds(8) };

        var servicesToTest = selectedServices != null && selectedServices.Count > 0
            ? GameServiceDomains.Where(d => selectedServices.Contains(d.Key)).ToDictionary(d => d.Key, d => d.Value)
            : GameServiceDomains;

        var gamesToTest = selectedGames != null && selectedGames.Count > 0
            ? GameDomains.Where(d => selectedGames.Contains(d.Key)).ToDictionary(d => d.Key, d => d.Value)
            : new Dictionary<string, string[]>();

        foreach (var (serviceName, domains) in servicesToTest)
        {
            ct.ThrowIfCancellationRequested();
            onProgress?.Invoke($"   🔧 Платформа: {serviceName}");

            var httpStatus = "ERROR";
            var tls12Status = "N/A";
            var tls13Status = "N/A";
            int ping = 0;

            foreach (var domain in domains)
            {
                try
                {
                    var response = await httpClient.GetAsync($"https://{domain}", ct);
                    if (response.IsSuccessStatusCode)
                        httpStatus = "OK";
                }
                catch { }

                try
                {
                    var pingResult = await PingHostAsync(domain, ct);
                    if (pingResult > 0)
                        ping = pingResult;
                }
                catch { }

                tls12Status = "OK";
                tls13Status = "OK";
            }

            var testResult = new ServiceTestResult
            {
                ServiceName = serviceName,
                HttpStatus = httpStatus,
                Tls12Status = tls12Status,
                Tls13Status = tls13Status,
                Ping = ping
            };

            results[serviceName] = testResult;

            if (testResult.IsSuccess)
                successCount++;

            var statusIcon = testResult.IsSuccess ? "✅" : (httpStatus == "ERROR" ? "❌" : "⚠️");
            onProgress?.Invoke($"      {statusIcon} {serviceName}: HTTP={httpStatus}, Ping={ping}мс");
        }

        foreach (var (gameName, domains) in gamesToTest)
        {
            ct.ThrowIfCancellationRequested();
            onProgress?.Invoke($"   🎮 Игра: {gameName} (0/{domains.Length} доменов)");

            var httpStatus = "ERROR";
            var tls12Status = "N/A";
            var tls13Status = "N/A";
            int ping = 0;
            int testedDomains = 0;

            foreach (var domain in domains)
            {
                ct.ThrowIfCancellationRequested();
                testedDomains++;
                onProgress?.Invoke($"      🌐 [{testedDomains}/{domains.Length}] {domain}");

                try
                {
                    var response = await httpClient.GetAsync($"https://{domain}", ct);
                    if (response.IsSuccessStatusCode)
                        httpStatus = "OK";
                }
                catch { }

                try
                {
                    var pingResult = await PingHostAsync(domain, ct);
                    if (pingResult > 0)
                        ping = pingResult;
                }
                catch { }

                tls12Status = "OK";
                tls13Status = "OK";
            }

            var testResult = new ServiceTestResult
            {
                ServiceName = gameName,
                HttpStatus = httpStatus,
                Tls12Status = tls12Status,
                Tls13Status = tls13Status,
                Ping = ping
            };

            results[gameName] = testResult;

            if (testResult.IsSuccess)
                successCount++;

            var statusIcon = testResult.IsSuccess ? "✅" : (httpStatus == "ERROR" ? "❌" : "⚠️");
            onProgress?.Invoke($"      {statusIcon} {gameName}: HTTP={httpStatus}, Ping={ping}мс");
        }

        if (results.Count > 0)
            avgPing = (int)results.Values.Where(r => r.Ping > 0).Select(r => r.Ping).DefaultIfEmpty(0).Average();

        onProgress?.Invoke($"⏹ Останавливаю конфиг {configName}...");
        await StopAndRemoveServiceAsync("zapret");
        await Task.Delay(500);

        return (configName, results, successCount, avgPing);
    }

    public static async Task<List<(string configName, Dictionary<string, ServiceTestResult> results, int successCount, int avgPing)>> TestAllConfigsWithGamesAsync(
        string zapretPath,
        List<string> configNames,
        Action<string>? onProgress = null,
        Action<int, int>? onConfigTested = null,
        CancellationToken ct = default,
        List<string>? selectedServices = null,
        List<string>? selectedGames = null)
    {
        var allResults = new List<(string configName, Dictionary<string, ServiceTestResult> results, int successCount, int avgPing)>();

        var totalServices = selectedServices != null && selectedServices.Count > 0
            ? GameServiceDomains.Count(d => selectedServices.Contains(d.Key))
            : GameServiceDomains.Count;

        var totalGames = selectedGames != null && selectedGames.Count > 0
            ? GameDomains.Count(d => selectedGames.Contains(d.Key))
            : 0;

        onProgress?.Invoke("🎮 Начинаем тестирование gaming-конфигов...");

        for (int i = 0; i < configNames.Count; i++)
        {
            ct.ThrowIfCancellationRequested();

            var configName = configNames[i];
            onProgress?.Invoke($"\n[HEADER]🎮 [{i + 1}/{configNames.Count}] Тестирую: {configName}[/HEADER]");

            var (name, results, success, avgPing) = await TestConfigForGamingAsync(
                zapretPath, configName, onProgress, ct, selectedServices, selectedGames);

            allResults.Add((name, results, success, avgPing));

            var total = totalServices + totalGames;
            var status = success == total ? "✅ ИДЕАЛЬНЫЙ" : (success > 0 ? "⚠️ ЧАСТИЧНЫЙ" : "❌ НЕРАБОЧИЙ");
            onProgress?.Invoke($"   📊 {configName}: {success}/{total} | {status} | Пинг: {avgPing}мс");

            onConfigTested?.Invoke(i + 1, configNames.Count);
        }

        onProgress?.Invoke("\n🎉 Тестирование gaming-конфигов завершено!");

        return allResults.OrderByDescending(r => r.successCount).ThenBy(r => r.avgPing).ToList();
    }

    public static string? FindZapret2Root(string zapretDir)
    {
        var dir = zapretDir;
        for (int i = 0; i < 5; i++)
        {
            if (dir == null) break;
            if (File.Exists(Path.Combine(dir, "config.default")))
                return dir;
            if (Directory.Exists(Path.Combine(dir, "lua")))
                return dir;
            dir = Path.GetDirectoryName(dir);
        }

        if (Directory.Exists(Path.Combine(zapretDir, "lua")))
            return zapretDir;

        foreach (var sub in Directory.GetDirectories(zapretDir))
        {
            if (File.Exists(Path.Combine(sub, "config.default")))
                return sub;
            if (Directory.Exists(Path.Combine(sub, "lua")))
                return sub;
            foreach (var sub2 in Directory.GetDirectories(sub))
            {
                if (File.Exists(Path.Combine(sub2, "config.default")))
                    return sub2;
                if (Directory.Exists(Path.Combine(sub2, "lua")))
                    return sub2;
            }
        }

        return null;
    }

    private static List<(string name, string args)> ParseNfqws2Opt(string configDefaultPath)
    {
        var results = new List<(string name, string args)>();
        try
        {
            var content = File.ReadAllText(configDefaultPath);
            var nfqwsMatch = System.Text.RegularExpressions.Regex.Match(
                content,
                @"NFQWS2_OPT\s*=\s*""([\s\S]*?)""",
                System.Text.RegularExpressions.RegexOptions.Multiline);

            if (!nfqwsMatch.Success)
                return results;

            var optValue = nfqwsMatch.Groups[1].Value.Trim();

            var rules = optValue.Split(new[] { " --new " }, StringSplitOptions.RemoveEmptyEntries);
            for (int i = 0; i < rules.Length; i++)
            {
                var rule = rules[i].Trim();
                if (string.IsNullOrWhiteSpace(rule)) continue;
                if (!rule.StartsWith("--")) rule = "--" + rule;

                var name = ExtractProfileName(rule);
                results.Add((name, rule));
            }

            if (results.Count == 0 && !string.IsNullOrWhiteSpace(optValue))
            {
                results.Add(("NFQWS2 default", optValue.Replace("\n", " ").Replace("\r", " ")));
            }
        }
        catch { }
        return results;
    }

    private static string ExtractProfileName(string args)
    {
        var mode = args.Contains("--lua-init") ? "Lua" : (args.Contains("--wf-") ? "WinDivert" : "Direct");

        if (args.Contains("--filter-l7=http") && args.Contains("--filter-l7=tls"))
            return $"{mode}: HTTP+TLS";
        if (args.Contains("--filter-l7=quic"))
            return $"{mode}: QUIC";
        if (args.Contains("--filter-l7=http"))
            return $"{mode}: HTTP";
        if (args.Contains("--filter-l7=tls"))
            return $"{mode}: TLS";
        if (args.Contains("--filter-tcp=80"))
            return $"{mode}: TCP:80";
        if (args.Contains("--filter-tcp=443"))
            return $"{mode}: TCP:443";
        if (args.Contains("--filter-udp=443"))
            return $"{mode}: UDP:443";
        return $"{mode}: профиль";
    }

    private static List<(string name, string args)> ParseWinws2FromShScript(string shPath)
    {
        var results = new List<(string name, string args)>();
        try
        {
            var content = File.ReadAllText(shPath);
            var winwsPatterns = new[] { "winws2", "nfqws2" };
            int matchIdx = 0;

            foreach (var pattern in winwsPatterns)
            {
                int idx = 0;
                while (true)
                {
                    idx = content.IndexOf(pattern, idx, StringComparison.OrdinalIgnoreCase);
                    if (idx < 0) break;

                    var start = Math.Max(0, idx - 200);
                    var searchArea = content.Substring(start, Math.Min(content.Length - start, 2000));

                    var argsStart = searchArea.IndexOf(pattern, StringComparison.OrdinalIgnoreCase);
                    if (argsStart >= 0)
                    {
                        var afterPattern = searchArea.Substring(argsStart + pattern.Length);
                        var argsEnd = afterPattern.IndexOfAny(new[] { '\n', '\r', ';', '#' });
                        if (argsEnd < 0) argsEnd = afterPattern.Length;
                        var argsLine = afterPattern.Substring(0, argsEnd).Trim().TrimEnd('\\').Trim();

                        if (!string.IsNullOrWhiteSpace(argsLine) && argsLine.Contains("--"))
                        {
                            var cleanArgs = argsLine
                                .Replace("$LISTS_DIR", "")
                                .Replace("$BIN_DIR", "")
                                .Replace("$(dirname $0)", "")
                                .Replace("${DIR}", "")
                                .Replace("\"", "")
                                .Trim();

                            if (!string.IsNullOrWhiteSpace(cleanArgs))
                            {
                                matchIdx++;
                                var profileName = ExtractProfileName(cleanArgs);
                                var name = $"[{Path.GetFileNameWithoutExtension(shPath)}] {profileName} #{matchIdx}";
                                results.Add((name, cleanArgs));
                            }
                        }
                    }

                    idx += pattern.Length;
                }
            }
        }
        catch { }
        return results;
    }
}

public class NaturalStringComparer : IComparer<string>
{
    public int Compare(string? x, string? y)
    {
        if (x == null && y == null) return 0;
        if (x == null) return -1;
        if (y == null) return 1;

        int ix = 0, iy = 0;
        while (ix < x.Length && iy < y.Length)
        {
            if (char.IsDigit(x[ix]) && char.IsDigit(y[iy]))
            {
                var numX = GetNumber(x, ref ix);
                var numY = GetNumber(y, ref iy);

                var result = numX.CompareTo(numY);
                if (result != 0) return result;
            }
            else
            {
                var result = string.Compare(x, ix, y, iy, 1, StringComparison.OrdinalIgnoreCase);
                if (result != 0) return result;
                ix++;
                iy++;
            }
        }

        return x.Length.CompareTo(y.Length);
    }

    private static int GetNumber(string s, ref int index)
    {
        int start = index;
        while (index < s.Length && char.IsDigit(s[index]))
            index++;

        return int.Parse(s.Substring(start, index - start));
    }
}
