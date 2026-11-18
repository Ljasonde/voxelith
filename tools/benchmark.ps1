param([string]$Executable = "$PSScriptRoot/../out/Release/Voxelith.exe",
      [string]$Output = "$PSScriptRoot/../benchmarks")
$ErrorActionPreference = 'Stop'
$cases = @(
    @('rt', 1, 1, '1.0', 12), @('raster', 0, 1, '1.0', 12),
    @('rt-direct', 1, 0, '1.0', 12), @('rt-07', 1, 1, '0.7', 12),
    @('rt-8', 1, 1, '1.0', 8), @('raster-direct', 0, 0, '1.0', 12),
    @('rt-no-history', 1, 1, '1.0', 12)
)
foreach ($case in $cases) {
    $history = [int]($case[0] -ne 'rt-no-history')
    & $Executable --benchmark (Join-Path $Output $case[0]) --rt $case[1] --culling $case[2] --scale $case[3] --distance $case[4] --width 1920 --height 1080 --hiz $history
    if ($LASTEXITCODE) { exit $LASTEXITCODE }
}

