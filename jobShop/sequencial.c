/*
 * Job-Shop Scheduling Problem - Implementação Sequencial
 * Algoritmo: Greedy com heurística de prioridade (Shortest Processing Time + ERT)
 *
 * Estratégia:
 *   - Mantém uma fila de operações "prontas" (cujo predecessor no job já terminou).
 *   - Iterativamente, seleciona a operação pronta com menor tempo de processamento
 *     (SPT) que pode começar mais cedo na sua máquina.
 *   - Garante o cumprimento das duas restrições:
 *       1) Cada operação só começa após a anterior do mesmo job terminar.
 *       2) Cada máquina só executa uma operação de cada vez.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ─── Limites estáticos (sem apontadores internos) ─────────────────────── */
#define MAX_JOBS       100
#define MAX_MACHINES   100
#define MAX_OPS_PER_JOB 100

/* ─── Estruturas de dados ───────────────────────────────────────────────── */

/* Uma operação pertence a um job e deve correr numa máquina específica */
typedef struct {
    int machine;    /* índice da máquina */
    int duration;   /* duração da operação */
    int start;      /* tempo de início (resultado) */
} Operation;

/* Problema completo: arrays estáticos, sem apontadores internos */
typedef struct {
    int num_jobs;
    int num_machines;
    int num_ops[MAX_JOBS];                         /* nº operações por job */
    Operation ops[MAX_JOBS][MAX_OPS_PER_JOB];      /* ops[job][op_idx]    */
} Problem;

/* ─── Leitura do ficheiro de entrada ────────────────────────────────────── */

/*
 * Formato esperado (igual ao enunciado):
 *   <num_jobs> <num_machines>
 *   Para cada job, numa linha: pares <machine> <duration> ...
 *
 * Exemplo gg03.jss:
 *   3 3
 *   0 3 1 2 2 2
 *   0 2 2 1 1 4
 *   1 4 2 3 0 1
 */
int read_problem(const char *filename, Problem *p) {
    FILE *f = fopen(filename, "r");
    if (!f) {
        fprintf(stderr, "Erro: não foi possível abrir '%s'\n", filename);
        return 0;
    }

    if (fscanf(f, "%d %d", &p->num_jobs, &p->num_machines) != 2) {
        fprintf(stderr, "Erro: cabeçalho inválido\n");
        fclose(f);
        return 0;
    }

    for (int j = 0; j < p->num_jobs; j++) {
        int op = 0;
        /* Lê pares machine/duration até fim de linha ou de ficheiro */
        int machine, duration;
        /* Determinamos o nº de operações lendo pares; usamos num_machines
         * como limite superior (num_ops pode == num_machines no caso geral) */
        for (int o = 0; o < p->num_machines; o++) {
            if (fscanf(f, "%d %d", &machine, &duration) != 2) break;
            p->ops[j][op].machine  = machine;
            p->ops[j][op].duration = duration;
            p->ops[j][op].start    = 0;
            op++;
        }
        p->num_ops[j] = op;
    }

    fclose(f);
    return 1;
}

/* ─── Algoritmo de Escalonamento Greedy ─────────────────────────────────── */

/*
 * greedy_schedule():
 *   Mantém:
 *     machine_free[m] = instante em que a máquina m fica disponível
 *     next_op[j]      = índice da próxima operação a escalonar do job j
 *     job_free[j]     = instante em que o job j fica disponível (após a sua
 *                       última operação escalonada terminar)
 *
 *   Em cada iteração seleciona, de entre todas as operações prontas
 *   (next_op[j] < num_ops[j]), a que tem o menor "earliest start time" na
 *   respectiva máquina, desempatando por menor duração (SPT).
 *   Atribui o tempo de início e avança os contadores.
 *
 *   Complexidade: O(J * N^2) onde N = total de operações — suficiente para
 *   instâncias de benchmark de referência.
 */
