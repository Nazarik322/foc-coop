// foc-coop installer: Next -> Next -> game folder (found automatically) -> Install.
// Puts dinput8.dll and foc_relay\ (FoCRelay.exe + steam_api.dll) into <game>\Binaries. The files are embedded as
// resources (build_installer.bat). An existing foreign dinput8.dll is kept as dinput8.dll.foccoop-backup.
// Command line for tests: FoCCoopSetup.exe /install "<game folder>" /silent [/mods audiologs,fmv,nointro,configurator]
//   [/modsdir <folder with the mod archives>]   or   /uninstall "<game folder>" /silent
// Optional Nexus mods (Mods.cs) are installed from archives the player downloaded.
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
        string mode = null, path = null, mods = "", modsDir = null; bool silent = false;
        for (int i = 0; i < args.Length; i++)
        {
            string a = args[i].ToLowerInvariant();
            if ((a == "/install" || a == "/uninstall") && i + 1 < args.Length) { mode = a.Substring(1); path = args[++i]; }
            else if (a == "/silent") silent = true;
            else if (a == "/mods" && i + 1 < args.Length) mods = args[++i];
            else if (a == "/modsdir" && i + 1 < args.Length) modsDir = args[++i];
        }
        if (mode != null && silent)
        {
            string msg; bool ok = mode == "install" ? Installer.Install(path, out msg) : Installer.Uninstall(path, out msg);
            if (ok && mode == "install" && mods.Length > 0)
            {
                try { Mods.Find(modsDir); Mods.Install(Installer.GameOf(path), new List<string>(mods.Split(',')), l => Console.WriteLine(l)); msg += Environment.NewLine + "mods: " + mods; }
                catch (Exception e) { msg += Environment.NewLine + "mods failed: " + e.Message; ok = false; }
            }
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

    public static string GameOf(string path) { string b = BinariesOf(path); return b == null ? null : Path.GetDirectoryName(b); }

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
            Mods.Uninstall(Path.GetDirectoryName(bin));
            msg = T.S("The mod was removed from ", "Мод удалён из ") + bin;
            return true;
        }
        catch (UnauthorizedAccessException) { msg = "ACCESS"; return false; }
        catch (Exception e) { msg = e.Message; return false; }
    }
}

class Wizard : Form
{
    // pages: 0 welcome, 1 before you install, 2 game folder, 3 mods, 4 waiting for downloads, 5 done
    int page;
    readonly Label title = new Label(), body = new Label();
    readonly Button back = new Button(), next = new Button(), cancel = new Button(), browse = new Button(), modBrowse = new Button(), openPages = new Button();
    readonly ComboBox pathBox = new ComboBox();
    readonly CheckBox uninstall = new CheckBox();
    readonly Label pathInfo = new Label(), modInfo = new Label(), waitInfo = new Label();
    readonly CheckedListBox modList = new CheckedListBox();
    readonly Timer waitTimer = new Timer();
    readonly HashSet<string> opened = new HashSet<string>();   // Nexus pages already opened in the browser
    readonly HashSet<string> chosen = new HashSet<string>();   // mod ids ticked on the mods page
    string modDir;
    bool busy, filling;

