# render-namagicos.ps1 - MIDI -> WAV (-14 LUFS) + video sincronizado con Dussek
# Uso:
#   .\render-namagicos.ps1 -Midi .\pieza.mid
#   .\render-namagicos.ps1 -Midi .\pieza.mid -Preset Lofi -Video Ninguno
#   .\render-namagicos.ps1 -Midi .\pieza.mid -Video Short -Personaje .\kling_clip.mp4
#   .\render-namagicos.ps1 -Midi .\pieza.mid -Limitar            (llega a -14 LUFS con limitador suave)
#   .\render-namagicos.ps1 -Midi .\pieza.mid -Fps 60              (mas fluido, tarda el doble)
#   .\render-namagicos.ps1 -Midi .\pieza.mid -Pad -Limitar       (agrega pad bajo el piano; -PadDb -9 lo sube)
#   .\render-namagicos.ps1 -Midi .\pieza.mid -Tema Clasico        (visual original; Deco requiere la version de tu fork)
#   .\render-namagicos.ps1 -Midi .\pieza.mid -SinFondo            (sin el arte de portada: cuerdas animadas)
param(
    [Parameter(Mandatory = $true)][string]$Midi,
    [ValidateSet('Nocturno', 'Lofi')][string]$Preset = 'Nocturno',
    [ValidateSet('Ninguno', 'Horizontal', 'Short', 'Transparente')][string]$Video = 'Horizontal',
    [string]$Personaje = '',
    [string]$DussekDir = "$env:USERPROFILE\Dussek\dussek-0.4.1-windows",
    [double]$Lufs = -14,
    [int]$Fps = 30,
    [switch]$Limitar,
    [switch]$Pad,
    [double]$PadDb = -11,
    [ValidateSet('Deco', 'Clasico')][string]$Tema = 'Deco',
    [string]$Fondo = "$PSScriptRoot\na-magicos\arte_portada_hd.png",
    [switch]$SinFondo
)

[Console]::OutputEncoding = [Text.Encoding]::UTF8
$inv = [Globalization.CultureInfo]::InvariantCulture
function Fail($msg) { Write-Host "ERROR: $msg" -ForegroundColor Red; exit 1 }

$exe = Join-Path $DussekDir 'dussek.exe'
if (-not (Test-Path $exe)) { Fail "No encuentro $exe. Indica la carpeta con -DussekDir" }
if (-not (Get-Command ffmpeg -ErrorAction SilentlyContinue)) { Fail "Falta ffmpeg. Instala con: winget install Gyan.FFmpeg  (y abre otra ventana)" }
if (-not (Test-Path $Midi)) { Fail "No existe el MIDI: $Midi" }
$Midi = (Resolve-Path $Midi).Path
if ($Personaje -and -not (Test-Path $Personaje)) { Fail "No existe el video del personaje: $Personaje" }
if ($Personaje -and $Video -ne 'Short') { Fail "-Personaje solo se usa con -Video Short" }

$dir  = Split-Path $Midi -Parent
$name = [IO.Path]::GetFileNameWithoutExtension($Midi)
$base = Join-Path $dir "$name`_$Preset"

# Mismos valores que los presets .dussek-preset, en las unidades de la linea de comandos
# (dB del plugin -> factor lineal: 10^(dB/20)). voicing=0.05 = Condition 10 % "Normal" del plugin.
$curve = '1:10,40:46,96:104,127:122'
switch ($Preset) {
    'Nocturno' { $inst = 'grand_b';   $param = 'dynamics=32,hardness=0.631,sympathetic=1.2,release=0.158,voicing=0.05'; $tuning = 'mic=2,sustain=1.15' }
    'Lofi'     { $inst = 'upright_c'; $param = 'dynamics=16,hardness=0.126,room=0.398,knock=0.708,release=0.398,voicing=0.05'; $tuning = 'mic=1' }
}
$instDir = Join-Path $DussekDir "instruments\$inst"
if (-not (Test-Path $instDir)) { Fail "No encuentro el instrumento $instDir" }

