// Optional mods. Owner 2026-10-25: they download from the owner's Google Drive folder (progress bar with speed); an
// archive already in Downloads / next to the installer / in the download cache is used as it is. Archives are extracted
// with Windows' tar.exe (zip, rar, 7z) and the files copied into the game. Every game file that gets replaced is
// first copied to <game>\foc_backup\ (same relative path); files that did not exist are listed in foc_backup\added.txt.
// Uninstall puts the backups back and deletes the added files.
// Exception (owner 2026-10-24): the Russian translation (Steam guide "РАБОЧИЙ МУЛЬТИПЛЕЕР + НОВАЯ РУСИФИКАЦИЯ TFOC + DLC"
// by Zerberus, based on the official Novy Disk localisation) is bundled when build_installer.bat finds its files
// (E:\FoC-coop\downloads\rus, not in git). It also sets foc_coop.ini language=rus (the DLL adds -languageforcooking=rus
// to the command line); uninstall removes the key again.
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Text.RegularExpressions;
using System.Runtime.InteropServices;
using Microsoft.Win32;
using System.Net;
using System.IO.Compression;
using System.Windows.Automation;

class ModDef
{
    public string Id, Name, NameRu, Pattern, Note, NoteRu, Url;   // Url: the mod's Nexus page (credits)
    public string DriveId, FileName; public long Size;          // the archive on the owner's Google Drive
    public bool Default;
    public Func<string, string> Target;   // file inside the archive (relative) -> path under the game folder, or null = skip
    public string Archive;                // found archive, or null
    public string Resource;               // bundled in the installer (zip resource name) instead of a downloaded archive
    public bool KeepExisting(string rel) { return rel.EndsWith(".ini", StringComparison.OrdinalIgnoreCase) && (Id == "configurator" || rel.EndsWith("ReShade.ini", StringComparison.OrdinalIgnoreCase)); }
}

