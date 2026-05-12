/*
 * Job-Shop Scheduling - Implementacao Sequencial
 * Algoritmo: Branch & Bound (1 thread)
 *
 * Compilacao:  gcc -O2 -Wall -o jobshop_seq jobshop_seq.c
 * Uso:         ./jobshop_seq <entrada> <saida> [repeticoes]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <time.h>

#define MAX_JOBS        50
#define MAX_MACHINES    50
#define MAX_OPS         50

/* ─── Estruturas de dados ────────────────────────────────────────────────── */

/* Problema: lido do ficheiro, nunca alterado */
typedef struct {
    int num_jobs;
    int num_machines;
    int num_ops[MAX_JOBS];
    int machine[MAX_JOBS][MAX_OPS];   /* maquina de cada operacao */
    int duration[MAX_JOBS][MAX_OPS];  /* duracao de cada operacao */
} Problem;

/*
 * Estado do B&B — representa um escalonamento parcial.
 * Copiado por valor na stack a cada chamada recursiva.
 * Sem apontadores internos (conforme enunciado).
 */
typedef struct {
    int machine_free[MAX_MACHINES]; /* proximo instante livre de cada maquina */
    int job_free[MAX_JOBS];         /* proximo instante livre de cada job     */
    int next_op[MAX_JOBS];          /* proxima operacao a escalonar por job   */
    int start[MAX_JOBS][MAX_OPS];   /* tempos de inicio atribuidos            */
    int makespan;                   /* maior tempo de conclusao ate agora     */
    int scheduled;                  /* numero de operacoes ja escalonadas     */
} State;

/* ─── Variaveis globais ──────────────────────────────────────────────────── */

static Problem prob;
static int     total_ops;

/* Melhor solucao encontrada ate agora */
static int best_makespan;
static int best_start[MAX_JOBS][MAX_OPS];

/* ─── Leitura do ficheiro ────────────────────────────────────────────────── */

static int read_problem(const char *file) {
    FILE *f = fopen(file, "r");
    if (!f) { fprintf(stderr, "Erro ao abrir %s\n", file); return 0; }

    fscanf(f, "%d %d", &prob.num_jobs, &prob.num_machines);

    total_ops = 0;
    for (int j = 0; j < prob.num_jobs; j++) {
        prob.num_ops[j] = prob.num_machines;
        for (int o = 0; o < prob.num_ops[j]; o++)
            fscanf(f, "%d %d", &prob.machine[j][o], &prob.duration[j][o]);
        total_ops += prob.num_ops[j];
    }
    fclose(f);
    return 1;
}

/* ─── Lower bound ────────────────────────────────────────────────────────── */

/*
 * Para cada job, o tempo minimo de conclusao e:
 *   job_free[j] + soma das duracoes das operacoes restantes.
 * O lower bound e o maximo destes valores.
 * E admissivel: nunca sobrestima o makespan real.
 */
static int lower_bound(const State *s) {
    int lb = s->makespan;
    for (int j = 0; j < prob.num_jobs; j++) {
        int rem = 0;
        for (int o = s->next_op[j]; o < prob.num_ops[j]; o++)
            rem += prob.duration[j][o];
        int est = s->job_free[j] + rem;
        if (est > lb) lb = est;
    }
    return lb;
}

/* ─── Greedy ERT+SPT para completar estados parciais ────────────────────── */

/*
 * Quando o B&B atinge o limite de profundidade sem solucao completa,
 * completa o escalonamento com greedy: em cada passo escolhe a operacao
 * pronta com menor tempo de inicio possivel (ERT), desempatando pela
 * menor duracao (SPT). Garante sempre uma solucao valida.
 */
