# Mede o tempo de quadro de um executável do Motor (Player ou editor) pelo título da janela, que o Motor atualiza
# a cada segundo com "N FPS (X ms)". Vsync desligado (MOTOR_VSYNC=0); descarta os 4 primeiros segundos e devolve
# a mediana das médias de 1 s, como o BenchRunner.cs do projeto gêmeo da Unity.
param([string]$Exe, [string]$Projeto, [string]$Rotulo, [string[]]$Extra = @(), [int]$Segundos = 12)
$env:MOTOR_VSYNC = "0"
$p = Start-Process -FilePath $Exe -ArgumentList (@("`"$Projeto`"") + $Extra) -PassThru
$amostras = @()
for ($i = 0; $i -lt $Segundos; $i++) {
    Start-Sleep -Seconds 1
    $p.Refresh()
    if ($i -ge 4 -and $p.MainWindowTitle -match '\|\s*([\d\.]+) FPS \(([\d\.]+) ms\)\s*\|\s*(\d+) draws') {
        $amostras += [pscustomobject]@{ fps = [double]$matches[1]; ms = [double]$matches[2]; draws = [int]$matches[3] }
    }
}
$ultimo = $p.MainWindowTitle
Stop-Process -Id $p.Id -ErrorAction SilentlyContinue
if ($amostras.Count -eq 0) { "{0,-34} sem leitura (título: {1})" -f $Rotulo, $ultimo; return }
$med = ($amostras | Sort-Object ms)[[int][math]::Floor($amostras.Count / 2)]
"{0,-34} {1,7:N1} FPS {2,8:N2} ms  {3,6} draws  ({4} amostras)" -f $Rotulo, $med.fps, $med.ms, $med.draws, $amostras.Count
