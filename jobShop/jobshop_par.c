/*
 * Job-Shop Scheduling - Implementacao Paralela (OpenMP)
 * Algoritmo: Branch & Bound com particao de trabalho por threads
 *
 * Estrategia de paralelismo:
 *   1. Expande a raiz 1 nivel em BFS -> gera num_jobs nos de trabalho
 *   2. Cada thread explora um no independente em DFS (parallel for)
 *   3. Threads partilham o melhor makespan global para podarem ramos
 *
 * Compilacao:  gcc -O2 -fopenmp -Wall -o jobshop_par jobshop_par.c
 * Uso:         ./jobshop_par <entrada> <saida> <threads> [repeticoes]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <omp.h>

#define MAX_JOBS        50
#define MAX_MACHINES    50
#define MAX_OPS         50
#define MAX_NODES       4096  /* nos do work pool */

/* ─── Estruturas de dados ────────────────────────────────────────────────── */

typedef struct {
    int num_jobs;
    int num_machines;
    int num_ops[MAX_JOBS];
    int machine[MAX_JOBS][MAX_OPS];
    int duration[MAX_JOBS][MAX_OPS];
} Problem;

/*
 * Estado do B&B.
 * Cada thread tem a sua copia na stack — sem malloc, sem apontadores internos.
 */
typedef struct {
    int machine_free[MAX_MACHINES];
    int job_free[MAX_JOBS];
    int next_op[MAX_JOBS];
    int start[MAX_JOBS][MAX_OPS];
    int makespan;
    int scheduled;
} State;

/* ─── Variaveis globais partilhadas ─────────────────────────────────────── */

/* So leitura apos inicializacao — sem necessidade de protecao */
static Problem prob;
static int     total_ops;

/*
 * Leitura/escrita pelas threads — protegidas por lock:
 *   global_best  — melhor makespan encontrado (upper bound global)
 *   best_start   — solucao associada ao melhor makespan
 *
 * Seccao critica: quando uma thread encontra solucao melhor, actualiza
 * global_best e best_start dentro do lock para evitar race condition.
 *
 * Variavel global_best e tambem lida SEM lock pelas threads para pruning
 * (race benigna: na pior hipotese a thread usa um bound ligeiramente
 * desatualizado, mas a correcao da solucao nao e afectada).
 */
static volatile int global_best;
static int          best_start[MAX_JOBS][MAX_OPS];
static omp_lock_t   lock;

/* Work pool: nos gerados pela expansao da raiz, distribuidos pelas threads */
static State work_pool[MAX_NODES];
static int   pool_size;

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

/* ─── Branch & Bound recursivo ──────────────────────────────────────────── */

/*
 * Executado por cada thread sobre o seu no de trabalho.
 *
 * Variaveis LOCAIS de cada thread (privadas, na stack):
 *   s (State), child (State), j, o, m, t_start, t_end (int)
 *
 * Variaveis globais SO LEITURA (sem lock):
 *   prob, total_ops
 *
 * Variaveis globais LEITURA/ESCRITA:
 *   global_best — lido sem lock (volatile); escrito dentro de lock
 *   best_start  — escrito dentro de lock
 */
/* ─── Greedy ERT+SPT para completar estados parciais (thread-safe) ─────── */

/*
 * Identica ao sequencial mas actualiza global_best com lock (thread-safe).
 * Cada thread trabalha sobre copias locais — sem acesso a memoria partilhada
 * durante o calculo. So usa o lock no final para actualizar o melhor global.
 */
static void greedy_complete(State s) {
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

    /* SECCAO CRITICA: actualiza solucao global se melhorou */
    omp_set_lock(&lock);
    if (mk < global_best) {
        global_best = mk;
        for (int j = 0; j < prob.num_jobs; j++)
            memcpy(best_start[j], ls[j], prob.num_ops[j] * sizeof(int));
    }
    omp_unset_lock(&lock);
}

/* Profundidade maxima de pesquisa por thread. Ajustada no main(). */
static int max_depth;

