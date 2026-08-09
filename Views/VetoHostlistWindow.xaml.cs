using ZapretHub.Veto.Services;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Windows;
using System.Windows.Controls;
using ZapretHub.Core.Services;
using ZapretHub.Core.Services.Mods;
using MessageBox = System.Windows.MessageBox;
using Button = System.Windows.Controls.Button;
using Orientation = System.Windows.Controls.Orientation;
using Application = System.Windows.Application;

namespace ZapretHub.Views
{
    public partial class VetoHostlistWindow : Window
    {
        private readonly string _filesDir;
        private bool _suppressEvents;
        private bool _dirty;

        public string? SelectedHostlistPath { get; private set; }

        public VetoHostlistWindow(string? activeHostlistPath = null)
        {
            InitializeComponent();

            _filesDir = GetFilesDir();
            SelectedHostlistPath = activeHostlistPath;

            try { Directory.CreateDirectory(_filesDir); } catch { }

            LoadFileList();

            if (!string.IsNullOrEmpty(activeHostlistPath))
            {
                var name = Path.GetFileName(activeHostlistPath);
                var item = FilesList.Items.OfType<string>().FirstOrDefault(f =>
                    string.Equals(f, name, StringComparison.OrdinalIgnoreCase));
                if (item != null)
                    FilesList.SelectedItem = item;
            }

            UpdateHeaderInfo();
        }

        public static string GetFilesDir()
        {
            string vetoPath = VetoService.GetVetoPath();
            if (vetoPath != null)
            {
                string dir = Path.Combine(Path.GetDirectoryName(vetoPath) ?? "", "files");
                if (Directory.Exists(dir)) return dir;
            }
            string appDir = AppDomain.CurrentDomain.BaseDirectory;
            string appFiles = Path.Combine(appDir, "Veto", "files");
            if (Directory.Exists(appFiles)) return appFiles;
            return Path.Combine(appDir, "Veto", "files");
        }

        private void LoadFileList()
        {
            _suppressEvents = true;
            FilesList.Items.Clear();
            try
            {
                var files = Directory.GetFiles(_filesDir, "*.txt")
                    .Select(Path.GetFileName)
                    .OrderBy(f => f, StringComparer.OrdinalIgnoreCase);
                foreach (var f in files)
                    FilesList.Items.Add(f);
            }
            catch { }
            _suppressEvents = false;
        }

        private void RefreshCurrentEditor()
        {
            if (FilesList.SelectedItem is string name)
            {
                CurrentFileName.Text = name;
                ActiveBadge.Visibility = IsActiveFile(name) ? Visibility.Visible : Visibility.Collapsed;
                SetActiveBtn.IsEnabled = true;
                DeleteBtn.IsEnabled = true;
                Editor.IsEnabled = true;

                _suppressEvents = true;
                try
                {
                    string path = Path.Combine(_filesDir, name);
                    Editor.Text = File.Exists(path) ? File.ReadAllText(path) : "";
                }
                catch { Editor.Text = ""; }
                _suppressEvents = false;
                _dirty = false;
                UpdateDomainCount();
            }
            else
            {
                CurrentFileName.Text = "Нет файла";
                ActiveBadge.Visibility = Visibility.Collapsed;
                SetActiveBtn.IsEnabled = false;
                DeleteBtn.IsEnabled = false;
                Editor.IsEnabled = false;
                Editor.Text = "";
                DomainCountLbl.Text = "";
            }
        }

        private bool IsActiveFile(string name)
        {
            return !string.IsNullOrEmpty(SelectedHostlistPath) &&
                   string.Equals(Path.GetFileName(SelectedHostlistPath), name, StringComparison.OrdinalIgnoreCase);
        }

        private void UpdateDomainCount()
        {
            int count = 0;
            if (FilesList.SelectedItem is string name)
            {
                string path = Path.Combine(_filesDir, name);
                try
                {
                    if (File.Exists(path))
                    {
                        foreach (var line in File.ReadAllLines(path))
                        {
                            var trimmed = line.Trim();
                            if (trimmed.Length > 0 && !trimmed.StartsWith("#"))
                                count++;
                        }
                    }
                }
                catch { }
            }
            DomainCountLbl.Text = count == 1 ? "1 домен" : $"{count} доменов";
        }

        private void UpdateHeaderInfo()
        {
            string activeName = string.IsNullOrEmpty(SelectedHostlistPath) ? "не выбран" : Path.GetFileName(SelectedHostlistPath);
            HeaderInfo.Text = $"активен: {activeName}";
        }

        private void FilesList_SelectionChanged(object sender, SelectionChangedEventArgs e)
        {
            if (_suppressEvents) return;
            RefreshCurrentEditor();
        }

        private void Editor_TextChanged(object sender, TextChangedEventArgs e)
        {
            if (_suppressEvents) return;
            _dirty = true;
        }

        private bool ConfirmDiscard()
        {
            if (!_dirty) return true;
            var res = MessageBox.Show(this, "Файл был изменён, но не сохранён. Сохранить изменения?",
                "Несохранённые изменения", MessageBoxButton.YesNoCancel, MessageBoxImage.Warning);            if (res == MessageBoxResult.Cancel) return false;
            if (res == MessageBoxResult.Yes) return SaveCurrent();
            return true;
        }

