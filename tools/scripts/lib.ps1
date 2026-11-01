# Helpers shared by the PowerShell tools (release.ps1, make_anyx.ps1, tools\regress\*.ps1). Dot-source it:
#   . "$PSScriptRoot\lib.ps1"
#
# - Get-TermsHash <launcher_terms.h>: the launcher's terms-accepted hash (FNV-1a 64 of the two
#   R"terms(...)terms" texts, \r removed), as terms_hash.py computes it. A sandbox LOCALAPPDATA needs
#   it in AnyX360\terms_accepted.txt, else the launcher stops at the terms screen.
# - Find-GameWindow / Save-GameWindow: a visible top-level window with EXACTLY this title, optionally
#   only among some process IDs, and a PrintWindow capture of its client area into a PNG. Never match
#   a title by substring: one run captured a browser window whose tab named the game.
# - Get-IdleSeconds: seconds since the last keyboard or mouse input on this machine.

if (-not ('AnyX360Tools.Win' -as [type])) {
  Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
using System.Text;
namespace AnyX360Tools {
public static class Win {
  delegate bool EnumProc(IntPtr hwnd, IntPtr lparam);
  [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc proc, IntPtr lparam);
  [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint pid);
  [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern int GetWindowText(IntPtr hwnd, StringBuilder text, int max);
  [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr hwnd);
  [DllImport("user32.dll")] static extern bool GetClientRect(IntPtr hwnd, out RECT rect);
  [DllImport("user32.dll")] static extern bool PrintWindow(IntPtr hwnd, IntPtr hdc, uint flags);
  [DllImport("user32.dll")] static extern bool SetProcessDpiAwarenessContext(IntPtr v);
  [DllImport("user32.dll")] static extern bool GetLastInputInfo(ref LASTINPUTINFO info);
  [StructLayout(LayoutKind.Sequential)] struct RECT { public int Left, Top, Right, Bottom; }
  [StructLayout(LayoutKind.Sequential)] struct LASTINPUTINFO { public uint cbSize; public uint dwTime; }
  static Win() { SetProcessDpiAwarenessContext(new IntPtr(-4)); }

  // The visible window titled exactly `title`; with pids, only theirs.
  public static IntPtr Find(string title, uint[] pids) {
    IntPtr found = IntPtr.Zero;
    HashSet<uint> set = pids == null || pids.Length == 0 ? null : new HashSet<uint>(pids);
    EnumWindows((h, l) => {
      uint pid; GetWindowThreadProcessId(h, out pid);
      if ((set != null && !set.Contains(pid)) || !IsWindowVisible(h)) return true;
      var sb = new StringBuilder(512); GetWindowText(h, sb, 512);
      if (sb.ToString() == title) { found = h; return false; }
      return true;
    }, IntPtr.Zero);
    return found;
  }

  // The titles of the visible windows of these processes (to report what a run showed).
  public static string[] Titles(uint[] pids) {
    var list = new List<string>();
    var set = new HashSet<uint>(pids);
    EnumWindows((h, l) => {
      uint pid; GetWindowThreadProcessId(h, out pid);
      if (!set.Contains(pid) || !IsWindowVisible(h)) return true;
      var sb = new StringBuilder(512); GetWindowText(h, sb, 512);
      if (sb.Length > 0) list.Add(sb.ToString());
      return true;
    }, IntPtr.Zero);
    return list.ToArray();
  }

  // The client area (the game's image) as a PNG; false when minimised or gone.
  public static bool Save(IntPtr hwnd, string path) {
    RECT r; if (!GetClientRect(hwnd, out r)) return false;
    int w = r.Right - r.Left, h = r.Bottom - r.Top;
    if (w < 64 || h < 64) return false;
    using (var bmp = new Bitmap(w, h, PixelFormat.Format32bppArgb)) {
      using (var g = Graphics.FromImage(bmp)) {
        IntPtr hdc = g.GetHdc();
        PrintWindow(hwnd, hdc, 3);  // PW_CLIENTONLY | PW_RENDERFULLCONTENT
        g.ReleaseHdc(hdc);
      }
      bmp.Save(path, ImageFormat.Png);
    }
    return true;
  }

  // Share of pixels (sampled) that aren't near-black: a black or blank capture scores about 0.
  public static double Lit(string path) {
    using (var bmp = new Bitmap(path)) {
      long n = 0, lit = 0;
      for (int y = 0; y < bmp.Height; y += 8)
        for (int x = 0; x < bmp.Width; x += 8) {
          Color c = bmp.GetPixel(x, y); ++n;
          if (c.R + c.G + c.B > 60) ++lit;
        }
      return n == 0 ? 0 : (double)lit / n;
    }
  }

  public static double IdleSeconds() {
    var info = new LASTINPUTINFO(); info.cbSize = (uint)Marshal.SizeOf(info);
    if (!GetLastInputInfo(ref info)) return -1;
    return ((uint)Environment.TickCount - info.dwTime) / 1000.0;
  }

  // FNV-1a 64 over bytes, continuing from `value`.
  public static ulong Fnv(ulong value, byte[] data) {
    unchecked { foreach (byte c in data) value = (value ^ c) * 1099511628211UL; }
    return value;
  }
}
}
'@
}

function Get-TermsHash([string]$Header) {
  if (-not (Test-Path -LiteralPath $Header)) { throw "Get-TermsHash: $Header is missing (build anyx360_launcher first)" }
  $bytes = [IO.File]::ReadAllBytes($Header)
  # Latin-1 maps bytes to chars one to one, so indexes into the text are indexes into the bytes.
  $text = [Text.Encoding]::GetEncoding(28591).GetString($bytes)
  $value = [uint64]14695981039346656037
  $start = 0
  for ($i = 0; $i -lt 2; $i++) {
    $a = $text.IndexOf('R"terms(', $start, [StringComparison]::Ordinal)
    if ($a -lt 0) { throw "Get-TermsHash: $Header has fewer than two terms texts" }
    $a += 8
    $b = $text.IndexOf(')terms"', $a, [StringComparison]::Ordinal)
    $part = $text.Substring($a, $b - $a).Replace("`r`n", "`n")
    $value = [AnyX360Tools.Win]::Fnv($value, [Text.Encoding]::GetEncoding(28591).GetBytes($part))
    $start = $b
  }
  return $value.ToString('x16')
}

function Find-GameWindow([string]$Title, [uint32[]]$ProcessIds = @()) {
  return [AnyX360Tools.Win]::Find($Title, $ProcessIds)
}

# Saves the window titled exactly $Title (among $ProcessIds when given) to $Path; $true when saved.
function Save-GameWindow([string]$Title, [string]$Path, [uint32[]]$ProcessIds = @()) {
  $h = [AnyX360Tools.Win]::Find($Title, $ProcessIds)
  if ($h -eq [IntPtr]::Zero) { return $false }
  return [AnyX360Tools.Win]::Save($h, $Path)
}

function Get-IdleSeconds { return [AnyX360Tools.Win]::IdleSeconds() }
