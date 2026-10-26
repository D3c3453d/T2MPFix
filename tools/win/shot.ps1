# Screenshot every visible window of the given process name (default Thief2MP) to <outdir>\<pid>.png
param([string]$Proc = "Thief2MP", [string]$OutDir = "D:\thief_coop_test\shots")
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System; using System.Runtime.InteropServices;
public class W {
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint f);
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
}
"@
[W]::SetProcessDPIAware() | Out-Null
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
foreach ($p in Get-Process -Name $Proc -ErrorAction SilentlyContinue) {
  $h = $p.MainWindowHandle
  if ($h -eq [IntPtr]::Zero) { continue }
  $r = New-Object W+RECT
  [W]::GetWindowRect($h, [ref]$r) | Out-Null
  $w = $r.R - $r.L; $ht = $r.B - $r.T
  if ($w -le 0 -or $ht -le 0) { continue }
  $bmp = New-Object System.Drawing.Bitmap $w, $ht
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $hdc = $g.GetHdc()
  [W]::PrintWindow($h, $hdc, 2) | Out-Null   # PW_RENDERFULLCONTENT: works when occluded
  $g.ReleaseHdc($hdc); $g.Dispose()
  $f = Join-Path $OutDir ("{0}.png" -f $p.Id)
  $bmp.Save($f); $bmp.Dispose()
  Write-Output "$($p.Id) $w x $ht -> $f title='$($p.MainWindowTitle)'"
}