    public Wizard(string mode, string path)
    {
        Text = "Fall of Cybertron Co-op " + T.S("Setup", "— установка");
        ClientSize = new Size(620, 400); FormBorderStyle = FormBorderStyle.FixedDialog; MaximizeBox = false; StartPosition = FormStartPosition.CenterScreen;
        Font = new Font("Segoe UI", 9.5f);
        var banner = new Panel { BackColor = Color.FromArgb(28, 33, 46), Dock = DockStyle.Top, Height = 70 };
        title.ForeColor = Color.White; title.Font = new Font("Segoe UI", 14f, FontStyle.Bold); title.AutoSize = false; title.SetBounds(20, 18, 580, 36);
        banner.Controls.Add(title); Controls.Add(banner);
        body.SetBounds(24, 82, 572, 66); body.AutoSize = false; Controls.Add(body);
        pathBox.SetBounds(24, 200, 470, 26); pathBox.DropDownStyle = ComboBoxStyle.DropDown; Controls.Add(pathBox);
        browse.Text = T.S("Browse...", "Обзор..."); browse.SetBounds(502, 199, 94, 28); Controls.Add(browse);
        pathInfo.SetBounds(24, 234, 572, 60); pathInfo.AutoSize = false; Controls.Add(pathInfo);
        uninstall.Text = T.S("Remove the mod from this folder instead", "Вместо установки удалить мод из этой папки"); uninstall.SetBounds(24, 296, 572, 24); Controls.Add(uninstall);
        modList.SetBounds(24, 150, 572, 112); modList.CheckOnClick = true; modList.IntegralHeight = false; Controls.Add(modList);
        modInfo.SetBounds(24, 268, 470, 76); modInfo.AutoSize = false; Controls.Add(modInfo);
        modBrowse.Text = T.S("Folder...", "Папка..."); modBrowse.SetBounds(502, 268, 94, 28); Controls.Add(modBrowse);
        openPages.Text = T.S("Open pages", "Открыть страницы"); openPages.SetBounds(24, 312, 160, 30); Controls.Add(openPages);
        waitInfo.SetBounds(24, 150, 572, 160); waitInfo.AutoSize = false; Controls.Add(waitInfo);
        pathBox.BringToFront(); browse.BringToFront(); pathInfo.BringToFront(); uninstall.BringToFront(); modList.BringToFront(); modInfo.BringToFront(); modBrowse.BringToFront(); waitInfo.BringToFront(); openPages.BringToFront();
        back.Text = T.S("< Back", "< Назад"); back.SetBounds(300, 352, 94, 30); Controls.Add(back);
        next.SetBounds(400, 352, 94, 30); Controls.Add(next);
        cancel.Text = T.S("Cancel", "Отмена"); cancel.SetBounds(502, 352, 94, 30); Controls.Add(cancel);
        back.Click += (s, e) => { if (page > 0 && !busy) Show(page == 4 ? 3 : page - 1); };
        next.Click += (s, e) => Next();
        cancel.Click += (s, e) => { if (!busy) Close(); };
        browse.Click += (s, e) => { using (var d = new FolderBrowserDialog()) { d.Description = T.S("Fall of Cybertron folder", "Папка Fall of Cybertron"); if (d.ShowDialog(this) == DialogResult.OK) { pathBox.Text = d.SelectedPath; CheckPath(); } } };
        modBrowse.Click += (s, e) => { using (var d = new FolderBrowserDialog()) { d.Description = T.S("Folder with the downloaded mod archives", "Папка со скачанными архивами модов"); if (d.ShowDialog(this) == DialogResult.OK) { modDir = d.SelectedPath; FillMods(); } } };
        openPages.Click += (s, e) => { foreach (var m in Missing()) Mods.OpenPage(m); };
        // ticking a mod whose archive is not downloaded yet opens its Nexus page in the default browser (once)
        modList.ItemCheck += (s, e) => {
            if (filling || e.Index >= Mods.All.Length) return;
            var m = Mods.All[e.Index];
            if (e.NewValue == CheckState.Checked) chosen.Add(m.Id); else chosen.Remove(m.Id);
            if (e.NewValue == CheckState.Checked && m.Archive == null && !opened.Contains(m.Id)) { opened.Add(m.Id); Mods.OpenPage(m); }
            BeginInvoke(new Action(() => { if (page == 3) next.Text = NextText(); }));
        };
        waitTimer.Interval = 2000; waitTimer.Tick += (s, e) => UpdateWait();
        pathBox.TextChanged += (s, e) => CheckPath();
        uninstall.CheckedChanged += (s, e) => { if (page == 2) next.Text = NextText(); };
        foreach (var g in Installer.FindGame()) pathBox.Items.Add(g);
        if (!string.IsNullOrEmpty(path)) pathBox.Text = path; else if (pathBox.Items.Count > 0) pathBox.SelectedIndex = 0;
        if (mode == "uninstall") uninstall.Checked = true;
        Show(mode != null ? 2 : 0);
    }

    string NextText()
    {
        if (page == 2) return uninstall.Checked ? T.S("Remove", "Удалить") : T.S("Next >", "Далее >");
        if (page == 3) return Missing().Count > 0 ? T.S("Next >", "Далее >") : T.S("Install", "Установить");
        if (page == 4) return T.S("Install", "Установить");
        if (page == 5) return T.S("Finish", "Готово");
        return T.S("Next >", "Далее >");
    }

