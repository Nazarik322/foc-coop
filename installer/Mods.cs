// Optional Nexus mods, installed from the archives the player downloaded (they are not bundled: Nexus files need the
// authors' permission to redistribute). The installer looks for the archives in Downloads and next to itself, extracts
// them with Windows' tar.exe (zip, rar, 7z) and copies the files into the game. Every game file that gets replaced is
// first copied to <game>\foc_backup\ (same relative path); files that did not exist are listed in foc_backup\added.txt.
// Uninstall puts the backups back and deletes the added files.
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Text.RegularExpressions;

class ModDef
{
    public string Id, Name, NameRu, Pattern, Note, NoteRu;
    public bool Default;
    public Func<string, string> Target;   // file inside the archive (relative) -> path under the game folder, or null = skip
    public string Archive;                // found archive, or null
}

static class Mods
{
    // install order matters: the intro skip replaces some of the remastered movies (logos), so it comes after them
    public static readonly ModDef[] All = new ModDef[] {
        new ModDef { Id = "audiologs", Name = "High Quality Audio Logs", NameRu = "Аудиологи в высоком качестве", Default = true,
            Pattern = @"High.?Quality.?Audio.?Logs.*\.(rar|zip|7z)$",
            Target = f => Path.GetFileName(f).Equals("TransGame.xxx", StringComparison.OrdinalIgnoreCase) ? @"TransGame\CookedPC\TransGame.xxx" : null },
        new ModDef { Id = "fmv", Name = "FMV Remaster (cutscene videos)", NameRu = "Ремастер видеороликов", Default = true,
            Pattern = @"FMV.?Remaster.*\.(7z|rar|zip)$",
            Target = f => f.EndsWith(".bik", StringComparison.OrdinalIgnoreCase) ? @"TransGame\Movies\" + Path.GetFileName(f) : null },
        new ModDef { Id = "nointro", Name = "Skip the intro videos", NameRu = "Пропуск вступительных роликов", Default = true,
            Pattern = @"No.?Intro.*\.(zip|rar|7z)$",
            Target = f => f.EndsWith(".bik", StringComparison.OrdinalIgnoreCase) ? @"TransGame\Movies\" + Path.GetFileName(f) : null },
        new ModDef { Id = "configurator", Name = "Advanced Configurator (F1 menu: FPS, FOV, graphics)", NameRu = "Advanced Configurator (меню F1: FPS, FOV, графика)", Default = false,
            Pattern = @"Advanced.?Configurator.*\.(zip|rar|7z)$",
            Note = "loaded by the co-op mod", NoteRu = "загружается кооп-модом",
            Target = f => {
                string n = Path.GetFileName(f);
                if (n.Equals("dinput8.dll", StringComparison.OrdinalIgnoreCase)) return @"Binaries\TFOCAdvancedConfigurator.dll";
                if (n.Equals("TFOCAdvancedConfigurator.ini", StringComparison.OrdinalIgnoreCase)) return @"Binaries\TFOCAdvancedConfigurator.ini";
                return null; } },
    };

    public static List<string> SearchDirs(string extra)
    {
        var d = new List<string>();
        if (!string.IsNullOrEmpty(extra)) d.Add(extra);
        try { d.Add(Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.UserProfile), "Downloads")); } catch { }
        try { d.Add(Path.GetDirectoryName(System.Reflection.Assembly.GetExecutingAssembly().Location)); } catch { }
        return d;
    }

    // newest matching archive per mod
    public static void Find(string extraDir)
    {
        foreach (var m in All)
        {
            m.Archive = null; DateTime best = DateTime.MinValue;
            foreach (var dir in SearchDirs(extraDir))
            {
                if (!Directory.Exists(dir)) continue;
                string[] files; try { files = Directory.GetFiles(dir); } catch { continue; }
                foreach (var f in files)
                {
                    if (!Regex.IsMatch(Path.GetFileName(f), m.Pattern, RegexOptions.IgnoreCase)) continue;
                    var t = File.GetLastWriteTime(f);
                    if (t > best) { best = t; m.Archive = f; }
                }
            }
        }
    }