static void bb(State s, int depth) {

    /* Solucao completa */
    if (s.scheduled == total_ops) {
        if (s.makespan < global_best) {
            /* ── SECCAO CRITICA: actualiza a melhor solucao global ── */
            omp_set_lock(&lock);
            if (s.makespan < global_best) {
                global_best = s.makespan;
                for (int j = 0; j < prob.num_jobs; j++)
                    memcpy(best_start[j], s.start[j],
                           prob.num_ops[j] * sizeof(int));
            }
            omp_unset_lock(&lock);
        }
        return;
    }

    /* Pruning */
    if (lower_bound(&s) >= global_best) return;

    /* Limite de profundidade: completa com greedy em vez de descartar */
    if (max_depth > 0 && depth >= max_depth) {
        greedy_complete(s);
        return;
    }

    /* Branching */
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

        if (lower_bound(&child) < global_best)
            bb(child, depth + 1);
    }
}

/* ─── Expansao da raiz (1 nivel BFS) para gerar work pool ──────────────── */

/*
 * Expande o estado inicial 1 nivel: gera um no filho por job.
 * Cada filho corresponde a escalonar a 1a operacao do job j.
 * Resultado: pool_size nos em work_pool[], um por thread.
 *
 * Com mais threads podemos expandir mais niveis (BB_EXPAND=N).
 */
static void build_pool(int n_levels) {
    State root;
    memset(&root, 0, sizeof(root));
    work_pool[0] = root;
    int cur = 0, cur_n = 1;

    for (int lv = 0; lv < n_levels && cur_n > 0; lv++) {
        int nxt = cur + cur_n, nxt_n = 0;
        for (int i = 0; i < cur_n && nxt + nxt_n < MAX_NODES; i++) {
            State *s = &work_pool[cur + i];
            if (lower_bound(s) >= global_best || s->scheduled == total_ops) continue;
            for (int j = 0; j < prob.num_jobs && nxt + nxt_n < MAX_NODES; j++) {
                int o = s->next_op[j];
                if (o >= prob.num_ops[j]) continue;
                int m = prob.machine[j][o], dur = prob.duration[j][o];
                int ts = s->job_free[j] > s->machine_free[m]
                         ? s->job_free[j] : s->machine_free[m];
                int te = ts + dur;
                State child = *s;
                child.start[j][o] = ts;
                child.job_free[j] = te; child.machine_free[m] = te;
                child.next_op[j]++; child.scheduled++;
                if (te > child.makespan) child.makespan = te;
                if (lower_bound(&child) >= global_best) continue;
                work_pool[nxt + nxt_n++] = child;
            }
        }
        cur = nxt; cur_n = nxt_n;
    }
    if (cur > 0 && cur_n > 0)
        memmove(work_pool, work_pool + cur, cur_n * sizeof(State));
    pool_size = cur_n;
}

/* ─── Validacao ──────────────────────────────────────────────────────────── */

