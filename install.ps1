param([switch]$Uninstall)
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath($PSScriptRoot)
$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) { throw 'Administrator privileges are required for MIDI device registration.' }
$subkey = 'SOFTWARE\Microsoft\Windows NT\CurrentVersion\Drivers32'
$views = @([Microsoft.Win32.RegistryView]::Registry32, [Microsoft.Win32.RegistryView]::Registry64)
$changes = @()
try {
    foreach ($view in $views) {
        $file = if ($view -eq [Microsoft.Win32.RegistryView]::Registry32) { 'FaithMidi32.dll' } else { 'FaithMidi64.dll' }
        $target = Join-Path $root $file
        if (-not $Uninstall) {
            foreach ($required in @($target, (Join-Path $root 'FaithMidiSettings.exe'))) {
                if (-not (Test-Path -LiteralPath $required -PathType Leaf)) { throw "Missing: $required" }
            }
        }
        $base = [Microsoft.Win32.RegistryKey]::OpenBaseKey([Microsoft.Win32.RegistryHive]::LocalMachine,$view)
        $key = $base.OpenSubKey($subkey,$true)
        try {
            $existing = @($key.GetValueNames() | Where-Object { $_ -match '^midi\d*$' -and $key.GetValue($_) -eq $target })
            if ($Uninstall) {
                foreach ($name in $existing) { $key.DeleteValue($name); Write-Output "Removed $view $name = $target" }
            } elseif (-not $existing.Count) {
                $slot = $null
                foreach ($i in 0..31) {
                    $name = if ($i -eq 0) { 'midi' } else { "midi$i" }
                    if ($null -eq $key.GetValue($name)) { $slot=$name;break }
                }
                if ($null -eq $slot) { throw "No free MIDI registry slot in $view" }
                $key.SetValue($slot,$target,[Microsoft.Win32.RegistryValueKind]::String)
                $changes += ,@($view,$slot,$target)
                Write-Output "Registered $view $slot = $target"
            } else { Write-Output "Already registered $view = $target" }
        } finally { if($key){$key.Dispose()};$base.Dispose() }
    }
} catch {
    foreach ($change in $changes) {
        $base = [Microsoft.Win32.RegistryKey]::OpenBaseKey([Microsoft.Win32.RegistryHive]::LocalMachine,$change[0])
        $key=$base.OpenSubKey($subkey,$true)
        try { if($key.GetValue($change[1]) -eq $change[2]) { $key.DeleteValue($change[1]) } }
        finally { $key.Dispose();$base.Dispose() }
    }
    throw
}
Write-Output 'Finished. Reopen MIDI applications and CoolSoft MIDI Mapper.'
