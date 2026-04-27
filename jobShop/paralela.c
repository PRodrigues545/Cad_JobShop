/*
 * Job-Shop Scheduling Problem - Implementação Paralela (OpenMP)
 * Algoritmo: Branch & Bound paralelo com partilha de memória
 *
 * ════════════════════════════════════════════════════════════════════════════
 * ESTRATÉGIA DE PARALELISMO — METODOLOGIA DE FOSTER
 * ════════════════════════════════════════════════════════════════════════════
 *
 * PARTICIONAMENTO:
 *   O espaço de pesquisa do B&B é uma árvore. Cada nodo representa um
 *   escalonamento parcial; cada filho resulta de escolher qual o próximo
 *   job a escalonar. As sub-árvores são independentes entre si e constituem
 *   a fonte natural de paralelismo.
 *
 *   Fase 1 (sequencial): calcula o upper bound inicial = makespan sequencial
 *                        puro (soma de todas as durações, igual ao jobshop_seq).
 *                        Este é o pior caso de referência.
 *   Fase 2 (sequencial): expande o nodo raiz em BFS até N_EXPAND níveis,
 *                        produzindo um "work pool" de nodos independentes.
 *   Fase 3 (paralela):   cada thread explora uma sub-árvore do B&B, tentando
 *                        encontrar escalonamentos válidos com makespan menor.
 *   Fase 4 (sequencial): validação e escrita do resultado.
 *
 * COMUNICAÇÃO ENTRE THREADS:
 *   - global_best (volatile int) — upper bound global.
 *     Lido sem lock (volatile); escrito dentro de best_lock.
 *   - best_starts (int[][])      — solução do melhor makespan.
 *     Sempre escrito dentro de best_lock.
 *
 * EXCLUSÃO MÚTUA:
 *   - omp_lock_t best_lock — lock explícito OpenMP (mais fino que critical).
 *
 * SECÇÕES CRÍTICAS:
 *   - Leitura/escrita de global_best + best_starts em greedy_complete() e
 *     bb_solve() quando encontram solução completa.
 *
 * LIMITE DE PROFUNDIDADE (híbrido B&B + greedy):
 *   Para instâncias grandes, quando a recursão atinge g_depth_limit,
 *   completa o escalonamento com o greedy. Garante terminação em tempo
 *   razoável e produz sempre solução válida e melhor que o greedy puro.
 *
 * ════════════════════════════════════════════════════════════════════════════
 * Compilação:
 *   gcc -O2 -fopenmp -Wall -o jobshop_par jobshop_par.c
 *
 * Uso:
 *   ./jobshop_par <entrada> <saida> <num_threads>
 *
 * Variáveis de ambiente opcionais:
 *   BB_DEPTH=N    — profundidade do B&B (0 = ilimitado/exacto)
 *   BB_EXPAND=N   — níveis de BFS para gerar o work pool (default: 3)
 * ════════════════════════════════════════════════════════════════════════════
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <omp.h>

/* ─── Limites estáticos (sem apontadores internos) ──────────────────────── */
#define MAX_JOBS         100
#define MAX_MACHINES     100
#define MAX_OPS_PER_JOB  100
#define MAX_WORK_NODES   16384  /* tamanho máximo do work pool              */

/* ─── Estruturas de dados ───────────────────────────────────────────────── */

typedef struct {
    int machine;
    int duration;
} Operation;

/* Problema — só leitura após read_problem() */
typedef struct {
    int num_jobs;
    int num_machines;
    int num_ops[MAX_JOBS];
    Operation ops[MAX_JOBS][MAX_OPS_PER_JOB];
} Problem;

/*
 * Estado parcial do B&B.
 * Cada thread tem a sua própria cópia na stack — sem malloc,
 * sem apontadores internos (conforme restrição do enunciado).
 */
typedef struct {
    int machine_free[MAX_MACHINES];         /* quando cada máquina fica livre */
    int job_free[MAX_JOBS];                 /* quando cada job fica livre     */
    int next_op[MAX_JOBS];                  /* próxima op a escalonar por job */
    int starts[MAX_JOBS][MAX_OPS_PER_JOB];  /* tempos de início atribuídos   */
    int lb;                                 /* lower bound actual (max end)   */
    int scheduled;                          /* nº de ops já escalonadas       */
} BBState;

