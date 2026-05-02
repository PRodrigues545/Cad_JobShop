#!/usr/bin/env python3
"""
graficos.py — Gera os gráficos de desempenho (Ponto C do enunciado)

Lê benchmark_results.csv gerado por benchmark.sh e produz:
  1. grafico_tempo.png   — Tempo de execução vs Número de threads
  2. grafico_speedup.png — Speedup (S = T1/Tp) vs Número de threads

Uso:
  python3 graficos.py [benchmark_results.csv]

Formato CSV esperado (gerado por benchmark.sh):
  modo,threads,reps,makespan_ub,makespan_bb,tempo_medio_s
  SEQ,-,10,-,1747,5.207123
  PAR,1,10,20009,1747,5.207123
  PAR,2,10,20009,1747,2.913451
  ...
"""

import sys
import csv
import os

try:
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
except ImportError:
    print("ERRO: pip install matplotlib")
    sys.exit(1)

# ── Leitura do CSV ────────────────────────────────────────────────────────────
csv_file = sys.argv[1] if len(sys.argv) > 1 else "benchmark_results.csv"

if not os.path.exists(csv_file):
    print(f"ERRO: '{csv_file}' nao encontrado. Corra primeiro ./benchmark.sh")
    sys.exit(1)

threads_list  = []
tempos_list   = []
makespan_ub   = None
makespan_bb   = None
num_reps      = None
t1            = None   # tempo com 1 thread — referencia do speedup

with open(csv_file, newline="") as f:
    reader = csv.DictReader(f)
    for row in reader:
        modo = row["modo"].strip()

        if modo == "SEQ":
            makespan_bb = int(row["makespan_bb"].strip())
            num_reps    = int(row["reps"].strip())
            continue

        if modo == "PAR":
            t      = int(row["threads"].strip())
            tmedio = float(row["tempo_medio_s"].strip())
            mk_ub  = int(row["makespan_ub"].strip())
            mk_bb  = int(row["makespan_bb"].strip())

            if makespan_ub is None: makespan_ub = mk_ub
            if makespan_bb is None: makespan_bb = mk_bb
            num_reps = int(row["reps"].strip())

            if t == 1:
                t1 = tmedio   # referencia para o speedup

            threads_list.append(t)
            tempos_list.append(tmedio)

if not threads_list:
    print("ERRO: nenhuma linha PAR encontrada.")
    sys.exit(1)

if t1 is None:
    t1 = tempos_list[0]

# Speedup medido e ideal
speedup_list  = [round(t1 / t, 3) for t in tempos_list]
speedup_ideal = list(threads_list)

# ── Sumário no terminal ───────────────────────────────────────────────────────
print("=" * 58)
print(f" Instancia    : inst_20x20.jss")
print(f" Repeticoes   : {num_reps}")
print(f" Makespan UB  : {makespan_ub}  (pior caso)")
print(f" Makespan B&B : {makespan_bb}  (solucao encontrada)")
print(f" T1 (ref.)    : {t1:.4f} s")
print()
print(f" {'Config':<8} {'Threads':<8} {'T_medio(s)':<14} {'Speedup'}")
print(f" {'-'*44}")
for t, tm, sp in zip(threads_list, tempos_list, speedup_list):
    print(f" {'PC'+str(t):<8} {t:<8} {tm:<14.6f} {sp:.3f}")
print("=" * 58)

# ── Estilo comum ─────────────────────────────────────────────────────────────
plt.rcParams.update({
    "figure.dpi"     : 150,
    "font.size"      : 11,
    "axes.titlesize" : 13,
    "axes.titleweight": "bold",
    "axes.grid"      : True,
    "grid.linestyle" : "--",
    "grid.alpha"     : 0.5,
    "lines.linewidth": 2,
    "lines.markersize": 7,
})

labels = [f"PC{t}" for t in threads_list]

# ════════════════════════════════════════════════════════════════════════════
# GRÁFICO 1 — Tempo de execução vs Número de threads
# ════════════════════════════════════════════════════════════════════════════
fig1, ax1 = plt.subplots(figsize=(8, 5))

ax1.plot(threads_list, tempos_list, color="#2E86AB",
         marker="o", label="Tempo medido")

ax1.axhline(y=t1, color="#A23B72", linestyle=":",
            linewidth=1.5, label=f"T1 = {t1:.3f} s")

for t, tm in zip(threads_list, tempos_list):
    ax1.annotate(f"{tm:.2f}s", xy=(t, tm),
                 xytext=(0, 10), textcoords="offset points",
                 ha="center", fontsize=9, color="#2E86AB")

ax1.set_xlabel("Número de Threads (p)")
ax1.set_ylabel("Tempo médio de execução (s)")
ax1.set_title(
    f"Tempo de Execução vs Número de Threads\n"
    f"(inst_20x20.jss, BB_DEPTH=3, média de {num_reps} execuções)"
)
ax1.set_xticks(threads_list)
ax1.set_xticklabels(labels, rotation=30)
ax1.set_ylim(bottom=0)
ax1.legend()
fig1.tight_layout()
fig1.savefig("grafico_tempo.png")
print("\nGrafico 1 guardado: grafico_tempo.png")

# ════════════════════════════════════════════════════════════════════════════
# GRÁFICO 2 — Speedup vs Número de threads
# ════════════════════════════════════════════════════════════════════════════
fig2, ax2 = plt.subplots(figsize=(8, 5))

ax2.plot(threads_list, speedup_ideal, color="#cccccc",
         linestyle="--", linewidth=1.5, label="Speedup ideal (linear)")

ax2.plot(threads_list, speedup_list, color="#F18F01",
         marker="s", label="Speedup medido")

ax2.axhline(y=1.0, color="#aaaaaa", linestyle=":", linewidth=1)

for t, sp in zip(threads_list, speedup_list):
    ax2.annotate(f"{sp:.2f}", xy=(t, sp),
                 xytext=(0, 10), textcoords="offset points",
                 ha="center", fontsize=9, color="#F18F01")

ax2.set_xlabel("Número de Threads (p)")
ax2.set_ylabel("Speedup  S = T₁ / Tₚ")
ax2.set_title(
    f"Speedup vs Número de Threads\n"
    f"(inst_20x20.jss, BB_DEPTH=3, média de {num_reps} execuções)"
)
ax2.set_xticks(threads_list)
ax2.set_xticklabels(labels, rotation=30)
ax2.set_ylim(bottom=0)
ax2.legend()
fig2.tight_layout()
fig2.savefig("grafico_speedup.png")
print("Grafico 2 guardado: grafico_speedup.png")
print()
print("Inclui no relatorio: grafico_tempo.png e grafico_speedup.png")
