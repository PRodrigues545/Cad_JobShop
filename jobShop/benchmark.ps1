# =============================================================================
# benchmark.ps1 - Recolha de dados de desempenho (Job-Shop Scheduling)
#
# Instancia : inst_20x20.jss (20 jobs x 20 maquinas)
# BB_DEPTH  : 4  (~13s por execucao)
# Repeticoes: 5  (total ~65s > 1 minuto para PC1)
#
# Uso: .\benchmark.ps1
# Depois de correr: python graficos.py
# =============================================================================

param([int]$Reps = 5)

$env:BB_DEPTH = "4"
$INSTANCIA    = "inst_20x20.jss"
$CSV          = "benchmark_results.csv"

# Verificacoes
if (-not (Test-Path ".\jobshop_seq.exe") -or -not (Test-Path ".\jobshop_par.exe")) {
    Write-Host "ERRO: compile primeiro:" -ForegroundColor Red
    Write-Host "  gcc -O2 -Wall -o jobshop_seq.exe jobshop_seq.c"
    Write-Host "  gcc -O2 -fopenmp -Wall -o jobshop_par.exe jobshop_par.c"
    exit 1
}
if (-not (Test-Path $INSTANCIA)) {
    Write-Host "ERRO: $INSTANCIA nao encontrado." -ForegroundColor Red
    exit 1
}

Write-Host "============================================================" -ForegroundColor Cyan
Write-Host " Job-Shop - Benchmark de Desempenho"
Write-Host " Instancia  : $INSTANCIA (20 jobs x 20 maquinas)"
Write-Host " BB_DEPTH   : $env:BB_DEPTH (~13s por execucao)"
Write-Host " Repeticoes : $Reps (~$($Reps * 13)s total para PC1)"
Write-Host " CPUs       : $([System.Environment]::ProcessorCount)"
Write-Host "============================================================" -ForegroundColor Cyan

# Limpar CSV anterior
if (Test-Path $CSV) { Remove-Item $CSV }

# Sequencial
Write-Host ""
Write-Host "[1/6] Sequencial ($Reps reps)..." -ForegroundColor Yellow
$raw = .\jobshop_seq.exe $INSTANCIA result_seq.txt $Reps 2>$null
Write-Host "  $raw"
Add-Content -Path $CSV -Value $raw

# Paralelo
$configs = @(1, 2, 4, 8)
$i = 2
foreach ($T in $configs) {
    Write-Host "[$i/6] Paralelo $T thread(s) ($Reps reps)..." -ForegroundColor Yellow
    $raw = .\jobshop_par.exe $INSTANCIA result_par.txt $T $Reps 2>$null
    Write-Host "  $raw"
    Add-Content -Path $CSV -Value $raw
    $i++
}

# Mostrar tabela de speedup
Write-Host ""
Write-Host "============================================================" -ForegroundColor Cyan
Write-Host " Tabela de Resultados"
Write-Host "============================================================" -ForegroundColor Cyan
Write-Host ("{0,-8} {1,-10} {2,-12} {3,-12} {4,-10}" -f "Config","T_medio(s)","T_total(s)","Makespan","Speedup")
Write-Host ("-" * 54)

$t1 = $null
Get-Content $CSV | ForEach-Object {
    $p = $_ -split ","
    if ($p[0] -eq "SEQ") {
        Write-Host ("{0,-8} {1,-10} {2,-12} {3,-12} {4,-10}" -f `
            "SEQ", [math]::Round([double]$p[3],3), [math]::Round([double]$p[4],3), $p[2], "(baseline)")
    } elseif ($p[0] -eq "PAR") {
        if ($p[1] -eq "1") { $t1 = [double]$p[5] }
        $sp = if ($t1) { [math]::Round($t1/[double]$p[5],3) } else { "N/A" }
        Write-Host ("{0,-8} {1,-10} {2,-12} {3,-12} {4,-10}" -f `
            "PC$($p[1])", [math]::Round([double]$p[5],3), [math]::Round([double]$p[6],3), $p[4], "${sp}x")
    }
}

Write-Host ("-" * 54)
Write-Host " Speedup S = T(PC1) / T(PCp)"
Write-Host ""
Write-Host " Dados guardados em: $CSV" -ForegroundColor Green
Write-Host " Para gerar os graficos: python graficos.py" -ForegroundColor Green
Write-Host "============================================================" -ForegroundColor Cyan