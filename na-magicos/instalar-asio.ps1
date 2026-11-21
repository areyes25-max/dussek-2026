# instalar-asio.ps1 - instala los drivers ASIO gratuitos para tocar sin retraso:
#   ASIO4ALL (Michael Tippach, freeware)  y  FlexASIO (Etienne Dechamps, licencia MIT)
# Se descargan de sus fuentes oficiales con winget (no van empaquetados dentro de Dussek).
# Uso (PowerShell normal; Windows pedira permiso de administrador para instalar cada driver):
#   Set-ExecutionPolicy -Scope Process Bypass
#   .\instalar-asio.ps1
$ErrorActionPreference = 'Continue'

foreach ($id in 'MichaelTippach.ASIO4ALL', 'dechamps.FlexASIO') {
    Write-Host "Instalando $id..." -ForegroundColor Cyan
    winget install --id $id -e --accept-source-agreements --accept-package-agreements
}

# FlexASIO lee su configuracion de %USERPROFILE%\FlexASIO.toml: WASAPI compartido (no bloquea el sonido de
# otros programas), buffer de 128 muestras y sin entrada (para el piano solo hace falta la salida).
$toml = "$env:USERPROFILE\FlexASIO.toml"
if (-not (Test-Path $toml)) {
    [IO.File]::WriteAllText($toml, @'
# FlexASIO para Dussek / NA_Magicos
backend = "Windows WASAPI"
bufferSizeSamples = 128

[input]
device = ""

[output]
suggestedLatencySeconds = 0.0
wasapiExclusiveMode = false
'@)
    Write-Host "Configuracion de FlexASIO: $toml" -ForegroundColor Green
} else {
    Write-Host "Ya tienes $toml; no lo toco." -ForegroundColor Yellow
}

# Comprobar que Windows registro los drivers ASIO
$asio = Get-ChildItem 'HKLM:\SOFTWARE\ASIO', 'HKLM:\SOFTWARE\WOW6432Node\ASIO' -ErrorAction SilentlyContinue | Select-Object -ExpandProperty PSChildName -Unique
if ($asio) { Write-Host "Drivers ASIO instalados: $($asio -join ', ')" -ForegroundColor Green }
else { Write-Host 'No veo drivers ASIO registrados; reinicia Windows y vuelve a revisar.' -ForegroundColor Yellow }
Write-Host @'

En Dussek NA_Magicos: pestana MIDI -> Audio engine: ASIO -> Audio device: ASIO4ALL v2 (o FlexASIO) -> Buffer 128.
En Reaper y Pianoteq: Audio -> Driver ASIO -> el mismo dispositivo.
Para menos retraso con FlexASIO, cambia en FlexASIO.toml: wasapiExclusiveMode = true
(Dussek tendra el audio en exclusiva: los demas programas quedan sin sonido mientras toca).
'@
