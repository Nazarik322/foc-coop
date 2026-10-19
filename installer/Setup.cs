// foc-coop installer: Next -> Next -> game folder (found automatically) -> Install.
// Puts dinput8.dll and foc_relay\ (FoCRelay.exe + steam_api.dll) into <game>\Binaries. The files are embedded as
// resources (build_installer.bat). An existing foreign dinput8.dll is kept as dinput8.dll.foccoop-backup.
// Command line for tests: FoCCoopSetup.exe /install "<game folder>" /silent   or   /uninstall "<game folder>" /silent
// Compiled with the .NET Framework csc.exe that ships with Windows (C# 5 syntax only).
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.Globalization;
using System.IO;
using System.Reflection;
using System.Security.Cryptography;
using System.Text.RegularExpressions;
using System.Windows.Forms;
using Microsoft.Win32;

static class Program
{
    [STAThread]
    static int Main(string[] args)
    {
        string mode = null, path = null; bool silent = false;
        for (int i = 0; i < args.Length; i++)
        {
            string a = args[i].ToLowerInvariant();
            if ((a == "/install" || a == "/uninstall") && i + 1 < args.Length) { mode = a.Substring(1); path = args[++i]; }
            else if (a == "/silent") silent = true;
        }
        if (mode != null && silent)
        {
            string msg; bool ok = mode == "install" ? Installer.Install(path, out msg) : Installer.Uninstall(path, out msg);
            Console.WriteLine(msg);
            return ok ? 0 : 1;
        }
        Application.EnableVisualStyles();
        Application.SetCompatibleTextRenderingDefault(false);
        Application.Run(new Wizard(mode, path));
        return 0;
    }
}

static class T   // texts: Russian when Windows is Russian, else English
{
    static readonly bool Ru = CultureInfo.CurrentUICulture.TwoLetterISOLanguageName == "ru";
    public static string S(string en, string ru) { return Ru ? ru : en; }
}

static class Installer
{
    public const string ExpectedSha256 = "c1dab177f81503ecb831d63a3d799f928dd9823242ca3689482c684396acfedc";
    public const string GameFolder = "Transformers Fall of Cybertron";
    static readonly string[][] Payload = new string[][] {   // resource name, path under Binaries
        new string[] { "dinput8.dll", "dinput8.dll" },
        new string[] { "FoCRelay.exe", @"foc_relay\FoCRelay.exe" },
        new string[] { "steam_api.dll", @"foc_relay\steam_api.dll" },
    };

