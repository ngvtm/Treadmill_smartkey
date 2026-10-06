# Kiem chung bo test co that su bat loi khong: co tinh gay loi vao source,
# chay lai test, roi hoan nguyen. Moi mutation PHAI lam test do.
#   powershell -File test/host/mutation_check.ps1
#
# Doc/ghi bang byte tho + UTF-8 khong BOM: Get-Content/Set-Content mac dinh
# dung ANSI codepage va se lam hong comment tieng Viet trong source.
#
# Moi chuoi 'from' phai nam tren MOT dong. PowerShell khong co chuoi ba
# nhay nhu Python, va here-string long trong hashtable rat de sai cu phap.

$ErrorActionPreference = 'Continue'

$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$here = $PSScriptRoot
$utf8 = New-Object System.Text.UTF8Encoding($false)

function Read-Src([string]$path) {
    return [System.IO.File]::ReadAllText($path, [System.Text.Encoding]::UTF8)
}
function Write-Src([string]$path, [string]$text) {
    [System.IO.File]::WriteAllBytes($path, $utf8.GetBytes($text))
}

$mutations = @(
    @{
        name = 'M1 changed() bo sot truong zone'
        file = "$root/main/lcd/lcd_display.c"
        from = '        || a->zone            != b->zone'
        to   = '        || false'
    },
    @{
        name = 'M2 kich thuoc framebuffer sai (dung be rong thay chieu cao)'
        file = "$root/main/lcd/lcd_gfx.c"
        from = '#define EPD_FB_SIZE (EPD_STRIDE * EPD_PANEL_H)'
        to   = '#define EPD_FB_SIZE (EPD_STRIDE * EPD_PANEL_W)'
    },
    @{
        name = 'M3 bo chan lenh toc do khi dieu khien dang tat'
        file = "$root/main/treadmill_ctrl/treadmill_ctrl.c"
        from = 'if (!s_enabled || !ble_client_is_ready()) {'
        to   = 'if (!ble_client_is_ready()) {'
    },
    @{
        name = 'M4 dai toc do cao qua, de len dai canh bao'
        file = "$root/main/lcd/lcd_display.c"
        from = '#define SP_H       (LCD_H * 36 / 100)'
        to   = '#define SP_H       (LCD_H * 60 / 100)'
    },
    @{
        name = 'M5 quen chuyen tiep co safety_limited len man hinh'
        file = "$root/main/treadmill_ctrl/treadmill_ctrl.c"
        from = '.safety_limited  = s_server_safety_limited != 0,'
        to   = '.safety_limited  = false,'
    },
    @{
        name = 'M6 khong xoa so lieu cu khi mat ket noi'
        file = "$root/main/treadmill_ctrl/treadmill_ctrl.c"
        from = '    s_server_speed = 0;'
        to   = '    /* mutated */'
    },
    @{
        name = 'M7 dat D/C vao GPIO34 (chan chi vao duoc)'
        file = "$root/main/lcd/lcd_gfx.h"
        from = '#define EPD_PIN_DC   26'
        to   = '#define EPD_PIN_DC   34'
    },
    @{
        name = 'M8 CS trung chan voi D/C'
        file = "$root/main/lcd/lcd_gfx.h"
        from = '#define EPD_PIN_CS   27'
        to   = '#define EPD_PIN_CS   26'
    },
    @{
        name = 'M9 dao nham cuc tinh bit 1bpp'
        file = "$root/main/lcd/lcd_gfx.c"
        from = '        *b = (uint8_t)(*b & ~mask);'
        to   = '        *b = (uint8_t)(*b | mask);'
    },
    @{
        name = 'M10 quen kich hoat refresh (anh nam trong RAM)'
        file = "$root/main/lcd/lcd_gfx.c"
        from = '    tx_cmd(CMD_MASTER_ACT, NULL, 0);'
        to   = '    /* mutated */;'
    },
    @{
        name = 'M11 so gate sai (dung be rong thay vi chieu cao)'
        file = "$root/main/lcd/lcd_gfx.c"
        from = '        (uint8_t)((EPD_PANEL_H - 1) & 0xFF),'
        to   = '        (uint8_t)((EPD_PANEL_W - 1) & 0xFF),'
    },
    @{
        name = 'M12 fit_scale tra co qua lon, chu bi bo'
        file = "$root/main/lcd/lcd_gfx.c"
        from = '            return sc;'
        to   = '            return max_scale;'
    }
)

$caught = 0
foreach ($m in $mutations) {
    $orig = Read-Src $m.file
    if (-not $orig.Contains($m.from)) {
        Write-Host "SKIP    $($m.name): khong tim thay doan can sua" -ForegroundColor Yellow
        continue
    }

    Write-Src $m.file ($orig.Replace($m.from, $m.to))
    $output = & powershell -ExecutionPolicy Bypass -File "$here/run_tests.ps1" 2>&1
    $rc = $LASTEXITCODE
    Write-Src $m.file $orig

    if ($rc -ne 0) {
        Write-Host "CAUGHT  $($m.name)" -ForegroundColor Green
        $output | Select-String -Pattern '  FAIL' | Select-Object -First 1 |
            ForEach-Object { Write-Host "        $($_.Line.Trim())" }
        $caught++
    } else {
        Write-Host "MISSED  $($m.name) -- test van pass!" -ForegroundColor Red
    }
}

Write-Host ""
Write-Host "$caught/$($mutations.Count) mutation bi bat"
exit ($(if ($caught -eq $mutations.Count) { 0 } else { 1 }))