/* ─── Variáveis globais partilhadas ─────────────────────────────────────── */

/* Leitura/escrita — protegidas por best_lock */
static volatile int  global_best;
static int           best_starts[MAX_JOBS][MAX_OPS_PER_JOB];
static omp_lock_t    best_lock;

/* Só leitura após inicialização — sem necessidade de lock */
static Problem  prob;
static int      total_ops;
static int      g_depth_limit;

/* Work pool — preenchido sequencialmente, lido em paralelo */
static BBState  work_pool[MAX_WORK_NODES];
static int      work_pool_size;

/* ─── Leitura do problema ────────────────────────────────────────────────── */

static int read_problem(const char *filename) {
    FILE *f = fopen(filename, "r");
    if (!f) {
        fprintf(stderr, "Erro: nao foi possivel abrir '%s'\n", filename);
        return 0;
    }
    if (fscanf(f, "%d %d", &prob.num_jobs, &prob.num_machines) != 2) {
        fprintf(stderr, "Erro: cabecalho invalido\n");
        fclose(f); return 0;
    }
    total_ops = 0;
    for (int j = 0; j < prob.num_jobs; j++) {
        int op = 0, machine, duration;
        for (int o = 0; o < prob.num_machines; o++) {
            if (fscanf(f, "%d %d", &machine, &duration) != 2) break;
            prob.ops[j][op].machine  = machine;
            prob.ops[j][op].duration = duration;
            op++;
        }
        prob.num_ops[j] = op;
        total_ops += op;
    }
    fclose(f);
    return 1;
}

/* ─── Lower bound admissível ─────────────────────────────────────────────── */

/*
 * lb = max(lb_actual, max_j(job_free[j] + soma_durações_restantes[j]))
 * É admissível: nunca sobrestima o makespan real → pruning correcto.
 */
static inline int lower_bound(const BBState *s) {
    int lb = s->lb;
    for (int j = 0; j < prob.num_jobs; j++) {
        int rem = 0;
        for (int o = s->next_op[j]; o < prob.num_ops[j]; o++)
            rem += prob.ops[j][o].duration;
        int est = s->job_free[j] + rem;
        if (est > lb) lb = est;
    }
    return lb;
}

/* ─── Upper bound inicial: escalonamento sequencial puro ────────────────── */

/*
 * sequential_upper_bound():
 *
 *   Calcula o makespan do caso sequencial puro — exactamente o mesmo
 *   algoritmo do jobshop_seq: percorre todos os jobs e operações por ordem,
 *   atribuindo cada operação imediatamente após a anterior, SEM considerar
 *   máquinas.
 *
 *   Este valor (soma de todas as durações) é o upper bound de arranque do B&B.
 *   Partir deste valor é correcto: qualquer solução válida que respeite as
 *   restrições do problema tem makespan ≤ makespan_sequencial, pelo que o
 *   B&B vai sempre melhorar (ou igualar) o caso sequencial.
 *
 *   Guarda o resultado em global_best e best_starts (a solução sequencial
 *   é válida do ponto de vista de tempos de início — respeita a ordem dentro
 *   de cada job, embora não respeite a restrição de máquinas).
 */
static int sequential_upper_bound(void) {
    int cursor = 0;
    int starts[MAX_JOBS][MAX_OPS_PER_JOB];

    for (int j = 0; j < prob.num_jobs; j++)
        for (int o = 0; o < prob.num_ops[j]; o++) {
            starts[j][o] = cursor;
            cursor += prob.ops[j][o].duration;
        }

    /* cursor == soma de todas as durações == makespan sequencial */
    global_best = cursor;
    for (int j = 0; j < prob.num_jobs; j++)
        memcpy(best_starts[j], starts[j],
               (size_t)prob.num_ops[j] * sizeof(int));

    return cursor;  /* devolve o makespan sequencial para mostrar no output */
}

/* ─── Completar estado parcial com greedy ERT+SPT ───────────────────────── */

/*
 * greedy_complete():
 *   Usada apenas DENTRO do B&B (quando o limite de profundidade é atingido)
 *   para completar rapidamente um estado parcial com uma heurística válida.
 *   NÃO é usada como upper bound inicial — esse papel pertence ao sequencial.
 *
 *   Trabalha inteiramente em variáveis locais — thread-safe sem locks.
 *   Actualiza global_best/best_starts em secção crítica se melhorar.
 */