int greedy_schedule(Problem *p) {
    int machine_free[MAX_MACHINES];
    int job_free[MAX_JOBS];
    int next_op[MAX_JOBS];

    memset(machine_free, 0, sizeof(machine_free));
    memset(job_free,     0, sizeof(job_free));
    memset(next_op,      0, sizeof(next_op));

    /* Total de operações a escalonar */
    int total_ops = 0;
    for (int j = 0; j < p->num_jobs; j++)
        total_ops += p->num_ops[j];

    int scheduled = 0;

    while (scheduled < total_ops) {
        /* Encontra a melhor operação pronta segundo a heurística ERT+SPT */
        int best_job  = -1;
        int best_op   = -1;
        int best_start = -1;
        int best_dur   = -1;

        for (int j = 0; j < p->num_jobs; j++) {
            int o = next_op[j];
            if (o >= p->num_ops[j]) continue; /* job concluído */

            int m = p->ops[j][o].machine;
            int dur = p->ops[j][o].duration;

            /* Earliest possible start: máximo entre máquina livre e job livre */
            int earliest = job_free[j] > machine_free[m]
                           ? job_free[j] : machine_free[m];

            /* Heurística: prefere o menor earliest start;
             * em caso de empate, prefere a menor duração (SPT) */
            if (best_job == -1
                || earliest < best_start
                || (earliest == best_start && dur < best_dur)) {
                best_job   = j;
                best_op    = o;
                best_start = earliest;
                best_dur   = dur;
            }
        }

        if (best_job == -1) {
            fprintf(stderr, "Erro interno: nenhuma operação pronta encontrada\n");
            return 0;
        }

        /* Atribui tempo de início */
        int j = best_job;
        int o = best_op;
        int m = p->ops[j][o].machine;

        p->ops[j][o].start = best_start;

        /* Actualiza disponibilidade */
        machine_free[m]   = best_start + p->ops[j][o].duration;
        job_free[j]        = best_start + p->ops[j][o].duration;
        next_op[j]++;
        scheduled++;
    }

    return 1;
}

/* ─── Cálculo do Makespan ───────────────────────────────────────────────── */

int compute_makespan(const Problem *p) {
    int makespan = 0;
    for (int j = 0; j < p->num_jobs; j++) {
        for (int o = 0; o < p->num_ops[j]; o++) {
            int end = p->ops[j][o].start + p->ops[j][o].duration;
            if (end > makespan) makespan = end;
        }
    }
    return makespan;
}

/* ─── Validação da solução ──────────────────────────────────────────────── */

/*
 * Verifica as duas restrições do problema:
 *   1) Ordem dentro de cada job.
 *   2) Sem sobreposição de operações na mesma máquina.
 * Devolve 1 se válido, 0 se inválido.
 */
int validate(const Problem *p) {
    /* Restrição 1: ordem das operações dentro de cada job */
    for (int j = 0; j < p->num_jobs; j++) {
        for (int o = 1; o < p->num_ops[j]; o++) {
            int prev_end = p->ops[j][o-1].start + p->ops[j][o-1].duration;
            if (p->ops[j][o].start < prev_end) {
                fprintf(stderr,
                    "VIOLAÇÃO restrição 1: job %d, op %d começa em %d "
                    "mas op %d ainda não terminou (end=%d)\n",
                    j, o, p->ops[j][o].start, o-1, prev_end);
                return 0;
            }
        }
    }

    /* Restrição 2: sem sobreposição na mesma máquina */
    /* Para cada par de operações na mesma máquina, verifica intervalos */
    for (int j1 = 0; j1 < p->num_jobs; j1++) {
        for (int o1 = 0; o1 < p->num_ops[j1]; o1++) {
            for (int j2 = j1; j2 < p->num_jobs; j2++) {
                int start_o2 = (j2 == j1) ? o1 + 1 : 0;
                for (int o2 = start_o2; o2 < p->num_ops[j2]; o2++) {
                    if (p->ops[j1][o1].machine != p->ops[j2][o2].machine)
                        continue;
                    int s1 = p->ops[j1][o1].start;
                    int e1 = s1 + p->ops[j1][o1].duration;
                    int s2 = p->ops[j2][o2].start;
                    int e2 = s2 + p->ops[j2][o2].duration;
                    /* Sobreposição: !(e1 <= s2 || e2 <= s1) */
                    if (!(e1 <= s2 || e2 <= s1)) {
                        fprintf(stderr,
                            "VIOLAÇÃO restrição 2: máquina %d, "
                            "job%d/op%d [%d,%d[ sobrepõe job%d/op%d [%d,%d[\n",
                            p->ops[j1][o1].machine,
                            j1, o1, s1, e1, j2, o2, s2, e2);
                        return 0;
                    }
                }
            }
        }
    }
    return 1;
}

