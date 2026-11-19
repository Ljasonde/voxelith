param([string]$Configuration = 'Release')
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$bin = Join-Path $root "out/$Configuration"
$target = Join-Path $root 'downloads/Voxelith-Windows-x64.zip'
$stage = Join-Path $root ('build/package-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $stage | Out-Null
New-Item -ItemType Directory -Force -Path (Split-Path $target) | Out-Null
Copy-Item -LiteralPath (Join-Path $bin 'Voxelith.exe') -Destination $stage
Copy-Item -LiteralPath (Join-Path $root 'LICENSE'),(Join-Path $root 'THIRD_PARTY_NOTICES.md') -Destination $stage
$shaderDir = New-Item -ItemType Directory -Path (Join-Path $stage 'shaders')
Get-ChildItem -LiteralPath (Join-Path $root 'shaders') -File |
    Where-Object Extension -In '.vert','.frag','.comp' | ForEach-Object {
        Copy-Item -LiteralPath (Join-Path $bin ('shaders/' + $_.Name + '.spv')) -Destination $shaderDir.FullName
    }
@'
Voxelith | Windows x64
Extract the entire archive, then launch Voxelith.exe. Requires a Vulkan 1.3 graphics driver.
Select New expedition. Collect one core from each ruin with the right mouse button.
Return to the purple gateway on the spawn island after collecting all three.
WASD move, mouse look, Shift sprint, Space jump, F flight, Ctrl descend.
Left click to mine, right click to build, 1-9 or wheel to select a block.
Esc pauses and saves. F2 hides the HUD and freezes time. F3 displays frame statistics.
Settings includes ray tracing, GPU culling, shadows, render scale and view distance.
Benchmark: Voxelith.exe --present --benchmark benchmarks/window --width 1920 --height 1080
4K image: Voxelith.exe --capture captures/spire.png --width 3840 --height 2160
Source, documentation, raw benchmark data and film: https://github.com/Ljasonde/voxelith
'@ | Set-Content -LiteralPath (Join-Path $stage 'README.txt') -Encoding ascii
Add-Type -AssemblyName System.IO.Compression.FileSystem
$tempZip = "$stage.zip"
[IO.Compression.ZipFile]::CreateFromDirectory($stage, $tempZip)
Copy-Item -LiteralPath $tempZip -Destination $target -Force
Get-FileHash -Algorithm SHA256 -LiteralPath $target