static void greedy_complete(const BBState *s) {
    int mf[MAX_MACHINES], jf[MAX_JOBS], nxt[MAX_JOBS];
    int ls[MAX_JOBS][MAX_OPS_PER_JOB];

    memcpy(mf,  s->machine_free, sizeof(mf));
    memcpy(jf,  s->job_free,     sizeof(jf));
    memcpy(nxt, s->next_op,      sizeof(nxt));
    for (int j = 0; j < prob.num_jobs; j++)
        memcpy(ls[j], s->starts[j], (size_t)prob.num_ops[j] * sizeof(int));

    int remaining = total_ops - s->scheduled;
    for (int k = 0; k < remaining; k++) {
        int bj = -1, bo = -1, best_est = INT_MAX, best_dur = INT_MAX;
        for (int j = 0; j < prob.num_jobs; j++) {
            int o = nxt[j];
            if (o >= prob.num_ops[j]) continue;
            int m   = prob.ops[j][o].machine;
            int dur = prob.ops[j][o].duration;
            int est = jf[j] > mf[m] ? jf[j] : mf[m];
            if (est < best_est || (est == best_est && dur < best_dur))
                { bj = j; bo = o; best_est = est; best_dur = dur; }
        }
        int m   = prob.ops[bj][bo].machine;
        int end = best_est + prob.ops[bj][bo].duration;
        ls[bj][bo] = best_est;
        mf[m] = end; jf[bj] = end; nxt[bj]++;
    }

    int mk = 0;
    for (int j = 0; j < prob.num_jobs; j++)
        for (int o = 0; o < prob.num_ops[j]; o++) {
            int e = ls[j][o] + prob.ops[j][o].duration;
            if (e > mk) mk = e;
        }

    /* ── SECÇÃO CRÍTICA: actualiza a melhor solução global ── */
    omp_set_lock(&best_lock);
    if (mk < global_best) {
        global_best = mk;
        for (int j = 0; j < prob.num_jobs; j++)
            memcpy(best_starts[j], ls[j],
                   (size_t)prob.num_ops[j] * sizeof(int));
    }
    omp_unset_lock(&best_lock);
}

/* ─── Branch & Bound recursivo (código das threads) ────────────────────── */

/*
 * bb_solve() — executado por cada thread OpenMP.
 *
 * Variáveis LOCAIS da thread (privadas, na stack):
 *   s      (BBState)  — estado parcial actual
 *   child  (BBState)  — estado filho gerado no branching
 *   j, o, m, dur, earliest, end_time  (int)
 *
 * Variáveis globais SÓ DE LEITURA (sem lock):
 *   prob, total_ops, g_depth_limit
 *
 * Variáveis globais LEITURA/ESCRITA:
 *   global_best — lido como volatile (sem lock); escrito com best_lock
 *   best_starts — escrito dentro de best_lock (via greedy_complete)
 */
static void bb_solve(BBState s, int depth) {

    /* ── Solução completa ─────────────────────────────────────────────────── */
    if (s.scheduled == total_ops) {
        /* ── SECÇÃO CRÍTICA ── */
        omp_set_lock(&best_lock);
        if (s.lb < global_best) {
            global_best = s.lb;
            for (int j = 0; j < prob.num_jobs; j++)
                memcpy(best_starts[j], s.starts[j],
                       (size_t)prob.num_ops[j] * sizeof(int));
        }
        omp_unset_lock(&best_lock);
        return;
    }

    /* ── Pruning por upper bound ──────────────────────────────────────────── */
    /* Leitura de global_best sem lock: race benigna — pior caso usa bound
     * ligeiramente desatualizado, solução permanece correcta. */
    if (lower_bound(&s) >= global_best) return;

    /* ── Limite de profundidade: completa com greedy ──────────────────────── */
    if (g_depth_limit > 0 && depth >= g_depth_limit) {
        greedy_complete(&s);
        return;
    }

    /* ── Branching: um filho por job com operações pendentes ──────────────── */
    for (int j = 0; j < prob.num_jobs; j++) {
        int o = s.next_op[j];
        if (o >= prob.num_ops[j]) continue;          /* job concluído */

        int m   = prob.ops[j][o].machine;
        int dur = prob.ops[j][o].duration;

        BBState child = s;   /* cópia stack-allocated — sem malloc */

        int earliest = child.job_free[j] > child.machine_free[m]
                       ? child.job_free[j] : child.machine_free[m];
        child.starts[j][o]     = earliest;
        int end_time            = earliest + dur;
        child.machine_free[m]  = end_time;
        child.job_free[j]      = end_time;
        child.next_op[j]++;
        child.scheduled++;
        if (end_time > child.lb) child.lb = end_time;

        /* Pruning antes de descer */
        if (lower_bound(&child) < global_best)
            bb_solve(child, depth + 1);
    }
}