        private bool SaveCurrent()
        {
            if (FilesList.SelectedItem is not string name) return false;
            string path = Path.Combine(_filesDir, name);
            try
            {
                File.WriteAllText(path, Editor.Text.Replace("\r\n", "\n").Replace('\r', '\n'));
                _dirty = false;
                SetStatus($"Сохранено: {name}");
                UpdateDomainCount();
                return true;
            }
            catch (Exception ex)
            {
                SetStatus($"Ошибка сохранения: {ex.Message}");
                return false;
            }
        }

        private void SetActiveBtn_Click(object sender, RoutedEventArgs e)
        {
            if (FilesList.SelectedItem is not string name) return;
            if (!SaveCurrent()) return;
            SelectedHostlistPath = Path.Combine(_filesDir, name);
            RefreshCurrentEditor();
            UpdateHeaderInfo();
            SetStatus($"Активный hostlist: {name}");
        }

        private void DeleteBtn_Click(object sender, RoutedEventArgs e)
        {
            if (FilesList.SelectedItem is not string name) return;
            var res = MessageBox.Show(this, $"Удалить файл «{name}»? Это действие нельзя отменить.",
                "Удаление", MessageBoxButton.YesNo, MessageBoxImage.Warning);
            if (res != MessageBoxResult.Yes) return;

            try
            {
                File.Delete(Path.Combine(_filesDir, name));
                if (IsActiveFile(name))
                {
                    SelectedHostlistPath = null;
                    UpdateHeaderInfo();
                }
                _dirty = false;
                LoadFileList();
                RefreshCurrentEditor();
                SetStatus($"Удалено: {name}");
            }
            catch (Exception ex)
            {
                SetStatus($"Ошибка удаления: {ex.Message}");
            }
        }

        private void NewBtn_Click(object sender, RoutedEventArgs e)
        {
            if (!ConfirmDiscard()) return;

            var dlg = new InputDialog("Имя нового hostlist", "list-general.txt", "Название файла (.txt)");
            if (dlg.ShowDialog() != true) return;

            string raw = dlg.Value.Trim();
            if (raw.Length == 0) return;
            if (!raw.EndsWith(".txt", StringComparison.OrdinalIgnoreCase)) raw += ".txt";

            string safe = string.Concat(raw.Where(c => !Path.GetInvalidFileNameChars().Contains(c)));
            string path = Path.Combine(_filesDir, safe);

            if (File.Exists(path))
            {
                MessageBox.Show(this, $"Файл «{safe}» уже существует.", "Ошибка",
                    MessageBoxButton.OK, MessageBoxImage.Warning);
                return;
            }

            try
            {
                File.WriteAllText(path, "");
                _dirty = false;
                LoadFileList();
                var item = FilesList.Items.OfType<string>().FirstOrDefault(f =>
                    string.Equals(f, safe, StringComparison.OrdinalIgnoreCase));
                if (item != null)
                {
                    FilesList.SelectedItem = item;
                    Editor.Focus();
                }
                SetStatus($"Создан: {safe}");
            }
            catch (Exception ex)
            {
                SetStatus($"Ошибка создания: {ex.Message}");
            }
        }

        private async void ImportBtn_Click(object sender, RoutedEventArgs e)
        {
            var dlg = new InputDialog("Импорт из URL", "https://info.dns.malw.link/hosts",
                "URL списка доменов (одна строка или hostlist)");
            if (dlg.ShowDialog() != true) return;

            string url = dlg.Value.Trim();
            if (url.Length == 0) return;

            SetStatus("Загрузка списка...");
            var (content, error) = await DomainListImporter.DownloadFromUrlAsync(url);
            if (content == null)
            {
                SetStatus($"Импорт не удался: {error}");
                return;
            }

            if (!ConfirmDiscard()) return;

            string name = DomainListImporter.NameFromUrl(url) + ".txt";
            string path = Path.Combine(_filesDir, name);

            if (File.Exists(path))
            {
                var res = MessageBox.Show(this, $"Файл «{name}» уже существует. Перезаписать?",
                    "Импорт", MessageBoxButton.YesNo, MessageBoxImage.Question);
                if (res != MessageBoxResult.Yes) return;
            }

            try
            {
                File.WriteAllText(path, content);
                _dirty = false;
                LoadFileList();
                var item = FilesList.Items.OfType<string>().FirstOrDefault(f =>
                    string.Equals(f, name, StringComparison.OrdinalIgnoreCase));
                if (item != null) FilesList.SelectedItem = item;
                SetStatus($"Импортировано: {name} ({content.Split('\n').Length} строк)");
            }
            catch (Exception ex)
            {
                SetStatus($"Ошибка импорта: {ex.Message}");
            }
        }

        private void SetStatus(string text) => StatusLbl.Text = text;

        private void Header_MouseLeftButtonDown(object sender, System.Windows.Input.MouseButtonEventArgs e)
        {
            if (e.ButtonState == System.Windows.Input.MouseButtonState.Pressed)
                DragMove();
        }

