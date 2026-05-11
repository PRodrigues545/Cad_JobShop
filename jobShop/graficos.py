import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

# Dados lidos directamente do CSV (sem cabecalho)
threads, tempos, labels = [], [], []
t1 = None

with open("benchmark_results.csv") as f:
    for line in f:
        line = line.strip()
        if not line or line.startswith("modo"): continue
        p = line.split(",")
        if p[0] == "PAR":
            t  = int(p[1])
            tm = float(p[5])
            if t == 1: t1 = tm
            threads.append(t)
            tempos.append(tm)
            labels.append(f"PC{t}")

if not t1: t1 = tempos[0]
speedups = [round(t1/t, 3) for t in tempos]
ideal    = list(threads)

print("Dados carregados:")
for t,tm,sp in zip(threads,tempos,speedups):
    print(f"  PC{t}: {tm:.3f}s  speedup={sp}")

plt.rcParams.update({
    "figure.dpi":150,"font.size":11,"axes.titlesize":13,
    "axes.titleweight":"bold","axes.grid":True,
    "grid.linestyle":"--","grid.alpha":0.5,
    "lines.linewidth":2.2,"lines.markersize":8
})

# Grafico 1: Tempo
fig,ax = plt.subplots(figsize=(8,5))
ax.plot(threads,tempos,color="#2E86AB",marker="o",label="Tempo medido")
ax.axhline(y=t1,color="#A23B72",linestyle=":",linewidth=1.5,label=f"T1 = {t1:.3f} s")
for t,tm in zip(threads,tempos):
    ax.annotate(f"{tm:.3f}s",xy=(t,tm),xytext=(0,10),
                textcoords="offset points",ha="center",
                fontsize=9,color="#2E86AB",fontweight="bold")
ax.set_xlabel("Numero de Threads (p)")
ax.set_ylabel("Tempo medio de execucao (s)")
ax.set_title("Tempo de Execucao vs Numero de Threads\n(inst_20x20.jss, BB_DEPTH=3, media de 10 execucoes)")
ax.set_xticks(threads); ax.set_xticklabels(labels)
ax.set_ylim(bottom=0); ax.legend()
fig.tight_layout(); fig.savefig("grafico_tempo.png")
print("grafico_tempo.png guardado")

# Grafico 2: Speedup
fig,ax = plt.subplots(figsize=(8,5))
ax.plot(threads,ideal,color="#cccccc",linestyle="--",linewidth=1.5,label="Speedup ideal (linear)")
ax.plot(threads,speedups,color="#F18F01",marker="s",label="Speedup medido")
ax.axhline(y=1.0,color="#aaaaaa",linestyle=":",linewidth=1)
for t,sp in zip(threads,speedups):
    ax.annotate(f"{sp:.2f}x",xy=(t,sp),xytext=(0,10),
                textcoords="offset points",ha="center",
                fontsize=9,color="#F18F01",fontweight="bold")
ax.set_xlabel("Numero de Threads (p)")
ax.set_ylabel("Speedup  S = T1 / Tp")
ax.set_title("Speedup vs Numero de Threads\n(inst_20x20.jss, BB_DEPTH=3, media de 10 execucoes)")
ax.set_xticks(threads); ax.set_xticklabels(labels)
ax.set_ylim(bottom=0); ax.legend()
fig.tight_layout(); fig.savefig("grafico_speedup.png")
print("grafico_speedup.png guardado")
print("Pronto!")