/* ─── Expansão da raiz para gerar o work pool ───────────────────────────── */

/*
 * expand_work_pool():
 *   Expande o nodo raiz exactamente `n_levels` níveis em BFS.
 *   Após n_levels, TODOS os nodos da fronteira ficam no work pool —
 *   o número de nodos cresce no máximo como J^n_levels mas é limitado por
 *   MAX_WORK_NODES e pruning. A contagem é determinista e sem loop infinito.
 *
 *   Usa dois arrays estáticos como ping-pong:
 *     cur_buf / nxt_buf — fronteira actual / seguinte
 *   (ambos apontam para secções de work_pool, sem alloc dinâmica)
 */
static void expand_work_pool(int n_levels) {
    /* Usamos work_pool como buffer de trabalho.
     * cur[0..cur_n-1] = nível actual; nxt começa imediatamente a seguir. */

    /* Nível 0: só a raiz */
    BBState root;
    memset(&root, 0, sizeof(root));
    work_pool[0] = root;
    int cur_start = 0, cur_n = 1;

    for (int level = 0; level < n_levels && cur_n > 0; level++) {
        int nxt_start = cur_start + cur_n;  /* os filhos vão aqui */
        int nxt_n     = 0;

        for (int i = 0; i < cur_n; i++) {
            BBState *s = &work_pool[cur_start + i];

            /* Pruning */
            if (s->lb >= global_best)      continue;
            if (s->scheduled == total_ops) continue;  /* folha */

            /* Gera filhos */
            for (int j = 0; j < prob.num_jobs; j++) {
                int o = s->next_op[j];
                if (o >= prob.num_ops[j]) continue;

                if (nxt_start + nxt_n >= MAX_WORK_NODES) goto done;

                int m   = prob.ops[j][o].machine;
                int dur = prob.ops[j][o].duration;
                BBState child = *s;

                int earliest = child.job_free[j] > child.machine_free[m]
                               ? child.job_free[j] : child.machine_free[m];
                child.starts[j][o]    = earliest;
                int end_time           = earliest + dur;
                child.machine_free[m] = end_time;
                child.job_free[j]     = end_time;
                child.next_op[j]++;
                child.scheduled++;
                if (end_time > child.lb) child.lb = end_time;

                if (child.lb >= global_best) continue;

                work_pool[nxt_start + nxt_n] = child;
                nxt_n++;
            }
        }

        /* Avança para o nível seguinte */
        cur_start = nxt_start;
        cur_n     = nxt_n;
    }
done:
    /* Compacta para work_pool[0..cur_n-1] */
    if (cur_start > 0 && cur_n > 0)
        memmove(work_pool, work_pool + cur_start,
                (size_t)cur_n * sizeof(BBState));
    work_pool_size = cur_n;
}

/* ─── Validação da solução ──────────────────────────────────────────────── */