        private void MinBtn_Click(object sender, RoutedEventArgs e)
        {
            WindowState = WindowState.Minimized;
        }

        private void CloseBtn_Click(object sender, RoutedEventArgs e)
        {
            if (!ConfirmDiscard()) return;
            DialogResult = true;
            Close();
        }
    }

    public class InputDialog : Window
    {
        private readonly System.Windows.Controls.TextBox _input;

        public string Value => _input.Text;

        public InputDialog(string title, string defaultValue, string hint)
        {
            Title = title;
            Width = 420; Height = 190;
            WindowStartupLocation = WindowStartupLocation.CenterOwner;
            Owner = Application.Current.MainWindow;
            WindowStyle = WindowStyle.None;
            ResizeMode = ResizeMode.NoResize;
            AllowsTransparency = true;
            Background = System.Windows.Media.Brushes.Transparent;

            var border = new Border
            {
                Background = new System.Windows.Media.SolidColorBrush(System.Windows.Media.Color.FromRgb(0x03, 0x03, 0x06)),
                CornerRadius = new CornerRadius(12),
                BorderBrush = new System.Windows.Media.SolidColorBrush(System.Windows.Media.Color.FromRgb(0x0a, 0x0a, 0x12)),
                BorderThickness = new Thickness(1)
            };

            var panel = new StackPanel { Margin = new Thickness(20) };

            panel.Children.Add(new TextBlock
            {
                Text = title,
                FontFamily = new System.Windows.Media.FontFamily("Cascadia Code, Consolas, monospace"),
                FontSize = 14,
                FontWeight = FontWeights.SemiBold,
                Foreground = new System.Windows.Media.SolidColorBrush(System.Windows.Media.Color.FromRgb(0xf0, 0xf0, 0xf0)),
                Margin = new Thickness(0, 0, 0, 4)
            });

            panel.Children.Add(new TextBlock
            {
                Text = hint,
                FontFamily = new System.Windows.Media.FontFamily("Cascadia Code, Consolas, monospace"),
                FontSize = 11,
                Foreground = new System.Windows.Media.SolidColorBrush(System.Windows.Media.Color.FromRgb(0x5a, 0x6a, 0x7a)),
                Margin = new Thickness(0, 0, 0, 10)
            });

            _input = new System.Windows.Controls.TextBox
            {
                Text = defaultValue,
                FontFamily = new System.Windows.Media.FontFamily("Cascadia Code, Consolas, monospace"),
                FontSize = 13,
                Foreground = new System.Windows.Media.SolidColorBrush(System.Windows.Media.Color.FromRgb(0xe0, 0xe0, 0xe0)),
                Background = new System.Windows.Media.SolidColorBrush(System.Windows.Media.Color.FromRgb(0x08, 0x08, 0x0e)),
                BorderBrush = new System.Windows.Media.SolidColorBrush(System.Windows.Media.Color.FromRgb(0x18, 0x18, 0x20)),
                Padding = new Thickness(10, 7, 10, 7)
            };
            panel.Children.Add(_input);

            var btns = new StackPanel
            {
                Orientation = Orientation.Horizontal,
                HorizontalAlignment = System.Windows.HorizontalAlignment.Right,
                Margin = new Thickness(0, 14, 0, 0)
            };

            var ok = new Button
            {
                Content = "OK",
                Width = 80, Height = 32,
                Margin = new Thickness(0, 0, 8, 0),
                Background = new System.Windows.Media.SolidColorBrush(System.Windows.Media.Color.FromRgb(0x00, 0xff, 0x88)),
                Foreground = System.Windows.Media.Brushes.Black,
                FontFamily = new System.Windows.Media.FontFamily("Cascadia Code, Consolas, monospace"),
                BorderThickness = new Thickness(0),
                Cursor = System.Windows.Input.Cursors.Hand
            };
            ok.Click += (s, e) => { DialogResult = true; };

            var cancel = new Button
            {
                Content = "Отмена",
                Width = 80, Height = 32,
                Foreground = new System.Windows.Media.SolidColorBrush(System.Windows.Media.Color.FromRgb(0xa0, 0xa0, 0xb0)),
                Background = System.Windows.Media.Brushes.Transparent,
                BorderBrush = new System.Windows.Media.SolidColorBrush(System.Windows.Media.Color.FromRgb(0x18, 0x18, 0x20)),
                BorderThickness = new Thickness(1),
                FontFamily = new System.Windows.Media.FontFamily("Cascadia Code, Consolas, monospace"),
                Cursor = System.Windows.Input.Cursors.Hand
            };
            cancel.Click += (s, e) => { DialogResult = false; };

            btns.Children.Add(ok);
            btns.Children.Add(cancel);
            panel.Children.Add(btns);
            border.Child = panel;
            Content = border;

            Loaded += (s, e) => { _input.Focus(); _input.SelectAll(); };
            KeyDown += (s, e) =>
            {
                if (e.Key == System.Windows.Input.Key.Enter) DialogResult = true;
                if (e.Key == System.Windows.Input.Key.Escape) DialogResult = false;
            };
        }
    }
}