static void greedy_complete(State s) {
    /* Variaveis locais — trabalha sobre copias, nao altera o estado original */
    int mf[MAX_MACHINES], jf[MAX_JOBS], nxt[MAX_JOBS];
    int ls[MAX_JOBS][MAX_OPS];

    memcpy(mf,  s.machine_free, sizeof(mf));
    memcpy(jf,  s.job_free,     sizeof(jf));
    memcpy(nxt, s.next_op,      sizeof(nxt));
    for (int j = 0; j < prob.num_jobs; j++)
        memcpy(ls[j], s.start[j], prob.num_ops[j] * sizeof(int));

    int remaining = total_ops - s.scheduled;
    for (int k = 0; k < remaining; k++) {
        int bj = -1, bo = -1, best_est = INT_MAX, best_dur = INT_MAX;
        for (int j = 0; j < prob.num_jobs; j++) {
            int o = nxt[j];
            if (o >= prob.num_ops[j]) continue;
            int m   = prob.machine[j][o];
            int dur = prob.duration[j][o];
            int est = jf[j] > mf[m] ? jf[j] : mf[m];
            if (est < best_est || (est == best_est && dur < best_dur))
                { bj = j; bo = o; best_est = est; best_dur = dur; }
        }
        int m   = prob.machine[bj][bo];
        int end = best_est + prob.duration[bj][bo];
        ls[bj][bo] = best_est;
        mf[m] = end; jf[bj] = end; nxt[bj]++;
    }

    int mk = 0;
    for (int j = 0; j < prob.num_jobs; j++)
        for (int o = 0; o < prob.num_ops[j]; o++) {
            int e = ls[j][o] + prob.duration[j][o];
            if (e > mk) mk = e;
        }

    if (mk < best_makespan) {
        best_makespan = mk;
        for (int j = 0; j < prob.num_jobs; j++)
            memcpy(best_start[j], ls[j], prob.num_ops[j] * sizeof(int));
    }
}

/* ─── Branch & Bound recursivo ──────────────────────────────────────────── */

/*
 * Profundidade maxima de pesquisa.
 * Quando atingida, completa com greedy em vez de descartar o ramo.
 * Garante sempre uma solucao valida mesmo para instancias grandes.
 * Valor 0 = sem limite (B&B exacto, so viavel para instancias pequenas).
 */
static int max_depth;

static void bb(State s, int depth) {

    /* Solucao completa */
    if (s.scheduled == total_ops) {
        if (s.makespan < best_makespan) {
            best_makespan = s.makespan;
            for (int j = 0; j < prob.num_jobs; j++)
                memcpy(best_start[j], s.start[j],
                       prob.num_ops[j] * sizeof(int));
        }
        return;
    }

    /* Pruning: descarta ramos que nao podem melhorar */
    if (lower_bound(&s) >= best_makespan) return;

    /* Limite de profundidade: completa com greedy em vez de descartar */
    if (max_depth > 0 && depth >= max_depth) {
        greedy_complete(s);
        return;
    }

    /* Branching: tenta colocar a proxima operacao de cada job */
    for (int j = 0; j < prob.num_jobs; j++) {
        int o = s.next_op[j];
        if (o >= prob.num_ops[j]) continue;

        int m       = prob.machine[j][o];
        int dur     = prob.duration[j][o];
        int t_start = s.job_free[j] > s.machine_free[m]
                      ? s.job_free[j] : s.machine_free[m];
        int t_end   = t_start + dur;

        State child = s;
        child.start[j][o]     = t_start;
        child.job_free[j]     = t_end;
        child.machine_free[m] = t_end;
        child.next_op[j]++;
        child.scheduled++;
        if (t_end > child.makespan) child.makespan = t_end;

        if (lower_bound(&child) < best_makespan)
            bb(child, depth + 1);
    }
}

/* ─── Escrita da saida ───────────────────────────────────────────────────── */

static void write_result(const char *file) {
    FILE *f = fopen(file, "w");
    if (!f) { fprintf(stderr, "Erro ao criar %s\n", file); return; }
    fprintf(f, "%d\n", best_makespan);
    for (int j = 0; j < prob.num_jobs; j++) {
        for (int o = 0; o < prob.num_ops[j]; o++) {
            if (o > 0) fprintf(f, " ");
            fprintf(f, "%d", best_start[j][o]);
        }
        fprintf(f, "\n");
    }
    fclose(f);
}