# 1) Render con Dussek (audio + video del motor, sincronizados al sample)
$raw = "$base.raw.wav"
$a = @('render', $instDir, $Midi, $raw, '--param', $param, '--tuning', $tuning, '--velocity-curve', $curve, '--tail', '4', '--bits', '24')
$rawVideo = $null
switch ($Video) {
    'Horizontal'   { $rawVideo = "$base.raw.mp4"; $a += @('--video', $rawVideo, '--size', '1920x1080', '--fps', "$Fps") }
    'Short'        { $rawVideo = "$base.raw.mp4"; $a += @('--video', $rawVideo, '--size', '1080x960',  '--fps', "$Fps") }
    'Transparente' { $a += @('--video', "$base.mov", '--size', '1920x1080', '--fps', "$Fps", '--transparent') }
}
$ayuda = & $exe render --help 2>&1 | Out-String
if ($Video -ne 'Ninguno' -and $Tema -eq 'Deco') {
    # El tema Art deco existe solo en la version NA_Magicos (tu fork): si este dussek.exe no lo conoce, usa el clasico
    if ($ayuda -match '--theme') { $a += @('--theme', 'deco') }
    else { Write-Host "      Aviso: este dussek.exe no tiene el tema Deco (usa el de tu fork). Sigo con el visual clasico." -ForegroundColor Yellow }
}
# Arte de portada de fondo (solo video horizontal): la imagen tal cual, con el teclado sincronizado abajo
$usarFondo = ($Video -eq 'Horizontal') -and (-not $SinFondo) -and (Test-Path $Fondo)
$fondoNativo = $usarFondo -and ($ayuda -match '--background')
if ($fondoNativo) { $a += @('--background', (Resolve-Path $Fondo).Path) }
Write-Host "[1/3] Renderizando $name con '$Preset' ($inst)..." -ForegroundColor Cyan
& $exe @a 2>&1 | ForEach-Object { Write-Host "      $_" }
if ($LASTEXITCODE -ne 0 -or -not (Test-Path $raw)) { Fail "Dussek no pudo renderizar (codigo $LASTEXITCODE)" }

if ($Pad) {
    # Pad generado desde los acordes del mismo MIDI, mezclado debajo del piano (misma linea de tiempo)
    $padPy = Join-Path $PSScriptRoot 'pad_from_midi.py'
    if (-not (Test-Path $padPy)) { Fail "No encuentro $padPy" }
    if (-not (Get-Command py -ErrorAction SilentlyContinue)) { Fail "Falta Python. Instala con: winget install Python.Python.3.12" }
    $padLvl = $PadDb.ToString($inv)
    Write-Host "[+]   Generando pad ($PadDb dB bajo el piano)..." -ForegroundColor Cyan
    & py -3 $padPy $Midi "$base.pad.wav" --mezclar-con $raw --salida-mezcla $raw "--nivel=$padLvl" 2>&1 | ForEach-Object { Write-Host "      $_" }
    if ($LASTEXITCODE -ne 0) { Fail "No se pudo generar el pad (revisa: py -3 -m pip install numpy scipy mido soundfile)" }
}

# 2) Volumen para YouTube: medir, ajustar a $Lufs LUFS (con -Limitar, limitador suave)
Write-Host "[2/3] Ajustando volumen a $Lufs LUFS..." -ForegroundColor Cyan
$m = & ffmpeg -hide_banner -nostats -i $raw -af "loudnorm=I=$($Lufs.ToString($inv)):TP=-1:print_format=json" -f null - 2>&1 | Out-String
$json = [regex]::Match($m, '\{[^{}]*"input_i"[^{}]*\}').Value
if (-not $json) { Fail "No pude medir el volumen con ffmpeg" }
$j = $json | ConvertFrom-Json
$inI  = [double]::Parse($j.input_i, $inv)
$inTP = [double]::Parse($j.input_tp, $inv)
$wav = "$base.wav"
if ($Limitar) {
    # Ganancia completa + limitador suave (techo -1.5 dBFS, con compensacion de retardo: la sincronia no cambia)
    $gain = [Math]::Round($Lufs - $inI, 2)
    $af = "volume=$($gain.ToString($inv))dB,alimiter=limit=0.8414:attack=5:release=80:level=disabled:latency=1"
} else {
    # Solo ganancia lineal: nunca supera -1 dBTP (si el pico manda, queda por debajo del objetivo)
    $gain = [Math]::Round([Math]::Min($Lufs - $inI, -1 - $inTP), 2)
    $af = "volume=$($gain.ToString($inv))dB"
}
& ffmpeg -y -hide_banner -loglevel error -i $raw -af $af -c:a pcm_s24le $wav
if ($LASTEXITCODE -ne 0) { Fail "ffmpeg no pudo escribir $wav" }
$chk = & ffmpeg -hide_banner -nostats -i $wav -af ebur128 -f null - 2>&1 | Out-String
$fin = [regex]::Matches($chk, 'I:\s+(-?[\d.]+) LUFS')
$final = if ($fin.Count) { $fin[$fin.Count - 1].Groups[1].Value } else { '?' }
Write-Host "      ganancia $($gain.ToString($inv)) dB -> $final LUFS"
if (-not $Limitar -and ($Lufs - $inI) -gt (-1 - $inTP) + 0.5) { Write-Host "      Aviso: el pico limito el volumen. Para llegar a $Lufs LUFS usa -Limitar." -ForegroundColor Yellow }

