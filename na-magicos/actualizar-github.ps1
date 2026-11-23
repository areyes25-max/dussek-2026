# actualizar-github.ps1 - sube a tu GitHub (areyes25-max/dussek-2026) los cambios nuevos de NA_Magicos
# que llegan en un archivo .bundle, y GitHub Actions compila solo el nuevo dussek.exe.
# Uso:  .\actualizar-github.ps1 -Bundle .\fuente\na-magicos-2.bundle
param([Parameter(Mandatory = $true)][string]$Bundle)
$repo = "$env:USERPROFILE\Dussek\fuente\dussek-github"
$Bundle = (Resolve-Path $Bundle).Path
Push-Location $repo
try {
    git fetch $Bundle na-magicos:na-magicos-nuevo
    if ($LASTEXITCODE -ne 0) { throw 'No pude leer el bundle' }
    git merge --ff-only na-magicos-nuevo
    if ($LASTEXITCODE -ne 0) { throw 'El merge no fue directo (alguien cambio main en GitHub): avisame' }
    git branch -D na-magicos-nuevo | Out-Null
    git push origin main
    if ($LASTEXITCODE -eq 0) { Write-Host 'Subido. Mira Actions > Platforms: arranca solo; al terminar descarga dussek-windows.' -ForegroundColor Green }
} finally { Pop-Location }   # vuelves a la carpeta donde estabas