/* ─── Validacao ──────────────────────────────────────────────────────────── */

static int validate(void) {
    /* R1: ordem dentro do job */
    for (int j = 0; j < prob.num_jobs; j++)
        for (int o = 1; o < prob.num_ops[j]; o++)
            if (best_start[j][o] < best_start[j][o-1] + prob.duration[j][o-1])
                return 0;
    /* R2: sem sobreposicao na mesma maquina */
    for (int j1 = 0; j1 < prob.num_jobs; j1++)
      for (int o1 = 0; o1 < prob.num_ops[j1]; o1++)
        for (int j2 = j1; j2 < prob.num_jobs; j2++) {
            int so2 = (j2 == j1) ? o1+1 : 0;
            for (int o2 = so2; o2 < prob.num_ops[j2]; o2++) {
                if (prob.machine[j1][o1] != prob.machine[j2][o2]) continue;
                int s1 = best_start[j1][o1], e1 = s1 + prob.duration[j1][o1];
                int s2 = best_start[j2][o2], e2 = s2 + prob.duration[j2][o2];
                if (!(e1 <= s2 || e2 <= s1)) return 0;
            }
        }
    return 1;
}

/* ─── Main ───────────────────────────────────────────────────────────────── */

int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Uso: %s <entrada> <saida> [repeticoes]\n", argv[0]);
        return 1;
    }
    int reps = (argc >= 4) ? atoi(argv[3]) : 1;

    if (!read_problem(argv[1])) return 1;

    fprintf(stderr, "Problema: %d jobs x %d maquinas | reps: %d\n",
            prob.num_jobs, prob.num_machines, reps);

    /* Upper bound inicial: soma de todas as duracoes (pior caso) */
    int ub = 0;
    for (int j = 0; j < prob.num_jobs; j++)
        for (int o = 0; o < prob.num_ops[j]; o++)
            ub += prob.duration[j][o];

    /* Limite de profundidade:
     *   - instancias pequenas (<=20 ops): B&B exacto (sem limite)
     *   - instancias maiores: limita a profundidade e completa com greedy
     * Pode ser sobreposto com BB_DEPTH=N */
    max_depth = (total_ops <= 20) ? 0 : prob.num_jobs;
    {
        char *e = getenv("BB_DEPTH");
        if (e) max_depth = atoi(e);
    }
    fprintf(stderr, "BB_DEPTH=%d (%s)\n", max_depth,
            max_depth == 0 ? "exacto" : "hibrido B&B+greedy");

    /* Medicao de tempo: N repeticoes sem output */
    double total_time = 0.0;
    struct timespec t0, t1;

    for (int r = 0; r < reps; r++) {
        best_makespan = ub;
        memset(best_start, 0, sizeof(best_start));

        State init;
        memset(&init, 0, sizeof(init));

        clock_gettime(CLOCK_MONOTONIC, &t0);
        bb(init, 0);
        clock_gettime(CLOCK_MONOTONIC, &t1);

        total_time += (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec)*1e-9;
    }

    double media = total_time / reps;

    fprintf(stderr, "Upper bound (pior caso): %d\n", ub);
    fprintf(stderr, "Makespan B&B           : %d\n", best_makespan);
    fprintf(stderr, "Melhoria               : %.1f%%\n",
            100.0*(ub - best_makespan)/ub);
    fprintf(stderr, "Tempo medio (%d reps)  : %.6f s\n", reps, media);
    fprintf(stderr, "Tempo total            : %.6f s\n", total_time);
    fprintf(stderr, "Solucao %s\n", validate() ? "valida" : "INVALIDA");

    write_result(argv[2]);

    /* Linha CSV: SEQ,reps,makespan,tempo_medio,tempo_total */
    printf("SEQ,%d,%d,%.6f,%.6f\n", reps, best_makespan, media, total_time);
    return 0;
}