static int validate_solution(void) {
    /* Restrição 1: ordem dentro de cada job */
    for (int j = 0; j < prob.num_jobs; j++)
        for (int o = 1; o < prob.num_ops[j]; o++) {
            int prev_end = best_starts[j][o-1] + prob.ops[j][o-1].duration;
            if (best_starts[j][o] < prev_end) {
                fprintf(stderr, "VIOLACAO R1: job %d op %d\n", j, o);
                return 0;
            }
        }
    /* Restrição 2: sem sobreposição na mesma máquina */
    for (int j1 = 0; j1 < prob.num_jobs; j1++)
        for (int o1 = 0; o1 < prob.num_ops[j1]; o1++)
            for (int j2 = j1; j2 < prob.num_jobs; j2++) {
                int so2 = (j2 == j1) ? o1+1 : 0;
                for (int o2 = so2; o2 < prob.num_ops[j2]; o2++) {
                    if (prob.ops[j1][o1].machine != prob.ops[j2][o2].machine)
                        continue;
                    int s1 = best_starts[j1][o1],
                        e1 = s1 + prob.ops[j1][o1].duration;
                    int s2 = best_starts[j2][o2],
                        e2 = s2 + prob.ops[j2][o2].duration;
                    if (!(e1 <= s2 || e2 <= s1)) {
                        fprintf(stderr,
                            "VIOLACAO R2: maquina %d job%d/op%d vs job%d/op%d\n",
                            prob.ops[j1][o1].machine, j1, o1, j2, o2);
                        return 0;
                    }
                }
            }
    return 1;
}

/* ─── Escrita da saída ──────────────────────────────────────────────────── */

static int write_result(const char *filename) {
    FILE *f = fopen(filename, "w");
    if (!f) {
        fprintf(stderr, "Erro: nao foi possivel criar '%s'\n", filename);
        return 0;
    }
    fprintf(f, "%d\n", global_best);
    for (int j = 0; j < prob.num_jobs; j++) {
        for (int o = 0; o < prob.num_ops[j]; o++) {
            if (o > 0) fprintf(f, " ");
            fprintf(f, "%d", best_starts[j][o]);
        }
        fprintf(f, "\n");
    }
    fclose(f);
    return 1;
}

/* ─── Main ──────────────────────────────────────────────────────────────── */

/*
 * Uso: ./jobshop_par <entrada> <saida> <num_threads> [repeticoes]
 *
 *   <entrada>    — ficheiro .jss com o problema
 *   <saida>      — ficheiro de saída com o escalonamento
 *   <num_threads>— número de threads OpenMP
 *   [repeticoes] — número de execuções para medição (default: 1)
 *
 * Durante a medição do tempo NÃO há qualquer output para a consola.
 * O tempo reportado é a MÉDIA das repetições.
 * O tempo INCLUI a criação e término das threads em cada repetição.
 * O resultado escrito no ficheiro corresponde à última repetição.
 */