static class Mods
{
    // install order matters: the intro skip replaces some of the remastered movies (logos), so it comes after them
    public static readonly ModDef[] All = new ModDef[] {
        new ModDef { Id = "rus", Name = "Russian language (fan translation by Zerberus, Steam guide)", NameRu = "Русский язык (русификатор Zerberus из руководства Steam)",
            Resource = "rus.zip", Url = "https://steamcommunity.com/sharedfiles/filedetails/?id=2983257437",
            Target = f => f.StartsWith(@"TransGame\", StringComparison.OrdinalIgnoreCase) ? f : null },
        new ModDef { Id = "audiologs", Name = "High Quality Audio Logs", NameRu = "Аудиологи в высоком качестве", Default = false,
            Url = "https://www.nexusmods.com/transformersfallofcybertron/mods/17?tab=files",
            Pattern = @"High.?Quality.?Audio.?Logs.*\.(rar|zip|7z)$",
            DriveId = "1Fsd4pEFvZ3XIaAEqsyO8TxlmUbciVHXk", FileName = "High Quality Audio Logs-17-1-1-1730562482.rar", Size = 13880144,
            Target = f => Path.GetFileName(f).Equals("TransGame.xxx", StringComparison.OrdinalIgnoreCase) ? @"TransGame\CookedPC\TransGame.xxx" : null },
        new ModDef { Id = "fmv", Name = "FMV Remaster (cutscene videos)", NameRu = "Ремастер видеороликов", Default = false,
            Url = "https://www.nexusmods.com/transformersfallofcybertron/mods/9?tab=files",
            Pattern = @"FMV.?Remaster.*\.(7z|rar|zip)$",
            DriveId = "1YUHgR-WKtsRm3QT2WYky7k0iA1xjKeJG", FileName = "Transformers Fall of Cybertron - FMV Remaster-9-1-0-1698267786.7z", Size = 3151405973,
            Target = f => f.EndsWith(".bik", StringComparison.OrdinalIgnoreCase) ? @"TransGame\Movies\" + Path.GetFileName(f) : null },
        new ModDef { Id = "textures", Name = "Authentic High-Quality Textures (PS4 textures; installs with TFC Installer, 5-10 min)", NameRu = "Текстуры высокого качества (с PS4; ставятся через TFC Installer, 5-10 мин)", Default = false,
            Url = "https://www.nexusmods.com/transformersfallofcybertron/mods/62?tab=files",
            Pattern = @"Authentic.?High.?Quality.?Textures.*\.(rar|zip|7z)$",
            DriveId = "1lPtwrIS5h_pC7T9Alt2gtixRWRJe1n9W", FileName = "Authentic High-Quality Textures-62-1-3-1760643876.rar", Size = 2292883921 + Textures.ExtraSize,
            Target = f => null },
        new ModDef { Id = "nointro", Name = "Skip the intro videos", NameRu = "Пропуск вступительных роликов", Default = false,
            Url = "https://www.nexusmods.com/transformersfallofcybertron/mods/12?tab=files",
            Pattern = @"No.?Intro.*\.(zip|rar|7z)$",
            DriveId = "1GtjKzZEWn6cSe-XK9SpICBr5I5wkvEBg", FileName = "TFOCNo-Intro-SplashFix-v.0.3-12-0-3-1776181851.zip", Size = 12437,
            Target = f => f.EndsWith(".bik", StringComparison.OrdinalIgnoreCase) ? @"TransGame\Movies\" + Path.GetFileName(f) : null },
        new ModDef { Id = "configurator", Name = "Advanced Configurator (F1 menu: FPS, FOV, graphics)", NameRu = "Advanced Configurator (меню F1: FPS, FOV, графика)", Default = false,
            Pattern = @"Advanced.?Configurator.*\.(zip|rar|7z)$",
            DriveId = "1F-BgQx4hGmQSRPSL8Ir56FWPu3IWxQNj", FileName = "TFOC-Advanced-Configurator-v.0.5 120 0.5 2026-09-13T12-18Z rlsya2gZp.zip", Size = 188931,
            Url = "https://www.nexusmods.com/transformersfallofcybertron/mods/120?tab=files",
            Note = "loaded by the co-op mod", NoteRu = "загружается кооп-модом",
            Target = f => {
                string n = Path.GetFileName(f);
                if (n.Equals("dinput8.dll", StringComparison.OrdinalIgnoreCase)) return @"Binaries\TFOCAdvancedConfigurator.dll";
                if (n.Equals("TFOCAdvancedConfigurator.ini", StringComparison.OrdinalIgnoreCase)) return @"Binaries\TFOCAdvancedConfigurator.ini";
                return null; } },
        new ModDef { Id = "reshade", Name = "ReShade 6.8 + colour preset (bundled; Home = ReShade menu)", NameRu = "ReShade 6.8 + цветовой пресет (встроен; Home — меню ReShade)", Default = false,
            Resource = "reshade.zip",
            Target = f => {
                string n = Path.GetFileName(f);
                if (n.StartsWith("LICENSE", StringComparison.OrdinalIgnoreCase) || n.Equals("README.txt", StringComparison.OrdinalIgnoreCase))
                    return @"Binaries\reshade-shaders\" + (n == "README.txt" ? "FoCCoop_ReShade_README.txt" : n);
                return @"Binaries\" + f; } },
    };

    // a download is complete when the file exists and its size did not change since the previous call (browsers write
    // to .crdownload/.part names first, which the patterns do not match, but some write in place)
    static readonly Dictionary<string, long> lastSize = new Dictionary<string, long>(StringComparer.OrdinalIgnoreCase);
    public static bool Stable(string f)
    {
        if (f == null) return false;
        if (f.StartsWith("(")) return true;   // bundled
        long n; try { n = new FileInfo(f).Length; } catch { return false; }
        long prev; bool had = lastSize.TryGetValue(f, out prev);
        lastSize[f] = n;
        if (n <= 0) return false;
        try { using (File.Open(f, FileMode.Open, FileAccess.Read, FileShare.Read)) { } } catch { return false; }   // still being written
        return had && prev == n;
    }

    public static void OpenPage(ModDef m)
    {
        if (string.IsNullOrEmpty(m.Url)) return;
        try { Process.Start(new ProcessStartInfo(m.Url) { UseShellExecute = true }); } catch { }
    }

    public static bool NoLocal;   // /nolocal (tests): only the download cache, so the Google Drive download runs
    public static List<string> SearchDirs(string extra)
    {
        var d = new List<string>();
        if (NoLocal) { d.Add(ModDownload.CacheDir()); return d; }
        if (!string.IsNullOrEmpty(extra)) d.Add(extra);
        try { d.Add(Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.UserProfile), "Downloads")); } catch { }
        try { d.Add(Path.GetDirectoryName(System.Reflection.Assembly.GetExecutingAssembly().Location)); } catch { }
        try { d.Add(ModDownload.CacheDir()); } catch { }
        return d;
    }

    // newest matching archive per mod
    public static void Find(string extraDir)
    {
        foreach (var m in All)
        {
            m.Archive = null; DateTime best = DateTime.MinValue;
            if (m.Resource != null) { if (System.Reflection.Assembly.GetExecutingAssembly().GetManifestResourceInfo(m.Resource) != null) m.Archive = "(" + T.S("bundled", "встроен") + ")"; continue; }
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
            if (m.Id == "textures") { Textures.Install(game, m.Archive, progress); Directory.CreateDirectory(backup); File.WriteAllText(Path.Combine(backup, "mod_textures.txt"), Path.GetFileName(m.Archive)); continue; }
            progress(T.S("Unpacking ", "Распаковка: ") + (m.Resource ?? Path.GetFileName(m.Archive)));
            string tmp = Path.Combine(Path.GetTempPath(), "foccoop_mod_" + m.Id + "_" + Guid.NewGuid().ToString("N").Substring(0, 8));
            try
            {
                if (m.Resource != null)
                {
                    string zip = tmp + ".zip";
                    using (var src = System.Reflection.Assembly.GetExecutingAssembly().GetManifestResourceStream(m.Resource)) using (var dst = File.Create(zip)) src.CopyTo(dst);
                    System.IO.Compression.ZipFile.ExtractToDirectory(zip, tmp);
                    File.Delete(zip);
                }
                else Extract(m.Archive, tmp);
                int n = 0;
                foreach (var f in Directory.GetFiles(tmp, "*", SearchOption.AllDirectories))
                {
                    string rel = m.Target(f.Substring(tmp.Length).TrimStart('\\'));
                    if (rel == null) continue;
                    string dst = Path.Combine(game, rel), bak = Path.Combine(backup, rel);
                    if (m.KeepExisting(rel) && File.Exists(dst)) continue;   // keep the player's settings
                    bool freshIni = m.Id == "configurator" && rel.EndsWith(".ini", StringComparison.OrdinalIgnoreCase);
                    Directory.CreateDirectory(Path.GetDirectoryName(dst));
                    if (File.Exists(dst) && !File.Exists(bak) && !Listed(added, rel)) { Directory.CreateDirectory(Path.GetDirectoryName(bak)); File.Copy(dst, bak); }
                    else if (!File.Exists(dst) && !File.Exists(bak) && !Listed(added, rel)) File.AppendAllText(added, rel + "\r\n");
                    progress(T.S("Copying ", "Копирование: ") + rel);
                    File.Copy(f, dst, true);
                    if (freshIni) File.WriteAllText(dst, ConfiguratorDefaults);
                    if (m.Id == "audiologs" && rel.EndsWith("TransGame.xxx", StringComparison.OrdinalIgnoreCase)) { progress(T.S("Audio logs: volume fix", "Аудиологи: исправление громкости")); AudioLogFix.Patch(dst); }
                    n++;
                }
                if (n == 0) throw new Exception(m.Name + ": " + T.S("no usable files in the archive", "в архиве нет нужных файлов"));
                if (m.Id == "rus") { progress(T.S("Language: Russian", "Язык: русский")); Language.Apply(game, backup); }
                File.WriteAllText(Path.Combine(backup, "mod_" + m.Id + ".txt"), Path.GetFileName(m.Archive));
            }
            finally { try { Directory.Delete(tmp, true); } catch { } }
        }
    }

    // owner 2026-10-25: the configurator's settings of the owner's own game, FPS 200 (written when the player has no ini)
    const string ConfiguratorDefaults = "[Settings]\r\nVehicleCompatibility=1\r\nFPS=200\r\nFOV=90\r\nSubtitleScale=125\r\nHighTexturePoolSize=1\r\n" +
        "AnisotropicFiltering16x=1\r\nImprovedShadowQuality=2\r\nMeshDetail=2\r\nParticleDetail=2\r\nLodDistancePercent=200\r\nDrawDistancePercent=200\r\n" +
        "HeightFog=0\r\nLensFlares=1\r\nGameplayHUD=1\r\nDialogueSubtitles=1\r\nHUDToggleKey=113\r\nRtxHdrCompatibility=0\r\nTexturePoolSizeMB=1024\r\n" +
        "TextureFix4K=1\r\nScriptedPlacementCompatibility=1\r\nMotionBlur=0\r\nDepthOfField=0\r\nBloom=1\r\nAmbientOcclusion=1\r\n";

    // download every chosen mod that has no archive yet; progress(text, fraction 0..1)
    public static void DownloadMissing(IList<string> ids, Action<string, double> progress)
    {
        long total = 0, before = 0;
        foreach (var m in All) if (ids.Contains(m.Id) && m.Archive == null && m.DriveId != null) total += m.Size;
        foreach (var m in All)
        {
            if (!ids.Contains(m.Id) || m.Archive != null || m.DriveId == null) continue;
            string name = T.S(m.Name, m.NameRu);
            long b0 = before;
            m.Archive = ModDownload.Get(m, (done, size, speed) => {
                double left = speed > 1 ? (total - b0 - done) / speed : 0;
                progress(string.Format(T.S("Downloading {0}\n{1} of {2}   {3}/s   about {4} left", "Скачивание: {0}\n{1} из {2}   {3}/с   осталось около {4}"),
                    name, ModDownload.Size(done), ModDownload.Size(size), ModDownload.Size((long)speed), ModDownload.Time(left)),
                    total > 0 ? (double)(b0 + done) / total : 0);
            });
            before += m.Size;
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
        Language.Restore(game, backup);
        if (File.Exists(Path.Combine(backup, "mod_textures.txt"))) Textures.Uninstall(game, s => { });
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

// The game's language: foc_coop.ini language=rus makes the DLL add -languageforcooking=rus to the game's command line.
// Tested 2026-10-24 (copy D): with the translation's files and this key the menus are Russian, without the key English;
// the registry Language value the translation's .reg sets (HKLM, needs admin) made no difference, so it is not touched.
static class Language
{
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode)] static extern bool WritePrivateProfileString(string section, string key, string value, string file);
    public static void Apply(string game, string backup)
    {
        WritePrivateProfileString("foc_coop", "language", "rus", Path.Combine(game, @"Binaries\foc_coop.ini"));
        File.WriteAllText(Path.Combine(backup, "language_set.txt"), "rus");
    }
    // owner 2026-10-25: Russian translation ticked -> the game and the F10 menu in Russian, not ticked -> English (also
    // when an earlier install had set Russian). Called after every install.
    public static void Choose(string game, bool russian)
    {
        string ini = Path.Combine(game, @"Binaries\foc_coop.ini");
        if (russian) { WritePrivateProfileString("foc_coop", "language", "rus", ini); return; }
        if (File.Exists(ini)) WritePrivateProfileString("foc_coop", "language", null, ini);
    }
    public static void Restore(string game, string backup)
    {
        if (!File.Exists(Path.Combine(backup, "language_set.txt"))) return;
        WritePrivateProfileString("foc_coop", "language", null, Path.Combine(game, @"Binaries\foc_coop.ini"));
    }
}

// Mods download from the owner's Google Drive folder (owner 2026-10-25), except the bundled ones (Russian, ReShade).
// drive.usercontent.google.com with confirm=t skips the "can't scan big files for viruses" page.
static class ModDownload
{
    public static string CacheDir() { string d = Path.Combine(Path.GetTempPath(), "foccoop_mods"); Directory.CreateDirectory(d); return d; }

    // progress(done, total, bytesPerSecond); the archive stays in %TEMP%\foccoop_mods (a second install reuses it)
    public static string Get(ModDef m, Action<long, long, double> progress)
    {
        return GetUrl("https://drive.usercontent.google.com/download?id=" + m.DriveId + "&export=download&confirm=t", m.FileName, m.Size, T.S(m.Name, m.NameRu), progress);
    }
    public static string GetUrl(string url, string fileName, long size, string name, Action<long, long, double> progress)
    {
        ServicePointManager.SecurityProtocol = (SecurityProtocolType)3072;   // TLS 1.2
        string dst = Path.Combine(CacheDir(), fileName), part = dst + ".part";
        if (File.Exists(dst) && new FileInfo(dst).Length == size) return dst;
        var req = (HttpWebRequest)WebRequest.Create(url);
        req.UserAgent = "Mozilla/5.0 (foc-coop installer)"; req.Timeout = 30000; req.ReadWriteTimeout = 60000;
        using (var resp = (HttpWebResponse)req.GetResponse())
        {
            if (resp.ContentType != null && resp.ContentType.StartsWith("text/html"))
                throw new Exception(name + ": " + T.S("Google Drive sent a page instead of the file (download limit?). Try again later.", "Google Диск прислал страницу вместо файла (лимит скачиваний?). Попробуйте позже."));
            long total = resp.ContentLength > 0 ? resp.ContentLength : size;
            using (var src = resp.GetResponseStream()) using (var outp = File.Create(part))
            {
                var buf = new byte[1 << 16]; long done = 0, lastDone = 0; double lastT = 0, speed = 0; var sw = Stopwatch.StartNew(); int n;
                while ((n = src.Read(buf, 0, buf.Length)) > 0)
                {
                    outp.Write(buf, 0, n); done += n;
                    double t = sw.Elapsed.TotalSeconds;
                    if (t - lastT >= 0.5) { double sp = (done - lastDone) / (t - lastT); speed = speed > 0 ? speed * 0.7 + sp * 0.3 : sp; lastDone = done; lastT = t; progress(done, total, speed); }
                }
                progress(done, total, speed);
                if (total > 0 && done != total) throw new Exception(name + ": " + T.S("download incomplete", "загрузка не завершилась"));
            }
        }
        if (File.Exists(dst)) File.Delete(dst);
        File.Move(part, dst);
        return dst;
    }

    public static string Size(long b)
    {
        if (b >= 1L << 30) return (b / (double)(1L << 30)).ToString("0.00") + T.S(" GB", " ГБ");
        if (b >= 1L << 20) return (b / (double)(1L << 20)).ToString("0.0") + T.S(" MB", " МБ");
        return Math.Max(1, b / 1024) + T.S(" KB", " КБ");
    }
    public static string Time(double sec)
    {
        if (sec < 60) return Math.Max(1, (int)sec) + T.S(" s", " с");
        if (sec < 3600) return (int)(sec / 60) + T.S(" min", " мин");
        return (int)(sec / 3600) + T.S(" h ", " ч ") + (int)(sec % 3600 / 60) + T.S(" min", " мин");
    }
}

// High Quality Audio Logs (2026-10-25): the mod's TransGame.xxx only changes the CategoryName of the 76 audio log sound
// events from DX_AUDIO_LOG (the lo-fi audio log effect) to None - and with no category the volume sliders do not reach
// them (always full volume). We set DX_DRY_RADIO instead: the clean dialogue category (no effect, dialogue volume).
// The package is UE3, chunk-compressed with zlib (128 KB blocks): unpack, patch the 76 values, pack again.
static class AudioLogFix
{
    static readonly byte[] Tag = { 0xc1, 0x83, 0x2a, 0x9e };
    public const string Category = "DX_DRY_RADIO";
    public static void Patch(string path)
    {
        byte[] o = File.ReadAllBytes(path);
        int hdrEnd = Find(o, Tag, 4);
        int first = -1;
        for (int p = 0x29; p < hdrEnd - 16; p++)
            if (U(o, p + 8) == hdrEnd && U(o, p + 4) > 0 && U(o, p) < hdrEnd) { first = p; break; }
        if (first < 0) throw new Exception("audio logs: chunk table not found");
        int cnt = (int)U(o, first - 4);
        var uoff = new long[cnt]; var usize = new int[cnt]; var coff = new int[cnt]; var bsz = new int[cnt];
        var full = new MemoryStream();
        for (int k = 0; k < cnt; k++)
        {
            uoff[k] = U(o, first + 16 * k); usize[k] = (int)U(o, first + 16 * k + 4); coff[k] = (int)U(o, first + 16 * k + 8);
            int co = coff[k]; bsz[k] = (int)U(o, co + 4); int us = (int)U(o, co + 12); int nb = (us + bsz[k] - 1) / bsz[k];
            int p = co + 16 + 8 * nb;
            for (int b = 0; b < nb; b++)
            {
                int c = (int)U(o, co + 16 + 8 * b);
                using (var ds = new DeflateStream(new MemoryStream(o, p + 2, c - 2), CompressionMode.Decompress)) ds.CopyTo(full);
                p += c;
            }
        }
        byte[] d = full.ToArray(); long u0 = uoff[0];
        // names
        int nc = (int)U(o, 0x2d); long no = U(o, 0x31);
        var names = new List<string>(); int q = (int)(no - u0);
        for (int k = 0; k < nc; k++) { int len = (int)U(d, q); names.Add(System.Text.Encoding.ASCII.GetString(d, q + 4, len).TrimEnd((char)0)); q += 4 + len + 8; }
        int iCat = names.IndexOf("CategoryName"), iNp = names.IndexOf("NameProperty"), iNone = names.IndexOf("None"), iLog = names.IndexOf("DX_AUDIO_LOG"), iNew = names.IndexOf(Category);
        if (iCat < 0 || iNp < 0 || iNew < 0) throw new Exception("audio logs: names not found");
        var tag = new byte[24]; W(tag, 0, (uint)iCat); W(tag, 8, (uint)iNp); W(tag, 16, 8);
        int patched = 0;
        for (int i = Find(d, tag, 0); i >= 0; i = Find(d, tag, i + 1))
        {
            uint v = U(d, i + 24);
            if (v == iNone || v == iLog) { W(d, i + 24, (uint)iNew); patched++; }
        }
        if (patched == 0) return;
        // pack again, same chunks and block size
        var outp = new MemoryStream(); outp.Write(o, 0, hdrEnd);
        var newC = new int[cnt]; var newS = new int[cnt];
        for (int k = 0; k < cnt; k++)
        {
            var blocks = new List<byte[]>();
            for (int b = 0; b < usize[k]; b += bsz[k]) blocks.Add(Zlib(d, (int)(uoff[k] - u0) + b, Math.Min(bsz[k], usize[k] - b)));
            newC[k] = (int)outp.Length;
            long csum = 0; foreach (var bl in blocks) csum += bl.Length;
            var h = new byte[16 + 8 * blocks.Count]; Array.Copy(Tag, h, 4); W(h, 4, (uint)bsz[k]); W(h, 8, (uint)csum); W(h, 12, (uint)usize[k]);
            for (int b = 0; b < blocks.Count; b++) { W(h, 16 + 8 * b, (uint)blocks[b].Length); W(h, 20 + 8 * b, (uint)Math.Min(bsz[k], usize[k] - b * bsz[k])); }
            outp.Write(h, 0, h.Length); foreach (var bl in blocks) outp.Write(bl, 0, bl.Length);
            newS[k] = (int)outp.Length - newC[k];
        }
        byte[] r = outp.ToArray();
        for (int k = 0; k < cnt; k++) { W(r, first + 16 * k + 8, (uint)newC[k]); W(r, first + 16 * k + 12, (uint)newS[k]); }
        File.WriteAllBytes(path, r);
    }
    static byte[] Zlib(byte[] d, int off, int len)
    {
        var ms = new MemoryStream(); ms.WriteByte(0x78); ms.WriteByte(0xDA);
        using (var ds = new DeflateStream(ms, CompressionLevel.Optimal, true)) ds.Write(d, off, len);
        uint a = 1, b = 0; for (int i = off; i < off + len; i++) { a = (a + d[i]) % 65521; b = (b + a) % 65521; }
        uint ad = (b << 16) | a; ms.WriteByte((byte)(ad >> 24)); ms.WriteByte((byte)(ad >> 16)); ms.WriteByte((byte)(ad >> 8)); ms.WriteByte((byte)ad);
        return ms.ToArray();
    }
    static uint U(byte[] b, int p) { return BitConverter.ToUInt32(b, p); }
    static void W(byte[] b, int p, uint v) { b[p] = (byte)v; b[p + 1] = (byte)(v >> 8); b[p + 2] = (byte)(v >> 16); b[p + 3] = (byte)(v >> 24); }
    static int Find(byte[] h, byte[] n, int from)
    {
        for (int i = from; i <= h.Length - n.Length; i++)
        {
            if (h[i] != n[0]) continue;
            int j = 1; while (j < n.Length && h[i + j] == n[j]) j++;
            if (j == n.Length) return i;
        }
        return -1;
    }
}

// Authentic High-Quality Textures (owner 2026-10-25: "install it automatically too"). The pack is applied by TFC Installer
// (UPK Explorer's installer, a .NET 6 WPF program without a command line): the installer downloads it (owner's Google
// Drive) and a private .NET 6 runtime (Microsoft's zips, no system install), starts it and clicks it through UI
// Automation: Game folder -> the folder dialog gets the path (WM_SETTEXT + IDOK, control ids, any Windows language),
// Mod folder -> the extracted pack, Update -> waits for "Updated N file(s)". Textures go into the packages themselves
// (740 files, backups in <game>\TFCInstallerBackups); the extracted pack is deleted afterwards. Uninstall clicks
// "Uninstall all" in the same program. Tested 2026-10-25 on a test copy: 5.5 min, co-op with a player without it works.
static class Textures
{
    const string TfcId = "1aD4Dj-40SkSIWfRpYlZ6myV8okjJxqu7", TfcFile = "TFC Installer 588 2.5.6.0.zip";
    const long TfcSize = 9841674;
    const string CoreUrl = "https://builds.dotnet.microsoft.com/dotnet/Runtime/6.0.36/dotnet-runtime-6.0.36-win-x64.zip";
    const string DeskUrl = "https://builds.dotnet.microsoft.com/dotnet/WindowsDesktop/6.0.36/windowsdesktop-runtime-6.0.36-win-x64.zip";
    const long CoreSize = 33057872, DeskSize = 36315065;
    public const long ExtraSize = TfcSize + CoreSize + DeskSize;

    static string ToolDir(string game) { return Path.Combine(game, "foc_tfc"); }   // not under foc_backup: its subfolders are restored into the game

    // TFC Installer + .NET 6 runtime under <game>\foc_tfc (kept for the uninstall)
    static string PrepareTool(string game, Action<string> progress)
    {
        if (!Environment.Is64BitOperatingSystem) throw new Exception(T.S("The texture pack needs 64-bit Windows.", "Для текстур нужна 64-битная Windows."));
        string dir = ToolDir(game), rt = Path.Combine(dir, "dotnet"), app = Path.Combine(dir, "TFCInstaller");
        Action<long, long, double> show = (d, t, sp) => progress(T.S("Downloading the texture installer ", "Скачивание установщика текстур ") + ModDownload.Size(d) + " / " + ModDownload.Size(t));
        if (!File.Exists(Path.Combine(app, "TFCInstaller.dll")))
        {
            string z = ModDownload.GetUrl("https://drive.usercontent.google.com/download?id=" + TfcId + "&export=download&confirm=t", TfcFile, TfcSize, "TFC Installer", show);
            Directory.CreateDirectory(dir); ZipFile.ExtractToDirectory(z, dir);
        }
        if (!File.Exists(Path.Combine(rt, "dotnet.exe")) || !Directory.Exists(Path.Combine(rt, @"shared\Microsoft.WindowsDesktop.App")))
        {
            Directory.CreateDirectory(rt);
            foreach (var u in new[] { new { Url = CoreUrl, Size = CoreSize }, new { Url = DeskUrl, Size = DeskSize } })
            {
                string z = ModDownload.GetUrl(u.Url, Path.GetFileName(u.Url), u.Size, ".NET 6", show);
                using (var a = ZipFile.OpenRead(z))
                    foreach (var e in a.Entries)
                    {
                        string dst = Path.Combine(rt, e.FullName.Replace('/', '\\'));
                        if (e.FullName.EndsWith("/")) { Directory.CreateDirectory(dst); continue; }
                        Directory.CreateDirectory(Path.GetDirectoryName(dst)); e.ExtractToFile(dst, true);
                    }
            }
        }
        return dir;
    }

    static Process Start(string dir)
    {
        string rt = Path.Combine(dir, "dotnet"), app = Path.Combine(dir, "TFCInstaller");
        var psi = new ProcessStartInfo(Path.Combine(rt, "dotnet.exe"), "\"" + Path.Combine(app, "TFCInstaller.dll") + "\"") { UseShellExecute = false, WorkingDirectory = app };
        psi.EnvironmentVariables["DOTNET_ROOT"] = rt; psi.EnvironmentVariables["DOTNET_MULTILEVEL_LOOKUP"] = "0";
        return Process.Start(psi);
    }

    public static void Install(string game, string archive, Action<string> progress)
    {
        string dir = PrepareTool(game, progress);
        string pack = Path.Combine(game, "foc_textures_tmp");
        progress(T.S("Textures: unpacking (2.3 GB)...", "Текстуры: распаковка (2,3 ГБ)..."));
        if (Directory.Exists(pack)) Directory.Delete(pack, true);
        Directory.CreateDirectory(pack);
        var psi = new ProcessStartInfo(Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.System), "tar.exe"), "-xf \"" + archive + "\" -C \"" + pack + "\"") { UseShellExecute = false, CreateNoWindow = true };
        using (var t = Process.Start(psi)) { t.WaitForExit(); if (t.ExitCode != 0) throw new Exception("tar: " + Path.GetFileName(archive)); }
        string modDir = pack;   // the folder with GameProfile.xml
        foreach (var f in Directory.GetFiles(pack, "GameProfile.xml", SearchOption.AllDirectories)) { modDir = Path.GetDirectoryName(f); break; }
        progress(T.S("Textures: TFC Installer is updating the game files (5-10 minutes, do not close its window)...", "Текстуры: TFC Installer обновляет файлы игры (5-10 минут, не закрывайте его окно)..."));
        using (var p = Start(dir))
        {
            try
            {
                var w = Ui.MainWindow(p, 60);
                Ui.ClickButton(w, "Game folder"); Ui.FillFolderDialog(p, game);
                Ui.WaitText(w, game, 20);
                Ui.ClickButton(w, "Mod folder"); Ui.FillFolderDialog(p, modDir);
                Ui.WaitButton(w, "Update Transformers Fall of Cybertron", 30);
                Ui.ClickButton(w, "Update Transformers Fall of Cybertron");
                string r = Ui.WaitResult(w, 3600, s2 => progress(T.S("Textures: ", "Текстуры: ") + s2));
                if (r == null || !r.StartsWith("Updated")) throw new Exception(T.S("Texture pack: ", "Текстуры: ") + (r ?? T.S("TFC Installer did not finish", "TFC Installer не закончил работу")));
                progress(T.S("Textures: ", "Текстуры: ") + r);
            }
            finally { try { if (!p.HasExited) p.Kill(); } catch { } }
        }
        try { Directory.Delete(pack, true); } catch { }
    }

    public static void Uninstall(string game, Action<string> progress)
    {
        string dir = ToolDir(game);
        if (!File.Exists(Path.Combine(dir, @"TFCInstaller\TFCInstaller.dll")) || !Directory.Exists(Path.Combine(game, "TFCInstallerBackups"))) return;
        using (var p = Start(dir))
        {
            try
            {
                var w = Ui.MainWindow(p, 60);
                Ui.ClickButton(w, "Game folder"); Ui.FillFolderDialog(p, game);
                Ui.WaitButton(w, "Uninstall all", 30);
                Ui.ClickButton(w, "Uninstall all");
                Ui.ConfirmYes(p, 30);
                Ui.WaitUninstalled(w, 3600, s2 => progress(s2));
            }
            finally { try { if (!p.HasExited) p.Kill(); p.WaitForExit(5000); } catch { } }
        }
        try { Directory.Delete(dir, true); } catch { }
    }
}

// UI Automation + Win32 helpers for driving TFC Installer
static class Ui
{
    [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc f, IntPtr l);
    [DllImport("user32.dll")] static extern bool EnumChildWindows(IntPtr p, EnumProc f, IntPtr l);
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern int GetClassName(IntPtr h, System.Text.StringBuilder s, int n);
    [DllImport("user32.dll")] static extern int GetDlgCtrlID(IntPtr h);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern IntPtr SendMessage(IntPtr h, uint m, IntPtr w, string l);
    [DllImport("user32.dll")] static extern IntPtr SendMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
    [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr h);
    delegate bool EnumProc(IntPtr h, IntPtr l);

    public static AutomationElement MainWindow(Process p, int sec)
    {
        for (int i = 0; i < sec * 2; i++)
        {
            if (p.HasExited) throw new Exception("TFC Installer: " + T.S("closed at start", "закрылся при запуске"));
            var w = AutomationElement.RootElement.FindFirst(TreeScope.Children, new PropertyCondition(AutomationElement.ProcessIdProperty, p.Id));
            if (w != null && w.FindFirst(TreeScope.Descendants, new PropertyCondition(AutomationElement.NameProperty, "Game folder")) != null) return w;
            System.Threading.Thread.Sleep(500);
        }
        throw new Exception("TFC Installer: " + T.S("window not found", "окно не найдено"));
    }
    static AutomationElement ButtonOf(AutomationElement w, string text)
    {
        var t = w.FindFirst(TreeScope.Descendants, new PropertyCondition(AutomationElement.NameProperty, text));
        for (var e = t; e != null; e = TreeWalker.ControlViewWalker.GetParent(e))
            if (e.Current.ControlType == ControlType.Button) return e;
        return null;
    }
    public static void WaitButton(AutomationElement w, string text, int sec)
    {
        for (int i = 0; i < sec * 2; i++) { var b = ButtonOf(w, text); if (b != null && b.Current.IsEnabled) return; System.Threading.Thread.Sleep(500); }
        throw new Exception("TFC Installer: " + T.S("button not ready: ", "кнопка не готова: ") + text);
    }
    public static void ClickButton(AutomationElement w, string text)
    {
        WaitButton(w, text, 20);
        ((InvokePattern)ButtonOf(w, text).GetCurrentPattern(InvokePattern.Pattern)).Invoke();
    }
    public static void WaitText(AutomationElement w, string text, int sec)
    {
        for (int i = 0; i < sec * 2; i++) { if (w.FindFirst(TreeScope.Descendants, new PropertyCondition(AutomationElement.NameProperty, text)) != null) return; System.Threading.Thread.Sleep(500); }
    }
    static IntPtr Dialog(Process p)
    {
        IntPtr r = IntPtr.Zero; uint pid = (uint)p.Id;
        EnumWindows((h, l) => { uint q; GetWindowThreadProcessId(h, out q); var sb = new System.Text.StringBuilder(64); GetClassName(h, sb, 64); if (q == pid && sb.ToString() == "#32770" && IsWindowVisible(h)) { r = h; return false; } return true; }, IntPtr.Zero);
        return r;
    }
    static IntPtr Ctrl(IntPtr dlg, int id, string cls)
    {
        IntPtr r = IntPtr.Zero;
        EnumChildWindows(dlg, (h, l) => { var sb = new System.Text.StringBuilder(64); GetClassName(h, sb, 64); if (GetDlgCtrlID(h) == id && (cls == null || sb.ToString() == cls)) { r = h; return false; } return true; }, IntPtr.Zero);
        return r;
    }
    // the Windows folder picker: type the path and OK; when it only navigated into the folder, OK again with an empty name
    public static void FillFolderDialog(Process p, string folder)
    {
        IntPtr dlg = IntPtr.Zero;
        for (int i = 0; i < 40 && dlg == IntPtr.Zero; i++) { System.Threading.Thread.Sleep(250); dlg = Dialog(p); }
        if (dlg == IntPtr.Zero) throw new Exception("TFC Installer: " + T.S("folder dialog not found", "окно выбора папки не найдено"));
        for (int round = 0; round < 3; round++)
        {
            IntPtr ed = Ctrl(dlg, 1152, "Edit"); if (ed == IntPtr.Zero) ed = Ctrl(dlg, 1152, null);
            SendMessage(ed, 0x000C, IntPtr.Zero, round == 0 ? folder : "");   // WM_SETTEXT
            SendMessage(dlg, 0x0111, (IntPtr)1, IntPtr.Zero);                   // WM_COMMAND IDOK
            for (int i = 0; i < 12; i++) { System.Threading.Thread.Sleep(250); if (Dialog(p) == IntPtr.Zero) return; }
        }
        throw new Exception("TFC Installer: " + T.S("could not choose the folder ", "не удалось выбрать папку ") + folder);
    }
    public static void ConfirmYes(Process p, int sec)
    {
        for (int i = 0; i < sec * 2; i++)
        {
            System.Threading.Thread.Sleep(500);
            var all = AutomationElement.RootElement.FindAll(TreeScope.Children, new PropertyCondition(AutomationElement.ProcessIdProperty, p.Id));
            foreach (AutomationElement w in all)
                foreach (string n in new[] { "YES", "Yes", "OK", "Да" })
                {
                    var b = w.FindFirst(TreeScope.Descendants, new AndCondition(new PropertyCondition(AutomationElement.NameProperty, n), new PropertyCondition(AutomationElement.ControlTypeProperty, ControlType.Button)));
                    if (b == null) { var t = ButtonOf(w, n); b = t; }
                    if (b != null) { ((InvokePattern)b.GetCurrentPattern(InvokePattern.Pattern)).Invoke(); return; }
                }
        }
    }
    // uninstall shows no result line: it is over when the program is idle again (Game folder clickable) and there is
    // nothing left to uninstall (Uninstall all greyed out)
    public static void WaitUninstalled(AutomationElement w, int sec, Action<string> tick)
    {
        var start = DateTime.Now;
        while ((DateTime.Now - start).TotalSeconds < sec)
        {
            System.Threading.Thread.Sleep(2000);
            try
            {
                var g = ButtonOf(w, "Game folder"); var u = ButtonOf(w, "Uninstall all");
                if (g != null && g.Current.IsEnabled && u != null && !u.Current.IsEnabled) return;
                tick(string.Format(T.S("Textures: restoring the original files... {0} min", "Текстуры: возврат оригинальных файлов... {0} мин"), (int)(DateTime.Now - start).TotalMinutes));
            }
            catch (ElementNotAvailableException) { return; }
        }
    }
    // the result line under the button: "Updated N file(s)" / "... could not be updated" / an error text; null on timeout
    public static string WaitResult(AutomationElement w, int sec, Action<string> tick)
    {
        var start = DateTime.Now;
        while ((DateTime.Now - start).TotalSeconds < sec)
        {
            System.Threading.Thread.Sleep(2000);
            try
            {
                foreach (AutomationElement t in w.FindAll(TreeScope.Descendants, new PropertyCondition(AutomationElement.ControlTypeProperty, ControlType.Text)))
                {
                    string n = t.Current.Name ?? "";
                    if (Regex.IsMatch(n, @"^Updated \d+ file") || n.Contains("could not be updated") || n.StartsWith("Uninstalled") || n.StartsWith("Error")) return n;
                }
                tick(string.Format(T.S("working... {0} min", "идёт работа... {0} мин"), (int)(DateTime.Now - start).TotalMinutes));
            }
            catch (ElementNotAvailableException) { return null; }
        }
        return null;
    }
}
