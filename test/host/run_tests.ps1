# Build va chay bo test host (khong can phan cung, khong can ESP-IDF).
#   powershell -File test/host/run_tests.ps1
#
# Cac module duoc bien dich native voi lop stub trong test/host/stubs,
# esp_lcd_panel_draw_bitmap() do vao framebuffer gia nen test assert
# duoc tren pixel that.

# Khong dung 'Stop': gcc ghi warning ra stderr, PowerShell coi do la loi.
$ErrorActionPreference = 'Continue'

$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$here = $PSScriptRoot
$out  = Join-Path $here 'build'

New-Item -ItemType Directory -Force -Path $out | Out-Null

$inc = @(
    "-I$here",
    "-I$here/stubs",
    "-I$root/main",
    "-I$root/main/lcd",
    "-I$root/main/treadmill_ctrl"
)
# Bo test kiem duong SPI phan cung; duong bit-bang la cong cu chan doan.
$flags = @('-std=gnu17', '-Wall', '-Wextra', '-Wno-unused-parameter', '-g', '-O1')

$suites = @(
    @{ name = 'test_gfx';     srcs = @("$here/test_gfx.c",     "$root/main/lcd/lcd_gfx.c", "$here/fake_panel.c") },
    @{ name = 'test_display'; srcs = @("$here/test_display.c", "$root/main/lcd/lcd_gfx.c", "$here/fake_panel.c") },
    @{ name = 'test_ctrl';    srcs = @("$here/test_ctrl.c",    "$root/main/treadmill_ctrl/treadmill_ctrl.c") }
)

$failed = 0
foreach ($s in $suites) {
    $exe = Join-Path $out "$($s.name).exe"
    Write-Host "==> compiling $($s.name)"
    & gcc @flags @inc $s.srcs -o $exe
    if ($LASTEXITCODE -ne 0) {
        Write-Host "compile FAILED: $($s.name)" -ForegroundColor Red
        $failed++
        continue
    }
    & $exe
    if ($LASTEXITCODE -ne 0) { $failed++ }
}

Write-Host ""
if ($failed -eq 0) {
    Write-Host "ALL SUITES PASSED" -ForegroundColor Green
    exit 0
} else {
    Write-Host "$failed SUITE(S) FAILED" -ForegroundColor Red
    exit 1
}