    static string Tar()
    {
        string t = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.System), "tar.exe");
        return File.Exists(t) ? t : "tar.exe";
    }

    static void Extract(string archive, string dir)
    {
        Directory.CreateDirectory(dir);
        var psi = new ProcessStartInfo(Tar(), "-xf \"" + archive + "\" -C \"" + dir + "\"") { UseShellExecute = false, CreateNoWindow = true, RedirectStandardError = true };
        using (var p = Process.Start(psi))
        {
            string err = p.StandardError.ReadToEnd();
            p.WaitForExit();
            if (p.ExitCode != 0) throw new Exception("tar: " + Path.GetFileName(archive) + ": " + err.Trim());
        }
    }

    // game = the game folder (parent of Binaries). progress gets one line per step.
    public static void Install(string game, IList<string> ids, Action<string> progress)
    {
        string backup = Path.Combine(game, "foc_backup"), added = Path.Combine(backup, "added.txt");
        Directory.CreateDirectory(backup);
        foreach (var m in All)
        {
            if (!ids.Contains(m.Id)) continue;
            if (m.Archive == null) throw new Exception(m.Name + ": " + T.S("archive not found", "архив не найден"));
            progress(T.S("Unpacking ", "Распаковка: ") + Path.GetFileName(m.Archive));
            string tmp = Path.Combine(Path.GetTempPath(), "foccoop_mod_" + m.Id + "_" + Guid.NewGuid().ToString("N").Substring(0, 8));
            try
            {
                Extract(m.Archive, tmp);
                int n = 0;
                foreach (var f in Directory.GetFiles(tmp, "*", SearchOption.AllDirectories))
                {
                    string rel = m.Target(f.Substring(tmp.Length).TrimStart('\\'));
                    if (rel == null) continue;
                    string dst = Path.Combine(game, rel), bak = Path.Combine(backup, rel);
                    if (m.Id == "configurator" && rel.EndsWith(".ini", StringComparison.OrdinalIgnoreCase) && File.Exists(dst)) continue;   // keep the player's settings
                    Directory.CreateDirectory(Path.GetDirectoryName(dst));
                    if (File.Exists(dst) && !File.Exists(bak) && !Listed(added, rel)) { Directory.CreateDirectory(Path.GetDirectoryName(bak)); File.Copy(dst, bak); }
                    else if (!File.Exists(dst) && !File.Exists(bak) && !Listed(added, rel)) File.AppendAllText(added, rel + "\r\n");
                    progress(T.S("Copying ", "Копирование: ") + rel);
                    File.Copy(f, dst, true);
                    n++;
                }
                if (n == 0) throw new Exception(m.Name + ": " + T.S("no usable files in the archive", "в архиве нет нужных файлов"));
                File.WriteAllText(Path.Combine(backup, "mod_" + m.Id + ".txt"), Path.GetFileName(m.Archive));
            }
            finally { try { Directory.Delete(tmp, true); } catch { } }
        }
    }

    static bool Listed(string added, string rel)
    {
        if (!File.Exists(added)) return false;
        foreach (var l in File.ReadAllLines(added)) if (string.Equals(l.Trim(), rel, StringComparison.OrdinalIgnoreCase)) return true;
        return false;
    }

    public static List<string> Installed(string game)
    {
        var r = new List<string>();
        string backup = Path.Combine(game, "foc_backup");
        foreach (var m in All) if (File.Exists(Path.Combine(backup, "mod_" + m.Id + ".txt"))) r.Add(m.Id);
        return r;
    }

    // restore every backed-up file, delete the added ones
    public static void Uninstall(string game)
    {
        string backup = Path.Combine(game, "foc_backup");
        if (!Directory.Exists(backup)) return;
        string added = Path.Combine(backup, "added.txt");
        if (File.Exists(added)) foreach (var l in File.ReadAllLines(added)) { string p = Path.Combine(game, l.Trim()); if (l.Trim().Length > 0 && File.Exists(p)) File.Delete(p); }
        foreach (var f in Directory.GetFiles(backup, "*", SearchOption.AllDirectories))
        {
            string rel = f.Substring(backup.Length).TrimStart('\\');
            if (!rel.Contains("\\")) continue;   // added.txt, mod_*.txt
            File.Copy(f, Path.Combine(game, rel), true);
        }
        Directory.Delete(backup, true);
    }
}
