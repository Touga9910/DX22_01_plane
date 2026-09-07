param([int]$GameProcessId, [string]$Action = 'capture', [int]$X = 0, [int]$Y = 0, [int]$Key = 0, [string]$OutputPath)
Add-Type -AssemblyName System.Drawing
Add-Type @'
using System;
using System.Runtime.InteropServices;
public class MapWindowProbe {
 [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr dc, uint flags);
 [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
 [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out Rect r);
 [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint msg, IntPtr w, IntPtr l);
 [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int cmd);
 [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
 [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
 [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref Point p);
 [DllImport("user32.dll")] public static extern void mouse_event(uint flags, uint x, uint y, uint data, UIntPtr extra);
 [DllImport("user32.dll")] public static extern void keybd_event(byte key, byte scan, uint flags, UIntPtr extra);
 public struct Rect { public int left, top, right, bottom; }
 public struct Point { public int x, y; }
}
'@
[MapWindowProbe]::SetProcessDPIAware() | Out-Null
$mapHandle = (Get-Process -Id $GameProcessId).MainWindowHandle
if ($Action -eq 'close') { [MapWindowProbe]::PostMessage($mapHandle, 16, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null; return }
[MapWindowProbe]::ShowWindow($mapHandle, 9) | Out-Null
[MapWindowProbe]::SetForegroundWindow($mapHandle) | Out-Null
Start-Sleep -Milliseconds 300
if ($Action -eq 'click') {
 $mapPoint = New-Object MapWindowProbe+Point
 $mapPoint.x = $X; $mapPoint.y = $Y
 [MapWindowProbe]::ClientToScreen($mapHandle, [ref]$mapPoint) | Out-Null
 [MapWindowProbe]::SetCursorPos($mapPoint.x,$mapPoint.y) | Out-Null
 Start-Sleep -Milliseconds 100
 [MapWindowProbe]::mouse_event(2,0,0,0,[UIntPtr]::Zero)
 Start-Sleep -Milliseconds 100
 [MapWindowProbe]::mouse_event(4,0,0,0,[UIntPtr]::Zero)
} elseif ($Action -eq 'key') {
 [MapWindowProbe]::keybd_event($Key,0,0,[UIntPtr]::Zero)
 Start-Sleep -Milliseconds 120
 [MapWindowProbe]::keybd_event($Key,0,2,[UIntPtr]::Zero)
}
Start-Sleep -Milliseconds 600
if ($OutputPath) {
 $mapRect = New-Object MapWindowProbe+Rect
 [MapWindowProbe]::GetClientRect($mapHandle, [ref]$mapRect) | Out-Null
 $mapOrigin = New-Object MapWindowProbe+Point
 [MapWindowProbe]::ClientToScreen($mapHandle, [ref]$mapOrigin) | Out-Null
 $mapBitmap = New-Object System.Drawing.Bitmap($mapRect.right,$mapRect.bottom)
 $mapGraphics = [System.Drawing.Graphics]::FromImage($mapBitmap)
 $mapDc = $mapGraphics.GetHdc()
 [MapWindowProbe]::PrintWindow($mapHandle,$mapDc,3) | Out-Null
 $mapGraphics.ReleaseHdc($mapDc)
 $mapBitmap.Save($OutputPath)
 $mapGraphics.Dispose(); $mapBitmap.Dispose()
}
