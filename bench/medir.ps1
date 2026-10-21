# Mede o tempo de quadro do Player do Motor pelos contadores (--perf, Fase 22): roda $Segundos segundos (--segundos,
# o Player fecha sozinho e grava o CSV), descarta os 4 primeiros e devolve a mediana das médias de 1 s, como o
# BenchRunner.cs do projeto gêmeo da Unity. Vsync desligado (MOTOR_VSYNC=0). Não depende do título da janela.
# -Csv guarda o CSV completo (CPU por parte, GPU por passada, memória de vídeo) num arquivo escolhido.
param([string]$Exe, [string]$Projeto, [string]$Rotulo, [string[]]$Extra = @(), [int]$Segundos = 12, [string]$Csv = "")
$env:MOTOR_VSYNC = "0"
$arquivo = if ($Csv) { $Csv } else { Join-Path $env:TEMP ("motor-medir-{0}.csv" -f [guid]::NewGuid()) }
Remove-Item $arquivo -ErrorAction SilentlyContinue
$args = @("`"$Projeto`"") + $Extra + @("--perf", "`"$arquivo`"", "--segundos", "$Segundos")
$p = Start-Process -FilePath $Exe -ArgumentList $args -PassThru
if (-not $p.WaitForExit(($Segundos + 60) * 1000)) {  # carregar a cena entra no tempo: folga de 1 minuto
    Stop-Process -Id $p.Id -ErrorAction SilentlyContinue
    "{0,-34} sem leitura (o Player não fechou sozinho)" -f $Rotulo; return
}
if (-not (Test-Path $arquivo)) { "{0,-34} sem leitura (sem o CSV do --perf)" -f $Rotulo; return }

# Médias de 1 s (quadro_ms somado / quadros), do segundo 4 em diante.
$porSegundo = @{}
$draws = @()
foreach ($q in Import-Csv $arquivo) {
    $t = [double]$q.tempo_s
    if ($t -lt 4) { continue }
    $s = [int][math]::Floor($t)
    if (-not $porSegundo.ContainsKey($s)) { $porSegundo[$s] = @(0.0, 0) }
    $porSegundo[$s] = @(($porSegundo[$s][0] + [double]$q.quadro_ms), ($porSegundo[$s][1] + 1))
    if ($q.draws -ne '') { $draws += [double]$q.draws }
}
if (-not $Csv) { Remove-Item $arquivo -ErrorAction SilentlyContinue }
$medias = $porSegundo.Values | Where-Object { $_[1] -gt 0 } | ForEach-Object { $_[0] / $_[1] } | Sort-Object
if ($medias.Count -eq 0) { "{0,-34} sem leitura (nenhum quadro depois de 4 s)" -f $Rotulo; return }
$ms = $medias[[int][math]::Floor($medias.Count / 2)]
$d = if ($draws.Count) { ($draws | Sort-Object)[[int][math]::Floor($draws.Count / 2)] } else { 0 }
"{0,-34} {1,7:N1} FPS {2,8:N2} ms  {3,6} draws  ({4} amostras)" -f $Rotulo, (1000.0 / $ms), $ms, $d, $medias.Count