/* ─── Escrita do ficheiro de saída ──────────────────────────────────────── */

/*
 * Formato de saída (cf. Anexo II):
 *   <makespan>
 *   Para cada job, numa linha: tempos de início separados por espaço
 */
int write_result(const char *filename, const Problem *p, int makespan) {
    FILE *f = fopen(filename, "w");
    if (!f) {
        fprintf(stderr, "Erro: não foi possível criar '%s'\n", filename);
        return 0;
    }

    fprintf(f, "%d\n", makespan);
    for (int j = 0; j < p->num_jobs; j++) {
        for (int o = 0; o < p->num_ops[j]; o++) {
            if (o > 0) fprintf(f, " ");
            fprintf(f, "%d", p->ops[j][o].start);
        }
        fprintf(f, "\n");
    }

    fclose(f);
    return 1;
}

/* ─── Main ──────────────────────────────────────────────────────────────── */

/* ─── Utilitário: tempo em segundos (wall-clock) ───────────────────────── */
static double now_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

/* ─── Main ──────────────────────────────────────────────────────────────── */

/*
 * Uso: ./jobshop_seq <entrada> <saida> [repeticoes]
 *
 *   <entrada>    — ficheiro .jss com o problema
 *   <saida>      — ficheiro de saída com o escalonamento
 *   [repeticoes] — número de execuções para medição (default: 1)
 *
 * Durante a medição do tempo não há qualquer output para a consola.
 * O tempo reportado é a MÉDIA das repetições.
 * O resultado escrito no ficheiro de saída é o da última repetição.
 */
int main(int argc, char *argv[]) {
    if (argc < 3 || argc > 4) {
        fprintf(stderr, "Uso: %s <entrada> <saida> [repeticoes]\n", argv[0]);
        return EXIT_FAILURE;
    }

    int repeticoes = (argc == 4) ? atoi(argv[3]) : 1;
    if (repeticoes < 1) repeticoes = 1;

    /* ── Leitura do problema (uma única vez, fora da medição) ── */
    Problem p_orig;
    memset(&p_orig, 0, sizeof(p_orig));
    if (!read_problem(argv[1], &p_orig)) return EXIT_FAILURE;

    fprintf(stderr, "Problema: %d jobs, %d maquinas | repeticoes: %d\n",
            p_orig.num_jobs, p_orig.num_machines, repeticoes);

    /* ── Medição de tempo: N repetições sem output para consola ── */
    double total_time = 0.0;
    Problem p_last;
    memset(&p_last, 0, sizeof(p_last));

    for (int r = 0; r < repeticoes; r++) {
        /* Copia o problema original para não acumular estado */
        Problem p;
        memcpy(&p, &p_orig, sizeof(Problem));

        double t0 = now_sec();

        if (!greedy_schedule(&p)) return EXIT_FAILURE;

        double t1 = now_sec();
        total_time += (t1 - t0);

        /* Guarda a última execução para validação e escrita */
        memcpy(&p_last, &p, sizeof(Problem));
    }

    double media = total_time / repeticoes;

    /* ── Makespan ── */
    int makespan = compute_makespan(&p_last);

    /* ── Output para consola (só após medição) ── */
    fprintf(stderr, "Makespan: %d\n", makespan);
    fprintf(stderr, "Tempo medio (%d reps): %.6f s\n", repeticoes, media);
    fprintf(stderr, "Tempo total           : %.6f s\n", total_time);

    /* ── Validação ── */
    if (!validate(&p_last)) {
        fprintf(stderr, "Solucao INVALIDA!\n");
        return EXIT_FAILURE;
    }
    fprintf(stderr, "Solucao valida\n");

    /* ── Escrita do resultado ── */
    if (!write_result(argv[2], &p_last, makespan)) return EXIT_FAILURE;
    fprintf(stderr, "Resultado gravado em '%s'\n", argv[2]);

    /* ── Linha de resultado em CSV para stdout (fácil de capturar) ──
     * formato: SEQ,<repeticoes>,<makespan>,<media_s>,<total_s>         */
    printf("SEQ,%d,%d,%.6f,%.6f\n",
           repeticoes, makespan, media, total_time);

    return EXIT_SUCCESS;
}