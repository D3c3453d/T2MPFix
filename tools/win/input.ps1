# Drive a game window with real input (the game reads the mouse through DirectInput, so posted
# messages are ignored). Brings the window to the front; moves the real cursor briefly.
#   input.ps1 -ProcId <pid> -Click x,y        left click at client coords (800x600 game space)
#   input.ps1 -ProcId <pid> -Key  0x0D         press and release a virtual key
#   input.ps1 -ProcId <pid> -Text "abc"        type text
# Not DPI-aware on purpose: coordinates are virtualized the same way as for the game.
param([int]$ProcId, [int[]]$Click, [int]$Key = -1, [string]$Text = "")
Add-Type @"
using System; using System.Runtime.InteropServices;
public class In {
  [StructLayout(LayoutKind.Sequential)] public struct PT { public int X, Y; }
  [StructLayout(LayoutKind.Sequential)] public struct MI { public int dx, dy; public uint data, flags, time; public IntPtr extra; }
  [StructLayout(LayoutKind.Sequential)] public struct KI { public ushort vk, scan; public uint flags, time; public IntPtr extra; public int pad1, pad2; }
  [StructLayout(LayoutKind.Explicit)] public struct INPUT { [FieldOffset(0)] public uint type; [FieldOffset(4)] public MI mi; [FieldOffset(4)] public KI ki; }
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref PT p);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern uint SendInput(uint n, INPUT[] i, int sz);
  [DllImport("user32.dll")] public static extern uint MapVirtualKey(uint c, uint t);
  [DllImport("user32.dll")] public static extern short VkKeyScan(char c);
  public static void Mouse(uint f) { var i = new INPUT[1]; i[0].type = 0; i[0].mi.flags = f; SendInput(1, i, Marshal.SizeOf(typeof(INPUT))); }
  public static void Move(int dx, int dy) { var i = new INPUT[1]; i[0].type = 0; i[0].mi.dx = dx; i[0].mi.dy = dy; i[0].mi.flags = 1; SendInput(1, i, Marshal.SizeOf(typeof(INPUT))); }
  public static void Kb(ushort vk, bool up) { var i = new INPUT[1]; i[0].type = 1; i[0].ki.vk = vk; i[0].ki.scan = (ushort)MapVirtualKey(vk, 0); i[0].ki.flags = up ? 2u : 0u; SendInput(1, i, Marshal.SizeOf(typeof(INPUT))); }
}
"@
$h = (Get-Process -Id $ProcId).MainWindowHandle
(New-Object -ComObject WScript.Shell).AppActivate($ProcId) | Out-Null
[In]::SetForegroundWindow($h) | Out-Null
Start-Sleep -Milliseconds 300
if ($Click) {
  $p = New-Object In+PT; $p.X = $Click[0]; $p.Y = $Click[1]
  [In]::ClientToScreen($h, [ref]$p) | Out-Null
  [In]::SetCursorPos($p.X, $p.Y) | Out-Null
  Start-Sleep -Milliseconds 120
  # relative moves generate raw input (WM_INPUT), which the game's mouse code needs
  for ($k = 0; $k -lt 4; $k++) { [In]::Move(2, 0); Start-Sleep -Milliseconds 30; [In]::Move(-2, 0); Start-Sleep -Milliseconds 30 }
  [In]::SetCursorPos($p.X, $p.Y) | Out-Null; Start-Sleep -Milliseconds 120
  [In]::Mouse(0x0002); Start-Sleep -Milliseconds 80; [In]::Mouse(0x0004)
}
if ($Key -ge 0) { [In]::Kb([uint16]$Key, $false); Start-Sleep -Milliseconds 80; [In]::Kb([uint16]$Key, $true) }
foreach ($c in $Text.ToCharArray()) {
  $v = [In]::VkKeyScan($c); $vk = [uint16]($v -band 0xff); $shift = ($v -band 0x100) -ne 0
  if ($shift) { [In]::Kb(0x10, $false) }
  [In]::Kb($vk, $false); Start-Sleep -Milliseconds 30; [In]::Kb($vk, $true)
  if ($shift) { [In]::Kb(0x10, $true) }
  Start-Sleep -Milliseconds 30
}
