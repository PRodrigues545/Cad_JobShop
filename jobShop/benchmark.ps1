# =============================================================================
# benchmark.ps1 - Recolha de dados de desempenho (Job-Shop Scheduling)
#
# Uso: .\benchmark.ps1 [repeticoes]
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
Write-Host " Instancia  : $INSTANCIA"
Write-Host " Repeticoes : $Reps"
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
    Write-Host "[$i/6] Paralelo $T thread(s)..." -ForegroundColor Yellow
    $raw = .\jobshop_par.exe $INSTANCIA result_par.txt $T $Reps 2>$null
    Write-Host "  $raw"
    Add-Content -Path $CSV -Value $raw
    $i++
}

Write-Host ""
Write-Host "============================================================" -ForegroundColor Cyan
Write-Host " Dados guardados em: $CSV"
Write-Host " Para gerar os graficos corre:" -ForegroundColor Green
Write-Host "   python graficos.py" -ForegroundColor Green
Write-Host "============================================================" -ForegroundColor Cyan