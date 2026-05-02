#!/bin/bash
# =============================================================================
# benchmark.sh — Análise de Desempenho (Ponto C do enunciado)
#
# Corre jobshop_seq e jobshop_par para a instância inst_20x20.jss
# com N repetições e gera benchmark_results.csv.
#
# Uso: ./benchmark.sh [repeticoes]
#   repeticoes — numero de repeticoes para media (default: 10)
#
# Formato CSV gerado:
#   modo,threads,reps,makespan_ub,makespan_bb,tempo_medio_s
#   SEQ — formato: SEQ,reps,makespan,tempo_medio
#   PAR — formato: PAR,threads,reps,makespan_ub,makespan_bb,tempo_medio
# =============================================================================

REPS=${1:-10}
INSTANCIA="inst_20x20.jss"
CSV="benchmark_results.csv"
export BB_DEPTH=3

# ── Verificações ─────────────────────────────────────────────────────────────
if [ ! -f "./jobshop_seq" ] || [ ! -f "./jobshop_par" ]; then
    echo "ERRO: compile primeiro:"
    echo "  gcc -O2 -Wall -o jobshop_seq jobshop_seq.c"
    echo "  gcc -O2 -fopenmp -Wall -o jobshop_par jobshop_par.c"
    exit 1
fi
if [ ! -f "$INSTANCIA" ]; then
    echo "ERRO: $INSTANCIA nao encontrado."
    exit 1
fi

echo "============================================================"
echo " Job-Shop — Benchmark de Desempenho"
echo " Instância : $INSTANCIA (20 jobs x 20 maquinas)"
echo " BB_DEPTH  : $BB_DEPTH"
echo " Repeticoes: $REPS"
echo " CPUs      : $(nproc)"
echo "============================================================"

# Cabeçalho CSV
echo "modo,threads,reps,makespan_ub,makespan_bb,tempo_medio_s" > "$CSV"

# ── Sequencial ───────────────────────────────────────────────────────────────
echo ""
echo "[1/8] Sequencial ($REPS reps)..."
RAW=$(./jobshop_seq "$INSTANCIA" result_seq.txt "$REPS" 2>/dev/null)
# RAW: SEQ,reps,makespan,tempo_medio
REPS_V=$(echo "$RAW" | cut -d',' -f2)
MK=$(echo "$RAW"    | cut -d',' -f3)
TM=$(echo "$RAW"    | cut -d',' -f4)
echo "  Makespan: $MK | Tempo medio: ${TM}s"
echo "SEQ,-,$REPS_V,-,$MK,$TM" >> "$CSV"

# ── Paralelo — várias configurações ──────────────────────────────────────────
N=2
for T in 1 2 4 8 16 32; do
    echo "[$N/8] Paralelo $T thread(s) ($REPS reps)..."
    RAW=$(./jobshop_par "$INSTANCIA" result_par.txt "$T" "$REPS" 2>/dev/null)
    # RAW: PAR,threads,reps,makespan_ub,makespan_bb,tempo_medio
    echo "  $RAW"
    echo "$RAW" >> "$CSV"
    N=$((N+1))
done

# ── Tabela de resultados ──────────────────────────────────────────────────────
echo ""
echo "============================================================"
echo " Tabela de Desempenho"
echo "============================================================"
printf " %-8s %-8s %-14s %-14s %-10s\n" "Config" "Threads" "Makespan" "T_medio(s)" "Speedup"
echo " ----------------------------------------------------------"

# T1 = tempo do paralelo com 1 thread (referencia para speedup)
T1=$(grep "^PAR,1," "$CSV" | cut -d',' -f6)

while IFS=',' read -r modo threads reps mk_ub mk_bb tmedio; do
    [ "$modo" = "modo" ] && continue   # skip header
    if [ "$modo" = "SEQ" ]; then
        printf " %-8s %-8s %-14s %-14s %-10s\n" "SEQ" "-" "$mk_bb" "$tmedio" "(baseline)"
    else
        if [ -n "$T1" ] && [ "$T1" != "0.000000" ]; then
            SP=$(awk "BEGIN{printf \"%.3f\", $T1/$tmedio}")
        else
            SP="N/A"
        fi
        printf " %-8s %-8s %-14s %-14s %-10s\n" "PC${threads}" "$threads" "$mk_bb" "$tmedio" "$SP"
    fi
done < "$CSV"

echo " ----------------------------------------------------------"
echo " Speedup S = T(PC1) / T(PCp)"
echo " Maquina com $(nproc) CPU(s) logico(s)"
echo ""
echo " CSV guardado em: $CSV"
echo " Graficos: python3 graficos.py"
echo "============================================================"
