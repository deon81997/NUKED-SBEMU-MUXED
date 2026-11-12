# Make Windows-made test volumes for fsread (Windows 7..11, PowerShell 2+, run as administrator).
# Creates FSRNTFS.VHD and FSREXFAT.VHD (96 MB fixed VHDs) next to this script, formats them with
# Windows itself, fills them with test files and writes FSRNTFS.TXT / FSREXFAT.TXT (what Windows
# sees: long name, 8.3 name, size, attributes, MD5). Send the 4 files back. Uses drive letters
# below while it runs; change them if they are taken.
$L_NTFS = 'W'; $L_EXFAT = 'V'
$ErrorActionPreference = 'Stop'
$out = Split-Path -Parent $MyInvocation.MyCommand.Path
$id = [Security.Principal.WindowsIdentity]::GetCurrent()
if (-not (New-Object Security.Principal.WindowsPrincipal $id).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    Write-Host 'Run this as administrator (right-click MKVHD.BAT, Run as administrator).'; exit 1 }

function DP($text) {
    $f = Join-Path $env:TEMP 'fsr_dp.txt'
    [IO.File]::WriteAllText($f, $text, [Text.Encoding]::ASCII)
    $r = & diskpart /s $f
    if ($LASTEXITCODE -ne 0) { $r | Write-Host; throw 'diskpart failed' }
}
function Bytes($n, $seed) { $b = New-Object byte[] $n; (New-Object Random $seed).NextBytes($b); $b }
function Put($p, $n, $seed) { [IO.File]::WriteAllBytes($p, (Bytes $n $seed)) }
function Txt($p, $s) { [IO.File]::WriteAllText($p, $s) }

function Fill($r, $ntfs) {
    $u = [string][char]0xFC + 'n' + [char]0xEF + 'code ' + [char]0x2713 + '.txt'   # unicode name
    Txt "$r\small.txt" "hello`n"; Txt "$r\readme.txt" "readme`r`n"; Txt "$r\UPPER.TXT" "upper`n"
    1..12 | ForEach-Object { Txt ("$r\Long File Name {0:D2}.txt" -f $_) "lfn $_`n" }
    Txt "$r\a b.c.d.txt" 'dots'; Txt "$r\$u" 'uni'; Txt "$r\NoExtensionLongName" 'noext'
    [IO.File]::WriteAllBytes("$r\empty.txt", (New-Object byte[] 0))
    Txt "$r\hidden.txt" 'h'; Txt "$r\system.txt" 's'; Txt "$r\readonly.txt" 'r'
    (Get-Item "$r\hidden.txt").Attributes = 'Hidden'; (Get-Item "$r\system.txt").Attributes = 'System'
    (Get-Item "$r\readonly.txt").Attributes = 'ReadOnly'
    New-Item -ItemType Directory "$r\Sub Dir\Deeper Level\third" | Out-Null
    Put "$r\Sub Dir\Deeper Level\x.bin" 100000 1; Txt "$r\Sub Dir\Deeper Level\third\deep.txt" 'deep'
    Put "$r\contig.bin" 3000000 2
    New-Item -ItemType Directory "$r\frag" | Out-Null
    1..40 | ForEach-Object { Put "$r\frag\p$_" 65536 (100 + $_) }
    1..40 | Where-Object { $_ % 2 } | ForEach-Object { Remove-Item "$r\frag\p$_" }
    Put "$r\frag\fragmented.bin" 2500000 3
    New-Item -ItemType Directory "$r\many" | Out-Null
    0..599 | ForEach-Object { Txt "$r\many\file with long name $_.dat" "$_" }
    if ($ntfs) {
        $f = "$r\sparse.bin"; [IO.File]::WriteAllBytes($f, (New-Object byte[] 0))
        & fsutil sparse setflag $f | Out-Null
        $s = [IO.File]::OpenWrite($f); $s.SetLength(64MB)
        foreach ($o in 0, 5MB, 33MB, 64MB - 4096) { $s.Position = $o; $b = Bytes 4096 $o; $s.Write($b, 0, 4096) }
        $s.Close()
        Txt "$r\compressed.txt" ('compress me ' * 20000); & compact /c "$r\compressed.txt" | Out-Null
        Txt "$r\ads.txt" 'main stream'; & cmd /c "echo secret> `"$r\ads.txt:hidden`"" | Out-Null
        New-Item -ItemType Directory "$r\links" | Out-Null
        Txt "$r\links\target.txt" 'linked'
        1..150 | ForEach-Object { & fsutil hardlink create "$r\links\hard link number $_.txt" "$r\links\target.txt" | Out-Null }
    }
}

function List($r, $txt, $fs) {
    $fso = New-Object -ComObject Scripting.FileSystemObject
    $md5 = [Security.Cryptography.MD5]::Create()
    $lines = New-Object Collections.ArrayList
    [void]$lines.Add("#fs=$fs os=$([Environment]::OSVersion.VersionString)")
    if ($fs -eq 'NTFS') { [void]$lines.Add('#8dot3 ' + ((& fsutil 8dot3name query "$($r.Substring(0,2))") -join ' ')) }
    Get-ChildItem -LiteralPath $r -Recurse -Force -ErrorAction SilentlyContinue |
      Where-Object { $_.FullName -notlike '*System Volume Information*' } | ForEach-Object {
        $rel = $_.FullName.Substring(2).Replace('\', '/')
        if ($_.PSIsContainer) {
            $sp = $fso.GetFolder($_.FullName).ShortPath.Substring(2)
            [void]$lines.Add("D|$rel|$sp|$([int]$_.Attributes)")
        } else {
            $sp = $fso.GetFile($_.FullName).ShortPath.Substring(2)
            $h = ($md5.ComputeHash([IO.File]::ReadAllBytes($_.FullName)) | ForEach-Object { $_.ToString('x2') }) -join ''
            [void]$lines.Add("F|$rel|$sp|$($_.Length)|$([int]$_.Attributes)|$h")
        }
    }
    [IO.File]::WriteAllLines($txt, [string[]]$lines, (New-Object Text.UTF8Encoding $false))
}

function Make($fs, $letter, $name) {
    $vhd = Join-Path $out "$name.VHD"
    if (Test-Path "${letter}:\") { throw "Drive ${letter}: is in use: change `$L_NTFS / `$L_EXFAT at the top." }
    if (Test-Path $vhd) { Remove-Item $vhd }
    Write-Host "Making $vhd ($fs) ..."
    DP "create vdisk file=`"$vhd`" maximum=96 type=fixed`r`nselect vdisk file=`"$vhd`"`r`nattach vdisk`r`ncreate partition primary`r`nformat fs=$fs quick label=$name`r`nassign letter=$letter`r`n"
    for ($i = 0; $i -lt 30 -and -not (Test-Path "${letter}:\"); $i++) { Start-Sleep 1 }
    if ($fs -eq 'NTFS') { & fsutil 8dot3name set "${letter}:" 0 | Out-Null }   # make 8.3 names like a system drive
    try { Fill "${letter}:" ($fs -eq 'NTFS'); List "${letter}:" (Join-Path $out "$name.TXT") $fs }
    finally { DP "select vdisk file=`"$vhd`"`r`ndetach vdisk`r`n" }
}

Make 'NTFS' $L_NTFS 'FSRNTFS'
Make 'EXFAT' $L_EXFAT 'FSREXFAT'
Write-Host "Done. Send back: FSRNTFS.VHD FSRNTFS.TXT FSREXFAT.VHD FSREXFAT.TXT (in $out)"
