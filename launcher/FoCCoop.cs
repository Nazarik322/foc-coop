// FoC Co-op launcher: installs the proxy DLL into a game copy and starts host / guest sessions.
// Compiled with the .NET Framework csc.exe that ships with Windows (C# 5 syntax only).
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Net;
using System.Net.Sockets;
using System.Security.Cryptography;
using System.Text;
using System.Windows.Forms;

static class Program
{
    [STAThread]
    static void Main()
    {
        Application.EnableVisualStyles();
        Application.SetCompatibleTextRenderingDefault(false);
        Application.Run(new MainForm());
    }
}

class MainForm : Form
{
    const string ExpectedSha256 = "c1dab177f81503ecb831d63a3d799f928dd9823242ca3689482c684396acfedc";
    static readonly string[][] Chapters = new string[][] {
        new string[] { "1  The Exodus", "L01_TheExodus_BASE_m" },
        new string[] { "2a Defend the Ark", "L02a_DefendTheArk_BASE_m" },
        new string[] { "2b Destroy All Mortars", "L02b_DestroyAllMortars_BASE_m" },
        new string[] { "3a Eye of the Storm", "L03a_EyeOfTheStorm_BASE_m" },
        new string[] { "3b Cut and Run", "L03b_CutAndRun_BASE_m" },
        new string[] { "4a Bridge Assault", "L04a_BridgeAssault_BASE_m" },
        new string[] { "4b Stop the Transport", "L04b_StopTheTransport_BASE_m" },
        new string[] { "4c Flying Fortress", "L04c_FlyingFortress_BASE_m" },
        new string[] { "5  Soundwave", "L05_Soundwave_BASE_m" },
        new string[] { "6  Megatron", "L06_Megatron_BASE_m" },
        new string[] { "8a Starscream", "L08a_Starscream_BASE_m" },
        new string[] { "8b Grimlock Smash", "L08b_GrimlockSmash_BASE_m" },
        new string[] { "9  Final Fight", "L09_FinalFight_BASE_m" },
    };
    static readonly string[][] Escalation = new string[][] {
        new string[] { "Ark", "ESC_Ark_BASE_m" },
        new string[] { "Gladiator", "ESC_Gladiator_BASE_m" },
        new string[] { "Iacon", "ESC_Iacon_BASE_m" },
        new string[] { "Temple", "ESC_Temple_BASE_m" },
    };

    TextBox txtGame, txtGame2, txtIp, txtExtra;
    ComboBox cmbChapter, cmbEsc;
    NumericUpDown numPlayers, numPort, numW, numH;
    CheckBox chkWindowed;
    Label lblStatus;
    string settingsPath;

    public MainForm()
    {
        Text = "Fall of Cybertron co-op";
        Name = "MainForm";
        ClientSize = new Size(640, 560);
        FormBorderStyle = FormBorderStyle.FixedDialog;
        MaximizeBox = false;
        StartPosition = FormStartPosition.CenterScreen;
        settingsPath = Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "launcher.ini");

        int y = 12;
        AddLabel("Game folder (the one with Binaries\\TFOC.exe):", 12, y); y += 20;
        txtGame = AddText("txtGame", 12, y, 520, DefaultGameDir());
        AddButton("btnBrowse", "Browse...", 540, y - 1, 88, delegate { Browse(txtGame); });
        y += 30;
        AddButton("btnInstall", "Install / update co-op files into this game folder", 12, y, 616, delegate { Install(txtGame.Text, ""); });
        y += 40;

        GroupBox host = new GroupBox(); host.Text = "Host a story campaign"; host.Left = 12; host.Top = y; host.Width = 616; host.Height = 110; Controls.Add(host);
        AddLabelTo(host, "Chapter:", 10, 25);
        cmbChapter = new ComboBox(); cmbChapter.Name = "cmbChapter"; cmbChapter.DropDownStyle = ComboBoxStyle.DropDownList; cmbChapter.Left = 80; cmbChapter.Top = 22; cmbChapter.Width = 260;
        foreach (string[] c in Chapters) cmbChapter.Items.Add(c[0]);
        cmbChapter.SelectedIndex = 0; host.Controls.Add(cmbChapter);
        AddLabelTo(host, "Max players:", 360, 25);
        numPlayers = new NumericUpDown(); numPlayers.Name = "numPlayers"; numPlayers.Minimum = 2; numPlayers.Maximum = 5; numPlayers.Value = 5; numPlayers.Left = 450; numPlayers.Top = 22; numPlayers.Width = 50; host.Controls.Add(numPlayers);
        Button bh = new Button(); bh.Name = "btnHost"; bh.Text = "Host campaign"; bh.Left = 10; bh.Top = 60; bh.Width = 180; bh.Height = 36; bh.Click += delegate { HostCampaign(); }; host.Controls.Add(bh);
        Label hint = new Label(); hint.Left = 200; hint.Top = 62; hint.Width = 405; hint.Height = 40; hint.Text = "Friends join with your IP address (shown below). When the opening scenes are over, press F8 in the host window to spawn the partners next to you."; host.Controls.Add(hint);
        y += 118;