int main(int argc, char *argv[]) {
    if (argc < 4 || argc > 5) {
        fprintf(stderr, "Uso: %s <entrada> <saida> <threads> [repeticoes]\n",
                argv[0]);
        return EXIT_FAILURE;
    }

    int num_threads  = atoi(argv[3]);
    int repeticoes   = (argc == 5) ? atoi(argv[4]) : 1;
    if (num_threads < 1) num_threads = 1;
    if (repeticoes  < 1) repeticoes  = 1;

    /* ════════════════════════════════════════════════════════════════════════
     * CÓDIGO DE ARRANQUE (sequencial) — executado uma única vez
     * ════════════════════════════════════════════════════════════════════════ */

    /* 1. Leitura do problema */
    memset(&prob, 0, sizeof(prob));
    if (!read_problem(argv[1])) return EXIT_FAILURE;

    fprintf(stderr,
            "Problema: %d jobs, %d maquinas | ops: %d | threads: %d | reps: %d\n",
            prob.num_jobs, prob.num_machines, total_ops, num_threads, repeticoes);

    /* 2. Inicialização do lock OpenMP */
    omp_init_lock(&best_lock);
    omp_set_num_threads(num_threads);

    /* 3. Profundidade e níveis de expansão (lidos uma única vez) */
    int g_depth, g_expand;
    {
        char *e;
        e = getenv("BB_DEPTH");
        if (e) {
            g_depth = atoi(e);
        } else if (total_ops <= 20) {
            g_depth = 0;
        } else if (total_ops <= 36) {
            g_depth = 5;
        } else {
            g_depth = 4;
        }
        g_depth_limit = g_depth;

        e = getenv("BB_EXPAND");
        if (e) {
            g_expand = atoi(e);
        } else {
            g_expand = 1;
            long est = prob.num_jobs;
            int  want = num_threads * 4;
            while (est < want && g_expand < 6) {
                g_expand++;
                est *= prob.num_jobs;
            }
        }
    }

    fprintf(stderr, "BB_DEPTH=%d | BB_EXPAND=%d\n", g_depth, g_expand);

    /* ════════════════════════════════════════════════════════════════════════
     * CICLO DE MEDIÇÃO — N repetições, sem output para consola
     * O tempo de cada repetição INCLUI:
     *   - cálculo do upper bound sequencial (pior caso)
     *   - expansão do work pool
     *   - criação, execução e término das threads (parallel for)
     * ════════════════════════════════════════════════════════════════════════ */
    double total_time = 0.0;
    int makespan_seq  = 0;   /* makespan do caso sequencial (pior caso) */

    for (int r = 0; r < repeticoes; r++) {

        /* Reset do estado global a cada repetição */
        global_best = INT_MAX;
        memset(best_starts, 0, sizeof(best_starts));

        /* ── INÍCIO DA MEDIÇÃO DE TEMPO ── */
        double t_start = omp_get_wtime();

        /* 4. Upper bound inicial = makespan sequencial puro (pior caso).
         *    Exactamente o mesmo algoritmo do jobshop_seq:
         *    percorre ops em série, cursor avança com cada duração. */
        makespan_seq = sequential_upper_bound();

        /* 5. Expansão BFS do nodo raiz para gerar o work pool */
        expand_work_pool(g_expand);

        /* ════════════════════════════════════════════════════════════════
         * CÓDIGO EXECUTADO PELAS THREADS — parallel for + dynamic
         *
         * Variáveis partilhadas SÓ DE LEITURA:
         *   prob, total_ops, g_depth_limit,
         *   work_pool[0..work_pool_size-1], work_pool_size
         *
         * Variáveis partilhadas LEITURA/ESCRITA:
         *   global_best (volatile int) — lido sem lock; escrito com best_lock
         *   best_starts (int[][])      — escrito com best_lock
         *   best_lock   (omp_lock_t)   — exclusão mútua
         *
         * Variáveis LOCAIS de cada thread (privadas):
         *   i (int), s (BBState)
         * ════════════════════════════════════════════════════════════════ */
        #pragma omp parallel for schedule(dynamic, 1)                              \
            shared(work_pool, work_pool_size, global_best, best_starts, best_lock, \
                   prob, total_ops, g_depth_limit)                                 \
            default(none)
        for (int i = 0; i < work_pool_size; i++) {
            BBState s = work_pool[i];
            if (lower_bound(&s) < global_best)
                bb_solve(s, 0);
        }
        /* Barreira implícita — todas as threads terminaram aqui */

        /* ── FIM DA MEDIÇÃO DE TEMPO ── */
        double t_end = omp_get_wtime();
        total_time += (t_end - t_start);
    }

    /* ════════════════════════════════════════════════════════════════════════
     * CÓDIGO FINAL (sequencial, após todas as repetições)
     * ════════════════════════════════════════════════════════════════════════ */
    double media = total_time / repeticoes;

    fprintf(stderr, "Makespan sequencial (pior caso): %d\n", makespan_seq);
    fprintf(stderr, "Makespan paralelo   (B&B)      : %d\n", global_best);
    fprintf(stderr, "Melhoria                       : %d unidades (%.1f%%)\n",
            makespan_seq - global_best,
            100.0 * (makespan_seq - global_best) / makespan_seq);
    fprintf(stderr, "Tempo medio (%d reps): %.6f s\n", repeticoes, media);
    fprintf(stderr, "Tempo total          : %.6f s\n", total_time);

    if (validate_solution())
        fprintf(stderr, "Solucao valida\n");
    else {
        fprintf(stderr, "Solucao INVALIDA!\n");
        omp_destroy_lock(&best_lock);
        return EXIT_FAILURE;
    }

    if (!write_result(argv[2])) {
        omp_destroy_lock(&best_lock);
        return EXIT_FAILURE;
    }
    fprintf(stderr, "Resultado gravado em '%s'\n", argv[2]);

    /* Linha CSV para stdout — fácil de capturar em scripts
     * formato: PAR,<threads>,<reps>,<makespan_seq>,<makespan_par>,<media_s>,<total_s> */
    printf("PAR,%d,%d,%d,%d,%.6f,%.6f\n",
           num_threads, repeticoes, makespan_seq, global_best, media, total_time);

    omp_destroy_lock(&best_lock);
    return EXIT_SUCCESS;
}