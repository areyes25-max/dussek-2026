# compilar-dussek-na-magicos.ps1 - compila en tu PC la version NA_Magicos de Dussek (interfaz dorada,
# --theme deco, --background) con MSYS2, igual que la compilacion oficial de Windows (GitHub Actions).
# Uso (PowerShell normal, NO como administrador):
#   Set-ExecutionPolicy -Scope Process Bypass
#   .\compilar-dussek-na-magicos.ps1
# Tarda 20-40 min la primera vez (descarga ~2 GB de herramientas). Las siguientes, unos minutos.
$ErrorActionPreference = 'Stop'
$base   = "$env:USERPROFILE\Dussek"
$zip    = "$base\fuente\dussek-na-magicos-fuente.zip"
$salida = "$base\dussek-na-magicos"
$msys   = 'C:\msys64\usr\bin\bash.exe'

if (-not (Test-Path $zip)) { throw "No encuentro $zip" }
if (-not (Test-Path $msys)) {
    Write-Host '[1/4] Instalando MSYS2 (compiladores)...' -ForegroundColor Cyan
    winget install --id MSYS2.MSYS2 -e --accept-source-agreements --accept-package-agreements
    if (-not (Test-Path $msys)) { throw 'MSYS2 no quedo en C:\msys64. Instalalo desde https://www.msys2.org y vuelve a ejecutar.' }
}
$env:MSYSTEM = 'UCRT64'; $env:CHERE_INVOKING = '1'
Write-Host '[2/4] Herramientas: gcc, cmake, ninja, rust...' -ForegroundColor Cyan
& $msys -lc 'pacman -Syu --noconfirm' | Out-Host
& $msys -lc 'pacman -Syu --noconfirm' | Out-Host   # MSYS2 pide dos pasadas tras actualizarse
& $msys -lc 'pacman -S --needed --noconfirm mingw-w64-ucrt-x86_64-toolchain mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja mingw-w64-ucrt-x86_64-pkgconf mingw-w64-ucrt-x86_64-rust mingw-w64-ucrt-x86_64-python unzip' | Out-Host

Write-Host '[3/4] Compilando Dussek NA_Magicos...' -ForegroundColor Cyan
$u = '/' + $env:USERPROFILE.Substring(0, 1).ToLower() + ($env:USERPROFILE.Substring(2) -replace '\\', '/')   # C:\Users\X -> /c/Users/X
& $msys -lc "cd '$u/Dussek/fuente' && rm -rf dussek && unzip -q dussek-na-magicos-fuente.zip && cd dussek && cmake -S plugin -B plugin/build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build plugin/build" | Out-Host
$exe = "$base\fuente\dussek\plugin\build\bin\dussek.exe"
if (-not (Test-Path $exe)) { throw 'La compilacion no produjo dussek.exe (mira los mensajes de arriba).' }

Write-Host '[4/4] Armando la carpeta dussek-na-magicos...' -ForegroundColor Cyan
New-Item -ItemType Directory -Force $salida | Out-Null
Copy-Item "$base\fuente\dussek\plugin\build\bin\*" $salida -Recurse -Force
Copy-Item "$base\dussek-0.4.1-windows\instruments" $salida -Recurse -Force
$w = New-Object -ComObject WScript.Shell
$l = $w.CreateShortcut("$env:USERPROFILE\Desktop\Dussek NA_Magicos.lnk")
$l.TargetPath = "$salida\dussek.exe"; $l.WorkingDirectory = $salida; $l.Save()
if (-not (Get-ChildItem 'HKLM:\SOFTWARE\ASIO', 'HKLM:\SOFTWARE\WOW6432Node\ASIO' -ErrorAction SilentlyContinue)) {
    Write-Host 'No tienes drivers ASIO: instalando ASIO4ALL y FlexASIO...' -ForegroundColor Cyan
    & "$base\instalar-asio.ps1"
}
Write-Host "Listo: $salida\dussek.exe (acceso directo 'Dussek NA_Magicos' en el escritorio)" -ForegroundColor Green
Write-Host "Para los videos: .\render-namagicos.ps1 -Midi pieza.mid -DussekDir `"$salida`"" -ForegroundColor Green
