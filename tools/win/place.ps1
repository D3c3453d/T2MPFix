# Restore, place topmost without activating, and capture each game window from the screen.
#   place.ps1 -ProcId <pid> -X <x> -Y <y> [-Shot <png>]
param([int]$ProcId, [int]$X = 0, [int]$Y = 0, [string]$Shot = "")
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System; using System.Runtime.InteropServices;
public class Pl { [StructLayout(LayoutKind.Sequential)] public struct R { public int L,T,Rt,B; }
 [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
 [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int c);
 [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr h);
 [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out R r);
 [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr a, int x, int y, int cx, int cy, uint f); }
"@
[Pl]::SetProcessDPIAware() | Out-Null
$h = (Get-Process -Id $ProcId).MainWindowHandle
if ([Pl]::IsIconic($h)) { [Pl]::ShowWindow($h, 4) | Out-Null }
# HWND_TOPMOST, SWP_NOSIZE|SWP_NOACTIVATE|SWP_SHOWWINDOW
[Pl]::SetWindowPos($h, [IntPtr](-1), $X, $Y, 0, 0, 0x0001 -bor 0x0010 -bor 0x0040) | Out-Null
if ($Shot) {
  Start-Sleep -Milliseconds 200
  $r = New-Object Pl+R; [Pl]::GetWindowRect($h, [ref]$r) | Out-Null
  $bmp = New-Object System.Drawing.Bitmap ($r.Rt - $r.L), ($r.B - $r.T)
  $g = [System.Drawing.Graphics]::FromImage($bmp); $g.CopyFromScreen($r.L, $r.T, 0, 0, $bmp.Size); $g.Dispose()
  $bmp.Save($Shot); $bmp.Dispose()
}