    void Show(int p)
    {
        page = p;
        pathBox.Visible = browse.Visible = pathInfo.Visible = uninstall.Visible = p == 2;
        modList.Visible = modInfo.Visible = modBrowse.Visible = p == 3;
        waitInfo.Visible = openPages.Visible = p == 4;
        back.Enabled = p > 0 && p < 5;
        next.Enabled = true;
        cancel.Visible = p < 5;
        waitTimer.Enabled = p == 4;
        if (p == 0)
        {
            title.Text = T.S("Fall of Cybertron Co-op", "Кооператив для Fall of Cybertron");
            body.Height = 104;
            body.Text = T.S("This installs the unofficial co-op mod for Transformers: Fall of Cybertron (Steam version).\n\nPlay the story and Escalation with up to 5 friends. In the game press F10 for the co-op menu.",
                            "Будет установлен неофициальный кооп-мод для Transformers: Fall of Cybertron (версия из Steam).\n\nСюжет и Escalation вместе с друзьями (до 5 человек). В игре нажмите F10 — откроется меню кооператива.");
        }
        else if (p == 1)
        {
            title.Text = T.S("Before you install", "Перед установкой");
            body.Height = 104;
            body.Text = T.S("• Every player needs their own copy of the game and this mod.\n• Steam must be running: friends join you through Steam, no IP addresses or port forwarding.\n• Host: F10 → Host a game. Friend: F10 → Join a game → Join a friend through Steam.\n• The mod adds dinput8.dll and the foc_relay folder to the game's Binaries folder. Optional mods (next pages) back up every file they replace.",
                            "• Каждому игроку нужна своя копия игры и этот мод.\n• Steam должен быть запущен: друзья подключаются через Steam, без IP-адресов и проброса портов.\n• Хост: F10 → Host a game. Друг: F10 → Join a game → Join a friend through Steam.\n• Мод добавляет dinput8.dll и папку foc_relay в папку Binaries игры. Дополнительные моды (дальше) сохраняют копии всех файлов, которые заменяют.");
        }
        else if (p == 2)
        {
            title.Text = T.S("Game folder", "Папка с игрой");
            body.Height = 104;
            body.Text = pathBox.Items.Count > 0 ? T.S("The game was found automatically. Check the folder and press Next.", "Игра найдена автоматически. Проверьте папку и нажмите «Далее».")
                                                : T.S("The game was not found automatically. Choose the Fall of Cybertron folder.", "Игру не удалось найти автоматически. Укажите папку Fall of Cybertron.");
            CheckPath();
        }
        else if (p == 3)
        {
            title.Text = T.S("Extra mods (optional)", "Дополнительные моды (по желанию)");
            body.Height = 66;
            body.Text = T.S("Mods from Nexus Mods that work with co-op. Tick a mod you have not downloaded yet and its Nexus page opens in your browser: download it there (Files → Manual / Slow download). All players should pick the same mods.",
                            "Моды с Nexus Mods, проверенные с кооперативом. Отметьте мод, которого ещё нет, — его страница на Nexus откроется в браузере: скачайте его там (Files → Manual / Slow download). Всем игрокам лучше выбрать одинаковые моды.");
            FillMods();
        }
        else if (p == 4)
        {
            title.Text = T.S("Waiting for the downloads", "Ожидание загрузок");
            body.Height = 66;
            body.Text = T.S("Download the ticked mods on the Nexus pages that opened. The installer checks the Downloads folder every 2 seconds and continues by itself when everything is there. Install now = only what is already downloaded.",
                            "Скачайте отмеченные моды на открывшихся страницах Nexus. Установщик проверяет папку «Загрузки» каждые 2 секунды и продолжит сам, когда всё скачается. «Установить» сейчас — только то, что уже скачано.");
            UpdateWait();
        }
        next.Text = NextText();
    }

    void FillMods()
    {
        Mods.Find(modDir);
        string game = Installer.GameOf(pathBox.Text);
        var have = game != null ? Mods.Installed(game) : new List<string>();
        filling = true;
        modList.Items.Clear();
        foreach (var m in Mods.All)
        {
            string name = T.S(m.Name, m.NameRu);
            string state = m.Archive != null ? Path.GetFileName(m.Archive) : T.S("not downloaded — tick to open its page", "не скачан — отметьте, откроется страница");
            if (have.Contains(m.Id)) state = T.S("installed; ", "уже установлен; ") + state;
            int i = modList.Items.Add(name + "  —  " + state);
            bool tick = chosen.Contains(m.Id);   // owner 2026-10-24: every mod (and the Russian translation) unticked by default
            modList.SetItemChecked(i, tick);
            if (tick) chosen.Add(m.Id);
        }
        filling = false;
        modInfo.Text = T.S("Searched: ", "Поиск в: ") + string.Join("; ", Mods.SearchDirs(modDir).ToArray());
        if (page == 3) next.Text = NextText();
    }

    List<ModDef> Missing()
    {
        var r = new List<ModDef>();
        foreach (var m in Mods.All) if (chosen.Contains(m.Id) && m.Archive == null) r.Add(m);
        return r;
    }

    List<string> ReadyMods()
    {
        var ids = new List<string>();
        foreach (var m in Mods.All) if (chosen.Contains(m.Id) && m.Archive != null && Mods.Stable(m.Archive)) ids.Add(m.Id);
        return ids;
    }

