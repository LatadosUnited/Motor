# Compara o Motor com a Unity nas 7 cenas do conjunto (bench/README.md), alternando um e outro em cada cena.
# Pré-requisitos: build Release do Motor (build-release), projeto gerado por bench/gerar_motor.py com os scripts
# compilados, e o player da Unity em bench/unity/Build (BenchBuild.MontarECompilar).
param(
    [Parameter(Mandatory = $true)][string]$Projeto,          # pasta "Motor Bench" gerada
    [string]$Saida = "$PSScriptRoot\resultados.csv",
    [string]$Player = "$PSScriptRoot\..\build-release\bin\Player.exe",
    [string]$Unity = "$PSScriptRoot\unity\Build\MotorBench.exe"
)
$env:PATH = "C:\msys64\ucrt64\bin;" + $env:PATH
$csvUnity = Join-Path (Split-Path $Unity) "resultados.csv"
Remove-Item $csvUnity -ErrorAction SilentlyContinue
$mproj = Get-ChildItem $Projeto -Filter *.mproject | Select-Object -First 1
$linhas = @()
foreach ($cena in "vazia", "cubos1000", "cubos1000sombra", "cubos10000", "fisica1000", "scripts1000", "spawn200") {
    $j = Get-Content $mproj.FullName -Raw -Encoding UTF8 | ConvertFrom-Json
    $j.startupScene = "Assets/Scenes/$cena.mscene"
    [IO.File]::WriteAllText($mproj.FullName, ($j | ConvertTo-Json), (New-Object Text.UTF8Encoding $false))
    $m = & "$PSScriptRoot\medir.ps1" -Exe $Player -Projeto $Projeto -Rotulo "Motor $cena"
    Start-Process $Unity -ArgumentList "-cena", $cena, "-screen-width", "1280", "-screen-height", "720", "-screen-fullscreen", "0" -Wait | Out-Null
    $u = Import-Csv $csvUnity | Where-Object { $_.cena -eq $cena } | Select-Object -Last 1
    $msM = if ($m -match '([\d,\.]+) ms') { [double]($matches[1] -replace ',', '.') } else { [double]::NaN }
    $msU = [double]$u.ms
    $pct = [math]::Round($msU / $msM * 100)
    $linhas += [pscustomobject]@{ cena = $cena; motor_ms = $msM; unity_ms = $msU; motor_pct_unity = $pct }
    "{0,-16} Motor {1,7:N2} ms   Unity {2,7:N2} ms   Motor = {3}% da Unity" -f $cena, $msM, $msU, $pct
}
$linhas | Export-Csv $Saida -NoTypeInformation -Encoding UTF8
