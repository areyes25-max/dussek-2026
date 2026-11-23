# instalar-version-nueva.ps1 - instala el dussek-windows.zip que descargas de GitHub Actions
# en C:\Users\<tu>\Dussek\dussek-na-magicos (conserva los instrumentos y tus ajustes).
$base = "$env:USERPROFILE\Dussek"
$desc = (New-Object -ComObject Shell.Application).Namespace('shell:Downloads').Self.Path
$zip = Get-ChildItem $desc -Filter 'dussek-windows*.zip' -ErrorAction SilentlyContinue | Sort-Object LastWriteTime -Descending | Select-Object -First 1
if (-not $zip) { Write-Host "No hay dussek-windows.zip en $desc" -ForegroundColor Yellow; return }
Stop-Process -Name dussek -ErrorAction SilentlyContinue; Start-Sleep 1
$tmp = Join-Path $env:TEMP 'dussek-nuevo'; Remove-Item $tmp -Recurse -Force -ErrorAction SilentlyContinue
Expand-Archive $zip.FullName -DestinationPath $tmp -Force
$src = (Get-ChildItem $tmp -Recurse -Filter dussek.exe | Select-Object -First 1).DirectoryName
Get-ChildItem $src | Where-Object Name -ne 'instruments' | Copy-Item -Destination "$base\dussek-na-magicos" -Recurse -Force
# los presets NA_Magicos ya vienen dentro del programa: quita las copias viejas de tu carpeta para no verlos repetidos
Remove-Item "$env:APPDATA\Dussek\presets\na-magicos-*.dussek-preset" -ErrorAction SilentlyContinue
# acceso directo "NA-Magico" en el Escritorio (el viejo "Dussek NA_Magicos" se quita)
$esc = [Environment]::GetFolderPath('Desktop')
Remove-Item "$esc\Dussek*.lnk" -ErrorAction SilentlyContinue
$ws = New-Object -ComObject WScript.Shell; $lnk = $ws.CreateShortcut("$esc\NA-Magico.lnk")
$lnk.TargetPath = "$base\dussek-na-magicos\dussek.exe"; $lnk.WorkingDirectory = "$base\dussek-na-magicos"; $lnk.Save()
Write-Host "Instalado desde $($zip.Name) ($($zip.LastWriteTime))" -ForegroundColor Green
Start-Process "$base\dussek-na-magicos\dussek.exe" -WorkingDirectory "$base\dussek-na-magicos"