    void UpdateWait()
    {
        if (page != 4) return;
        Mods.Find(modDir);
        var lines = new List<string>(); int waiting = 0;
        foreach (var m in Mods.All)
        {
            if (!chosen.Contains(m.Id)) continue;
            bool ready = m.Archive != null && Mods.Stable(m.Archive);
            if (!ready) waiting++;
            lines.Add((ready ? "✔ " : "… ") + T.S(m.Name, m.NameRu) + "  —  " +
                      (m.Archive == null ? T.S("waiting for the download", "ждём загрузку") : ready ? Path.GetFileName(m.Archive) : T.S("downloading...", "скачивается...")));
        }
        waitInfo.Text = string.Join("\n", lines.ToArray());
        if (waiting == 0 && lines.Count > 0) { waitTimer.Enabled = false; StartWork(false); }
    }

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
        if (busy) return;
        if (page < 2) { Show(page + 1); return; }
        if (page == 5) { Close(); return; }
        if (page == 2 && !uninstall.Checked) { Show(3); return; }
        if (page == 3 && Missing().Count > 0) { foreach (var m in Missing()) if (!opened.Contains(m.Id)) { opened.Add(m.Id); Mods.OpenPage(m); } Show(4); return; }
        StartWork(page == 2);
    }

    void StartWork(bool removing)
    {
        if (busy) return;
        Mods.Find(modDir);
        var ids = new List<string>();
        if (!removing) foreach (var m in Mods.All) if (chosen.Contains(m.Id) && m.Archive != null) ids.Add(m.Id);
        string folder = pathBox.Text;
        busy = true; waitTimer.Enabled = false; next.Enabled = back.Enabled = false; cancel.Visible = false;
        modList.Visible = modInfo.Visible = modBrowse.Visible = waitInfo.Visible = openPages.Visible = false;
        pathBox.Visible = browse.Visible = pathInfo.Visible = uninstall.Visible = false;
        title.Text = removing ? T.S("Removing...", "Удаление...") : T.S("Installing...", "Установка...");
        body.Text = "";
        var th = new System.Threading.Thread(() => {
            string msg; bool ok;
            try
            {
                ok = removing ? Installer.Uninstall(folder, out msg) : Installer.Install(folder, out msg);
                if (ok && ids.Count > 0)
                {
                    try { Mods.Install(Installer.GameOf(folder), ids, l => BeginInvoke(new Action(() => body.Text = l))); msg += T.S("\nMods: ", "\nМоды: ") + string.Join(", ", ids.ToArray()); }
                    catch (UnauthorizedAccessException) { ok = false; msg = "ACCESS"; }
                    catch (Exception e) { ok = false; msg = e.Message; }
                }
            }
            catch (Exception e) { ok = false; msg = e.Message; }
            BeginInvoke(new Action(() => Finish(ok, msg, removing, ids, folder)));
        });
        th.IsBackground = true; th.Start();
    }

    void Finish(bool ok, string msg, bool removing, List<string> ids, string folder)
    {
        busy = false;
        if (!ok && msg == "ACCESS")
        {   // e.g. Program Files: run again elevated
            try
            {
                string args = (removing ? "/uninstall" : "/install") + " \"" + folder + "\" /silent";
                if (ids.Count > 0) args += " /mods " + string.Join(",", ids.ToArray()) + (modDir != null ? " /modsdir \"" + modDir + "\"" : "");
                var psi = new ProcessStartInfo(Application.ExecutablePath, args) { Verb = "runas", UseShellExecute = true };
                body.Text = T.S("Waiting for administrator rights...", "Ожидание прав администратора...");
                var p = Process.Start(psi); p.WaitForExit();
                ok = p.ExitCode == 0;
                msg = ok ? (removing ? T.S("The mod was removed.", "Мод удалён.") : T.S("Installed into ", "Установлено в ") + Installer.BinariesOf(folder) + (ids.Count > 0 ? T.S("\nMods: ", "\nМоды: ") + string.Join(", ", ids.ToArray()) : ""))
                         : T.S("Installing with administrator rights failed.", "Установка с правами администратора не удалась.");
            }
            catch { msg = T.S("Administrator rights are needed for this folder.", "Для этой папки нужны права администратора."); }
        }
        if (!ok) { MessageBox.Show(this, msg, Text, MessageBoxButtons.OK, MessageBoxIcon.Warning); Show(removing ? 2 : 3); return; }
        Show(5);
        title.Text = T.S("Done", "Готово");
        body.Height = 200;
        body.Text = msg + (removing ? "" : T.S("\n\nStart the game from Steam and press F10.", "\n\nЗапустите игру из Steam и нажмите F10."));
    }

    protected override void OnFormClosing(FormClosingEventArgs e) { if (busy) e.Cancel = true; base.OnFormClosing(e); }
}