# 3) Video final con el audio ajustado
Write-Host "[3/3] Armando el video..." -ForegroundColor Cyan
switch ($Video) {
    'Horizontal' {
        if ($usarFondo -and -not $fondoNativo) {
            # dussek.exe sin --background (0.4.1): el arte arriba y la franja de teclado del render abajo, con ffmpeg
            # el teclado mas arriba (termina 130 px sobre el borde): queda libre de la barra del reproductor
            $fc = "color=c=0x040302:s=1920x1080:r=$($Fps)[base];[1:v]scale=1920:1080[img];[base][img]overlay=0:-90,drawbox=x=0:y=833:w=1920:h=247:color=0x040302@1:t=fill[bg];[0:v]crop=1840:120:40:910,scale=1840:105[k];[bg][k]overlay=40:845:shortest=1[v]"
            & ffmpeg -y -hide_banner -loglevel error -i $rawVideo -loop 1 -i $Fondo -i $wav -filter_complex $fc -map '[v]' -map 2:a:0 -c:v libx264 -preset medium -crf 18 -pix_fmt yuv420p -c:a aac -b:a 320k -shortest -movflags +faststart "$base.mp4"
        } else {
            & ffmpeg -y -hide_banner -loglevel error -i $rawVideo -i $wav -map 0:v:0 -map 1:a:0 -c:v copy -c:a aac -b:a 320k -shortest "$base.mp4"
        }
    }
    'Short' {
        if ($Personaje) {
            # "Dos mundos": personaje arriba (1080x960, recortado al centro, en bucle) + piano abajo = 1080x1920
            $fc = "[0:v]scale=1080:960:force_original_aspect_ratio=increase,crop=1080:960,fps=$Fps,setsar=1[top];[1:v]setsar=1[bot];[top][bot]vstack=inputs=2[v]"
            & ffmpeg -y -hide_banner -loglevel error -stream_loop -1 -i $Personaje -i $rawVideo -i $wav -filter_complex $fc -map '[v]' -map 2:a:0 -c:v libx264 -preset medium -crf 18 -pix_fmt yuv420p -c:a aac -b:a 320k -shortest -movflags +faststart "$base`_short.mp4"
        } else {
            & ffmpeg -y -hide_banner -loglevel error -i $rawVideo -i $wav -map 0:v:0 -map 1:a:0 -c:v copy -c:a aac -b:a 320k -shortest "$base`_piano_mitad.mp4"
        }
    }
    'Transparente' { Write-Host "      $base.mov (ProRes 4444 con alfa) listo para tu editor; usa $wav como audio." }
}
if ($LASTEXITCODE -ne 0) { Fail "ffmpeg fallo al armar el video" }

Remove-Item $raw -ErrorAction SilentlyContinue
if ($rawVideo) { Remove-Item $rawVideo -ErrorAction SilentlyContinue }
Write-Host "Listo:" -ForegroundColor Green
Get-ChildItem "$base*" | Select-Object Name, @{n = 'MB'; e = { [Math]::Round($_.Length / 1MB, 1) } } | Format-Table -AutoSize