        GroupBox esc = new GroupBox(); esc.Text = "Host Escalation"; esc.Left = 12; esc.Top = y; esc.Width = 616; esc.Height = 62; Controls.Add(esc);
        AddLabelTo(esc, "Map:", 10, 28);
        cmbEsc = new ComboBox(); cmbEsc.Name = "cmbEsc"; cmbEsc.DropDownStyle = ComboBoxStyle.DropDownList; cmbEsc.Left = 80; cmbEsc.Top = 25; cmbEsc.Width = 200;
        foreach (string[] c in Escalation) cmbEsc.Items.Add(c[0]);
        cmbEsc.SelectedIndex = 0; esc.Controls.Add(cmbEsc);
        Button be = new Button(); be.Name = "btnHostEsc"; be.Text = "Host Escalation"; be.Left = 300; be.Top = 22; be.Width = 180; be.Height = 30; be.Click += delegate { HostEscalation(); }; esc.Controls.Add(be);
        y += 70;

        GroupBox join = new GroupBox(); join.Text = "Join a game"; join.Left = 12; join.Top = y; join.Width = 616; join.Height = 62; Controls.Add(join);
        AddLabelTo(join, "Host IP:", 10, 28);
        txtIp = new TextBox(); txtIp.Name = "txtIp"; txtIp.Left = 70; txtIp.Top = 25; txtIp.Width = 170; txtIp.Text = "127.0.0.1"; join.Controls.Add(txtIp);
        AddLabelTo(join, "Port:", 255, 28);
        numPort = new NumericUpDown(); numPort.Name = "numPort"; numPort.Minimum = 1; numPort.Maximum = 65535; numPort.Value = 7778; numPort.Left = 290; numPort.Top = 25; numPort.Width = 70; join.Controls.Add(numPort);
        Button bj = new Button(); bj.Name = "btnJoin"; bj.Text = "Join"; bj.Left = 380; bj.Top = 22; bj.Width = 180; bj.Height = 30; bj.Click += delegate { Join(txtGame.Text); }; join.Controls.Add(bj);
        y += 70;

        GroupBox opt = new GroupBox(); opt.Text = "Options / local test"; opt.Left = 12; opt.Top = y; opt.Width = 616; opt.Height = 130; Controls.Add(opt);
        chkWindowed = new CheckBox(); chkWindowed.Name = "chkWindowed"; chkWindowed.Text = "Windowed"; chkWindowed.Left = 10; chkWindowed.Top = 22; chkWindowed.Width = 90; chkWindowed.Checked = true; opt.Controls.Add(chkWindowed);
        numW = new NumericUpDown(); numW.Name = "numW"; numW.Minimum = 640; numW.Maximum = 7680; numW.Value = 960; numW.Left = 110; numW.Top = 20; numW.Width = 60; opt.Controls.Add(numW);
        AddLabelTo(opt, "x", 175, 24);
        numH = new NumericUpDown(); numH.Name = "numH"; numH.Minimum = 360; numH.Maximum = 4320; numH.Value = 540; numH.Left = 190; numH.Top = 20; numH.Width = 60; opt.Controls.Add(numH);
        AddLabelTo(opt, "Extra game arguments:", 270, 24);
        txtExtra = new TextBox(); txtExtra.Name = "txtExtra"; txtExtra.Left = 400; txtExtra.Top = 20; txtExtra.Width = 200; txtExtra.Text = "-nostartupmovies"; opt.Controls.Add(txtExtra);
        AddLabelTo(opt, "Second game copy (guest on this PC, optional):", 10, 58);
        txtGame2 = new TextBox(); txtGame2.Name = "txtGame2"; txtGame2.Left = 10; txtGame2.Top = 78; txtGame2.Width = 420; opt.Controls.Add(txtGame2);
        Button b2i = new Button(); b2i.Name = "btnInstall2"; b2i.Text = "Install"; b2i.Left = 440; b2i.Top = 76; b2i.Width = 70; b2i.Click += delegate { Install(txtGame2.Text, "B"); }; opt.Controls.Add(b2i);
        Button b2j = new Button(); b2j.Name = "btnJoin2"; b2j.Text = "Join with copy"; b2j.Left = 515; b2j.Top = 76; b2j.Width = 90; b2j.Click += delegate { Join(txtGame2.Text); }; opt.Controls.Add(b2j);
        Button brel = new Button(); brel.Name = "btnRelease"; brel.Text = "Spawn partners now"; brel.Left = 10; brel.Top = 100; brel.Width = 150; brel.Click += delegate { Release(); }; opt.Controls.Add(brel);
        Button blog = new Button(); blog.Name = "btnLog"; blog.Text = "Open game folder"; blog.Left = 170; blog.Top = 100; blog.Width = 130; blog.Click += delegate { OpenFolder(txtGame.Text); }; opt.Controls.Add(blog);
        y += 138;