static int validate(void) {
    for (int j = 0; j < prob.num_jobs; j++)
        for (int o = 1; o < prob.num_ops[j]; o++)
            if (best_start[j][o] < best_start[j][o-1] + prob.duration[j][o-1])
                return 0;
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

/* ─── Escrita da saida ───────────────────────────────────────────────────── */

static void write_result(const char *file) {
    FILE *f = fopen(file, "w");
    if (!f) { fprintf(stderr, "Erro ao criar %s\n", file); return; }
    fprintf(f, "%d\n", global_best);
    for (int j = 0; j < prob.num_jobs; j++) {
        for (int o = 0; o < prob.num_ops[j]; o++) {
            if (o > 0) fprintf(f, " ");
            fprintf(f, "%d", best_start[j][o]);
        }
        fprintf(f, "\n");
    }
    fclose(f);
}

/* ─── Main ───────────────────────────────────────────────────────────────── */

int main(int argc, char *argv[]) {
    if (argc < 4) {
        fprintf(stderr, "Uso: %s <entrada> <saida> <threads> [repeticoes]\n",
                argv[0]);
        return 1;
    }
    int num_threads = atoi(argv[3]);
    int reps        = (argc >= 5) ? atoi(argv[4]) : 1;
    if (num_threads < 1) num_threads = 1;
    if (reps < 1) reps = 1;

    /* ── Codigo de arranque (sequencial) ── */
    if (!read_problem(argv[1])) return 1;

    fprintf(stderr, "Problema: %d jobs x %d maquinas | threads: %d | reps: %d\n",
            prob.num_jobs, prob.num_machines, num_threads, reps);

    /* Upper bound inicial = soma de todas as duracoes (pior caso) */
    int ub = 0;
    for (int j = 0; j < prob.num_jobs; j++)
        for (int o = 0; o < prob.num_ops[j]; o++)
            ub += prob.duration[j][o];

    omp_init_lock(&lock);
    omp_set_num_threads(num_threads);

    /* Niveis de expansao: mais threads → mais nos no pool */
    int n_levels = 1;
    {
        long est = prob.num_jobs;
        while (est < num_threads * 2 && n_levels < 4) {
            n_levels++; est *= prob.num_jobs;
        }
    }

    /* Limite de profundidade: igual ao sequencial para comparacao justa */
    max_depth = (total_ops <= 20) ? 0 : prob.num_jobs;
    {
        char *e = getenv("BB_DEPTH");
        if (e) max_depth = atoi(e);
    }
    fprintf(stderr, "BB_DEPTH=%d | pool levels=%d\n", max_depth, n_levels);

    /* ── Ciclo de medicao: N repeticoes, sem output ── */
    double total_time = 0.0;

    for (int r = 0; r < reps; r++) {

        /* Reset do estado global */
        global_best = ub;
        memset(best_start, 0, sizeof(best_start));

        /* Inicio da medicao — inclui criacao e termino das threads */
        double t_start = omp_get_wtime();

        /* Expande a raiz para gerar o work pool */
        build_pool(n_levels);

        /*
         * ── Codigo executado pelas threads ──────────────────────────────
         *
         * Padrao: work pool estatico + schedule(dynamic,1)
         *   Cada iteracao e um no independente do work pool.
         *   dynamic: OpenMP atribui nos as threads conforme ficam livres,
         *   balancando a carga variavel das sub-arvores.
         *
         * Variaveis partilhadas so leitura : prob, total_ops, work_pool, pool_size
         * Variaveis partilhadas leit/escrita: global_best (volatile, sem lock),
         *                                     best_start + global_best (com lock)
         * Variaveis locais de cada thread  : i (int), s (State)
         * Tecnica de exclusao mutua        : omp_lock_t lock
         */
        #pragma omp parallel for schedule(dynamic, 1) \
            shared(work_pool, pool_size, global_best, best_start, lock, \
                   prob, total_ops, max_depth) \
            default(none)
        for (int i = 0; i < pool_size; i++) {
            State s = work_pool[i];
            if (lower_bound(&s) < global_best)
                bb(s, 0);
        }
        /* Barreira implicita: todas as threads terminaram aqui */

        total_time += omp_get_wtime() - t_start;
    }

    /* ── Codigo final (sequencial) ── */
    double media = total_time / reps;

    fprintf(stderr, "Upper bound (pior caso): %d\n", ub);
    fprintf(stderr, "Makespan B&B           : %d\n", global_best);
    fprintf(stderr, "Melhoria               : %.1f%%\n",
            100.0*(ub - global_best)/ub);
    fprintf(stderr, "Pool size              : %d nos\n", pool_size);
    fprintf(stderr, "Tempo medio (%d reps)  : %.6f s\n", reps, media);
    fprintf(stderr, "Tempo total            : %.6f s\n", total_time);
    fprintf(stderr, "Solucao %s\n", validate() ? "valida" : "INVALIDA");

    write_result(argv[2]);

    /* CSV: PAR,threads,reps,makespan_ub,makespan_bb,tempo_medio,tempo_total */
    printf("PAR,%d,%d,%d,%d,%.6f,%.6f\n",
           num_threads, reps, ub, global_best, media, total_time);

    omp_destroy_lock(&lock);
    return 0;
}