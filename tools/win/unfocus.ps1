Add-Type @"
using System; using System.Runtime.InteropServices;
public class Uf { [DllImport("user32.dll")] public static extern IntPtr GetShellWindow();
 [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h); }
"@
(New-Object -ComObject WScript.Shell).AppActivate("Program Manager") | Out-Null
[Uf]::SetForegroundWindow([Uf]::GetShellWindow()) | Out-Null