        lblStatus = new Label(); lblStatus.Name = "lblStatus"; lblStatus.Left = 12; lblStatus.Top = y; lblStatus.Width = 616; lblStatus.Height = 40;
        lblStatus.Text = "Your addresses: " + LocalAddresses();
        Controls.Add(lblStatus);

        LoadSettings();
        FormClosing += delegate { SaveSettings(); };
    }

    // ---------------------------------------------------------------- UI helpers
    void AddLabel(string text, int x, int y) { Label l = new Label(); l.Text = text; l.Left = x; l.Top = y; l.Width = 600; Controls.Add(l); }
    void AddLabelTo(Control parent, string text, int x, int y) { Label l = new Label(); l.Text = text; l.Left = x; l.Top = y; l.AutoSize = true; parent.Controls.Add(l); }
    TextBox AddText(string name, int x, int y, int w, string text) { TextBox t = new TextBox(); t.Name = name; t.Left = x; t.Top = y; t.Width = w; t.Text = text; Controls.Add(t); return t; }
    void AddButton(string name, string text, int x, int y, int w, EventHandler h) { Button b = new Button(); b.Name = name; b.Text = text; b.Left = x; b.Top = y; b.Width = w; b.Height = 28; b.Click += h; Controls.Add(b); }
    void Browse(TextBox target) { using (FolderBrowserDialog d = new FolderBrowserDialog()) { if (d.ShowDialog() == DialogResult.OK) target.Text = d.SelectedPath; } }
    void Status(string s) { lblStatus.Text = s; Application.DoEvents(); }

    static string DefaultGameDir()
    {
        string[] c = new string[] {
            @"C:\Program Files (x86)\Steam\steamapps\common\Transformers Fall of Cybertron",
            @"C:\Program Files\Steam\steamapps\common\Transformers Fall of Cybertron",
            @"D:\SteamLibrary\steamapps\common\Transformers Fall of Cybertron" };
        foreach (string s in c) if (File.Exists(Path.Combine(s, @"Binaries\TFOC.exe"))) return s;
        return c[0];
    }

    static string LocalAddresses()
    {
        List<string> l = new List<string>();
        try { foreach (IPAddress a in Dns.GetHostAddresses(Dns.GetHostName())) if (a.AddressFamily == AddressFamily.InterNetwork) l.Add(a.ToString()); } catch { }
        return l.Count == 0 ? "(unknown)" : string.Join(", ", l.ToArray()) + "   (game port 7778/UDP; friends outside your LAN need a VPN such as ZeroTier/Tailscale)";
    }

    // ---------------------------------------------------------------- settings
    void LoadSettings()
    {
        try
        {
            if (!File.Exists(settingsPath)) return;
            foreach (string line in File.ReadAllLines(settingsPath))
            {
                int i = line.IndexOf('='); if (i < 0) continue;
                string k = line.Substring(0, i), v = line.Substring(i + 1);
                if (k == "game") txtGame.Text = v; else if (k == "game2") txtGame2.Text = v; else if (k == "ip") txtIp.Text = v;
                else if (k == "extra") txtExtra.Text = v;
            }
        }
        catch { }
    }

    void SaveSettings()
    {
        try { File.WriteAllText(settingsPath, "game=" + txtGame.Text + "\r\ngame2=" + txtGame2.Text + "\r\nip=" + txtIp.Text + "\r\nextra=" + txtExtra.Text + "\r\n"); } catch { }
    }

    // ---------------------------------------------------------------- actions
    static string Sha256(string path)
    {
        using (SHA256 h = SHA256.Create()) using (FileStream f = File.OpenRead(path))
        {
            StringBuilder sb = new StringBuilder();
            foreach (byte b in h.ComputeHash(f)) sb.Append(b.ToString("x2"));
            return sb.ToString();
        }
    }

    bool CheckGame(string dir)
    {
        string exe = Path.Combine(dir, @"Binaries\TFOC.exe");
        if (!File.Exists(exe)) { Status("TFOC.exe not found in " + dir + "\\Binaries"); return false; }
        return true;
    }

    void Install(string dir, string instance)
    {
        try
        {
            if (!CheckGame(dir)) return;
            string bin = Path.Combine(dir, "Binaries");
            string sha = Sha256(Path.Combine(bin, "TFOC.exe"));
            if (sha != ExpectedSha256)
            {
                if (MessageBox.Show("TFOC.exe is not the supported build (SHA-256 differs).\nThe co-op files may not work. Install anyway?", "Unknown game build", MessageBoxButtons.YesNo, MessageBoxIcon.Warning) != DialogResult.Yes) return;
            }
            string payload = Path.Combine(AppDomain.CurrentDomain.BaseDirectory, @"payload\dinput8.dll");
            if (!File.Exists(payload)) { Status("payload\\dinput8.dll is missing next to the launcher"); return; }
            File.Copy(payload, Path.Combine(bin, "dinput8.dll"), true);
            StringBuilder ini = new StringBuilder();
            ini.AppendLine("[foc_coop]");
            if (instance.Length > 0) ini.AppendLine("instance=" + instance);
            ini.AppendLine("tcpdriver=1");
            ini.AppendLine("forcechar=1");
            ini.AppendLine("maxplayers=5");
            ini.AppendLine("tutguard=1");
            ini.AppendLine("ctrlshare=1");
            ini.AppendLine("hostfirst=1");
            ini.AppendLine("rescue=1");
            File.WriteAllText(Path.Combine(bin, "foc_coop.ini"), ini.ToString());
            if (!File.Exists(Path.Combine(bin, "steam_appid.txt"))) File.WriteAllText(Path.Combine(bin, "steam_appid.txt"), "213120");
            Status("Installed into " + bin);
        }
        catch (Exception e) { Status("Install failed: " + e.Message); }
    }

    string CommonArgs()
    {
        StringBuilder sb = new StringBuilder();
        if (chkWindowed.Checked) sb.Append(" -windowed ResX=" + numW.Value + " ResY=" + numH.Value);
        sb.Append(" -log -nohomedir ");
        sb.Append(txtExtra.Text);
        return sb.ToString();
    }

    void Launch(string dir, string url)
    {
        try
        {
            if (!CheckGame(dir)) return;
            if (!File.Exists(Path.Combine(dir, @"Binaries\dinput8.dll"))) { Status("Co-op files are not installed in that folder - press Install first."); return; }
            ProcessStartInfo psi = new ProcessStartInfo(Path.Combine(dir, @"Binaries\TFOC.exe"), "\"" + url + "\"" + CommonArgs());
            psi.WorkingDirectory = Path.Combine(dir, "Binaries");
            psi.UseShellExecute = false;
            Process.Start(psi);
            Status("Started: " + url);
        }
        catch (Exception e) { Status("Launch failed: " + e.Message); }
    }

    void HostCampaign()
    {
        string map = Chapters[cmbChapter.SelectedIndex][1];
        Launch(txtGame.Text, map + "?Game=TransContent.TnCampaignGame?listen?MaxPlayers=" + numPlayers.Value);
    }

    void HostEscalation()
    {
        string map = Escalation[cmbEsc.SelectedIndex][1];
        Launch(txtGame.Text, map + "?Game=TransContent.TnSurvivalGame?listen?MaxPlayers=5");
    }

    void Join(string dir)
    {
        Launch(dir, txtIp.Text.Trim() + ":" + numPort.Value);
    }

    void Release()
    {
        try { File.WriteAllText(Path.Combine(txtGame.Text, @"Binaries\foc_coop.release"), "1"); Status("Release requested."); }
        catch (Exception e) { Status("Release failed: " + e.Message); }
    }

    void OpenFolder(string dir)
    {
        try { Process.Start("explorer.exe", Path.Combine(dir, "Binaries")); } catch { }
    }
}