    // Steam (registry) -> every library in libraryfolders.vdf -> steamapps\common\Transformers Fall of Cybertron
    public static List<string> FindGame()
    {
        var found = new List<string>();
        var roots = new List<string>();
        foreach (var key in new string[] { @"HKEY_CURRENT_USER\Software\Valve\Steam", @"HKEY_LOCAL_MACHINE\SOFTWARE\WOW6432Node\Valve\Steam", @"HKEY_LOCAL_MACHINE\SOFTWARE\Valve\Steam" })
        {
            foreach (var name in new string[] { "SteamPath", "InstallPath" })
            {
                try { var v = Registry.GetValue(key, name, null) as string; if (!string.IsNullOrEmpty(v)) roots.Add(v.Replace('/', '\\')); } catch { }
            }
        }
        roots.Add(@"C:\Program Files (x86)\Steam"); roots.Add(@"C:\Program Files\Steam");
        var libs = new List<string>();
        foreach (var r in roots)
        {
            if (!libs.Contains(r.ToLowerInvariant())) libs.Add(r.ToLowerInvariant());
            string vdf = Path.Combine(r, @"steamapps\libraryfolders.vdf");
            if (!File.Exists(vdf)) continue;
            try
            {
                foreach (Match m in Regex.Matches(File.ReadAllText(vdf), "\"path\"\\s+\"([^\"]+)\""))
                {
                    string p = m.Groups[1].Value.Replace(@"\\", @"\").ToLowerInvariant();
                    if (!libs.Contains(p)) libs.Add(p);
                }
            }
            catch { }
        }
        foreach (var l in libs)
        {
            string g = Path.Combine(l, @"steamapps\common\" + GameFolder);
            if (File.Exists(Path.Combine(g, @"Binaries\TFOC.exe")) && !found.Exists(x => string.Equals(x, g, StringComparison.OrdinalIgnoreCase))) found.Add(g);
        }
        return found;
    }

    // accepts the game folder or its Binaries folder
    public static string BinariesOf(string path)
    {
        if (string.IsNullOrEmpty(path)) return null;
        path = path.Trim().TrimEnd('\\');
        if (File.Exists(Path.Combine(path, "TFOC.exe"))) return path;
        if (File.Exists(Path.Combine(path, @"Binaries\TFOC.exe"))) return Path.Combine(path, "Binaries");
        return null;
    }

    public static string Sha256(string file)
    {
        using (var s = File.OpenRead(file)) using (var h = SHA256.Create())
            return BitConverter.ToString(h.ComputeHash(s)).Replace("-", "").ToLowerInvariant();
    }

    public static bool IsInstalled(string bin) { return bin != null && File.Exists(Path.Combine(bin, @"foc_relay\FoCRelay.exe")); }

    static bool GameRunning(string bin)
    {
        foreach (var p in Process.GetProcessesByName("TFOC"))
        {
            try { if (string.Equals(Path.GetDirectoryName(p.MainModule.FileName), bin, StringComparison.OrdinalIgnoreCase)) return true; } catch { }
        }
        foreach (var p in Process.GetProcessesByName("FoCRelay"))
        {
            try { if (p.MainModule.FileName.StartsWith(bin, StringComparison.OrdinalIgnoreCase)) return true; } catch { }
        }
        return false;
    }

    public static bool Install(string path, out string msg)
    {
        string bin = BinariesOf(path);
        if (bin == null) { msg = T.S("TFOC.exe was not found in that folder.", "В этой папке не найден TFOC.exe."); return false; }
        if (GameRunning(bin)) { msg = T.S("Close the game first.", "Сначала закройте игру."); return false; }
        try
        {
            string dll = Path.Combine(bin, "dinput8.dll"), backup = dll + ".foccoop-backup";
            if (File.Exists(dll) && !File.Exists(backup) && !IsOurs(dll)) File.Copy(dll, backup);
            Directory.CreateDirectory(Path.Combine(bin, "foc_relay"));
            var asm = Assembly.GetExecutingAssembly();
            foreach (var p in Payload)
            {
                using (var src = asm.GetManifestResourceStream(p[0]))
                using (var dst = File.Create(Path.Combine(bin, p[1])))
                    src.CopyTo(dst);
            }
            File.WriteAllText(Path.Combine(bin, @"foc_relay\steam_appid.txt"), "480");
            msg = T.S("Installed into ", "Установлено в ") + bin;
            return true;
        }
        catch (UnauthorizedAccessException) { msg = "ACCESS"; return false; }
        catch (Exception e) { msg = e.Message; return false; }
    }

    static bool IsOurs(string dll)
    {
        try { return File.ReadAllText(dll).Contains("foc_coop"); } catch { return false; }
    }

    public static bool Uninstall(string path, out string msg)
    {
        string bin = BinariesOf(path);
        if (bin == null) { msg = T.S("TFOC.exe was not found in that folder.", "В этой папке не найден TFOC.exe."); return false; }
        if (GameRunning(bin)) { msg = T.S("Close the game first.", "Сначала закройте игру."); return false; }
        try
        {
            string dll = Path.Combine(bin, "dinput8.dll"), backup = dll + ".foccoop-backup";
            if (File.Exists(dll) && IsOurs(dll)) File.Delete(dll);
            if (File.Exists(backup) && !File.Exists(dll)) File.Move(backup, dll);
            string relay = Path.Combine(bin, "foc_relay");
            if (Directory.Exists(relay)) Directory.Delete(relay, true);
            msg = T.S("The mod was removed from ", "Мод удалён из ") + bin;
            return true;
        }
        catch (UnauthorizedAccessException) { msg = "ACCESS"; return false; }
        catch (Exception e) { msg = e.Message; return false; }
    }
}

class Wizard : Form
{
    int page;
    readonly Label title = new Label(), body = new Label();
    readonly Button back = new Button(), next = new Button(), cancel = new Button(), browse = new Button();
    readonly ComboBox pathBox = new ComboBox();
    readonly CheckBox uninstall = new CheckBox();
    readonly Label pathInfo = new Label();
    bool done;

    public Wizard(string mode, string path)
    {
        Text = "Fall of Cybertron Co-op " + T.S("Setup", "— установка");
        ClientSize = new Size(620, 400); FormBorderStyle = FormBorderStyle.FixedDialog; MaximizeBox = false; StartPosition = FormStartPosition.CenterScreen;
        Font = new Font("Segoe UI", 9.5f);
        var banner = new Panel { BackColor = Color.FromArgb(28, 33, 46), Dock = DockStyle.Top, Height = 70 };
        title.ForeColor = Color.White; title.Font = new Font("Segoe UI", 14f, FontStyle.Bold); title.AutoSize = false; title.SetBounds(20, 18, 580, 36);
        banner.Controls.Add(title); Controls.Add(banner);
        body.SetBounds(24, 88, 572, 104); body.AutoSize = false; Controls.Add(body);
        pathBox.SetBounds(24, 200, 470, 26); pathBox.DropDownStyle = ComboBoxStyle.DropDown; Controls.Add(pathBox);
        browse.Text = T.S("Browse...", "Обзор..."); browse.SetBounds(502, 199, 94, 28); Controls.Add(browse);
        pathInfo.SetBounds(24, 234, 572, 60); pathInfo.AutoSize = false; Controls.Add(pathInfo);
        uninstall.Text = T.S("Remove the mod from this folder instead", "Вместо установки удалить мод из этой папки"); uninstall.SetBounds(24, 296, 572, 24); Controls.Add(uninstall);
        pathBox.BringToFront(); browse.BringToFront(); pathInfo.BringToFront(); uninstall.BringToFront();
        back.Text = T.S("< Back", "< Назад"); back.SetBounds(300, 352, 94, 30); Controls.Add(back);
        next.SetBounds(400, 352, 94, 30); Controls.Add(next);
        cancel.Text = T.S("Cancel", "Отмена"); cancel.SetBounds(502, 352, 94, 30); Controls.Add(cancel);
        back.Click += (s, e) => { if (page > 0) Show(page - 1); };
        next.Click += (s, e) => Next();
        cancel.Click += (s, e) => Close();
        browse.Click += (s, e) => { using (var d = new FolderBrowserDialog()) { d.Description = T.S("Fall of Cybertron folder", "Папка Fall of Cybertron"); if (d.ShowDialog(this) == DialogResult.OK) { pathBox.Text = d.SelectedPath; CheckPath(); } } };
        pathBox.TextChanged += (s, e) => CheckPath();
        foreach (var g in Installer.FindGame()) pathBox.Items.Add(g);
        if (!string.IsNullOrEmpty(path)) pathBox.Text = path; else if (pathBox.Items.Count > 0) pathBox.SelectedIndex = 0;
        if (mode == "uninstall") uninstall.Checked = true;
        Show(mode != null ? 2 : 0);
    }

    void Show(int p)
    {
        page = p;
        bool pathPage = p == 2;
        pathBox.Visible = browse.Visible = pathInfo.Visible = uninstall.Visible = pathPage;
        back.Enabled = p > 0 && p < 3;
        next.Text = p == 2 ? (uninstall.Checked ? T.S("Remove", "Удалить") : T.S("Install", "Установить")) : p == 3 ? T.S("Finish", "Готово") : T.S("Next >", "Далее >");
        cancel.Visible = p < 3;
        if (p == 0)
        {
            title.Text = T.S("Fall of Cybertron Co-op", "Кооператив для Fall of Cybertron");
            body.Text = T.S("This installs the unofficial co-op mod for Transformers: Fall of Cybertron (Steam version).\n\nPlay the story and Escalation with up to 5 friends. In the game press F10 for the co-op menu.",
                            "Будет установлен неофициальный кооп-мод для Transformers: Fall of Cybertron (версия из Steam).\n\nСюжет и Escalation вместе с друзьями (до 5 человек). В игре нажмите F10 — откроется меню кооператива.");
        }
        else if (p == 1)
        {
            title.Text = T.S("Before you install", "Перед установкой");
            body.Text = T.S("• Every player needs their own copy of the game and this mod.\n• Steam must be running: friends join you through Steam, no IP addresses or port forwarding.\n• Host: F10 → Host a game. Friend: F10 → Join a game → Join a friend through Steam.\n• The mod adds dinput8.dll and the foc_relay folder to the game's Binaries folder; nothing else is changed.",
                            "• Каждому игроку нужна своя копия игры и этот мод.\n• Steam должен быть запущен: друзья подключаются через Steam, без IP-адресов и проброса портов.\n• Хост: F10 → Host a game. Друг: F10 → Join a game → Join a friend through Steam.\n• Мод добавляет dinput8.dll и папку foc_relay в папку Binaries игры, больше ничего не меняется.");
        }
        else if (p == 2)
        {
            title.Text = T.S("Game folder", "Папка с игрой");
            body.Text = pathBox.Items.Count > 0 ? T.S("The game was found automatically. Check the folder and press Install.", "Игра найдена автоматически. Проверьте папку и нажмите «Установить».")
                                                : T.S("The game was not found automatically. Choose the Fall of Cybertron folder.", "Игру не удалось найти автоматически. Укажите папку Fall of Cybertron.");
            CheckPath();
        }
        uninstall.CheckedChanged -= OnUninstallChanged; uninstall.CheckedChanged += OnUninstallChanged;
    }
    void OnUninstallChanged(object s, EventArgs e) { if (page == 2) next.Text = uninstall.Checked ? T.S("Remove", "Удалить") : T.S("Install", "Установить"); }

    void CheckPath()
    {
        if (page != 2) return;
        string bin = Installer.BinariesOf(pathBox.Text);
        if (bin == null) { pathInfo.ForeColor = Color.Firebrick; pathInfo.Text = T.S("TFOC.exe is not in this folder.", "В этой папке нет TFOC.exe."); next.Enabled = false; return; }
        next.Enabled = true;
        string hash = ""; try { hash = Installer.Sha256(Path.Combine(bin, "TFOC.exe")); } catch { }
        bool known = hash == Installer.ExpectedSha256;
        pathInfo.ForeColor = known ? Color.DarkGreen : Color.DarkOrange;
        pathInfo.Text = (known ? T.S("Supported game version (Steam build 12831).", "Поддерживаемая версия игры (Steam, сборка 12831).")
                               : T.S("Unknown TFOC.exe version: the mod is made for the Steam build 12831 and may not work.", "Неизвестная версия TFOC.exe: мод сделан для Steam-сборки 12831 и может не работать."))
                        + (Installer.IsInstalled(bin) ? T.S("\nThe mod is already installed here; Install updates it.", "\nМод уже установлен сюда; «Установить» обновит его.") : "");
    }

    void Next()
    {
        if (page < 2) { Show(page + 1); return; }
        if (page == 3) { Close(); return; }
        string msg;
        bool ok = uninstall.Checked ? Installer.Uninstall(pathBox.Text, out msg) : Installer.Install(pathBox.Text, out msg);
        if (!ok && msg == "ACCESS")
        {   // e.g. Program Files: run again elevated
            try
            {
                var psi = new ProcessStartInfo(Application.ExecutablePath, (uninstall.Checked ? "/uninstall" : "/install") + " \"" + pathBox.Text + "\" /silent") { Verb = "runas", UseShellExecute = true };
                var p = Process.Start(psi); p.WaitForExit();
                ok = p.ExitCode == 0;
                msg = ok ? (uninstall.Checked ? T.S("The mod was removed.", "Мод удалён.") : T.S("Installed into ", "Установлено в ") + Installer.BinariesOf(pathBox.Text))
                         : T.S("Installing with administrator rights failed.", "Установка с правами администратора не удалась.");
            }
            catch { msg = T.S("Administrator rights are needed for this folder.", "Для этой папки нужны права администратора."); }
        }
        if (!ok) { MessageBox.Show(this, msg, Text, MessageBoxButtons.OK, MessageBoxIcon.Warning); return; }
        done = true;
        Show(3);
        title.Text = T.S("Done", "Готово");
        body.Text = msg + (uninstall.Checked ? "" : T.S("\n\nStart the game from Steam and press F10.", "\n\nЗапустите игру из Steam и нажмите F10."));
    }

    protected override void OnFormClosing(FormClosingEventArgs e) { base.OnFormClosing(e); }
}
