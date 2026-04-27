/*
 * Job-Shop Scheduling Problem - Implementação Sequencial
 *
 * Algoritmo: Travessia sequencial pura (pior caso / baseline)
 *
 * O enunciado descreve assim esta abordagem:
 *
 *   "O método de atribuição de tempos de início mais simples de usar é
 *    fazer uma travessia sequencial para todas as operações de todos os
 *    trabalhos, e atribuir um tempo de início a todas as operações,
 *    respeitando a sua duração, INDEPENDENTEMENTE da máquina ou trabalho
 *    a que pertençam. Esta abordagem corresponde ao caso sequencial, que
 *    também é o mais longo possível, sem intervalos, em que se executa
 *    uma operação e apenas uma operação em cada instante de tempo."
 *
 * Comportamento:
 *   - Percorre os jobs por ordem (j=0, j=1, ...) e dentro de cada job
 *     as operações por ordem (op=0, op=1, ...).
 *   - Cada operação começa imediatamente quando a anterior termina.
 *   - Não há qualquer consideração de máquinas nem de sobreposição.
 *   - Resultado: makespan = soma de TODAS as durações de todas as operações.
 *
 * Para o exemplo gg03 (3 jobs × 3 operações):
 *   durações: 3,2,2  2,1,4  4,3,1
 *   soma total: 3+2+2 + 2+1+4 + 4+3+1 = 22
 *   makespan = 22
 *
 * Esta é a solução de referência (pior caso). A versão paralela usa esta
 * como ponto de partida e melhora o makespan através do escalonamento real
 * com múltiplas threads.
 *
 * Compilação:
 *   gcc -O2 -Wall -o jobshop_seq jobshop_seq.c
 *
 * Uso:
 *   ./jobshop_seq <entrada> <saida> [repeticoes]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ─── Limites estáticos (sem apontadores internos) ──────────────────────── */
#define MAX_JOBS        100
#define MAX_MACHINES    100
#define MAX_OPS_PER_JOB 100

/* ─── Estruturas de dados ───────────────────────────────────────────────── */

typedef struct {
    int machine;   /* índice da máquina (não usado no algoritmo sequencial) */
    int duration;  /* duração da operação */
    int start;     /* tempo de início (resultado)                           */
} Operation;

typedef struct {
    int num_jobs;
    int num_machines;
    int num_ops[MAX_JOBS];
    Operation ops[MAX_JOBS][MAX_OPS_PER_JOB];
} Problem;

/* ─── Leitura do ficheiro de entrada ────────────────────────────────────── */

/*
 * Formato (igual ao enunciado):
 *   <num_jobs> <num_machines>
 *   Para cada job: pares <machine> <duration> ...
 *
 * Exemplo gg03.jss:
 *   3 3
 *   0 3 1 2 2 2
 *   0 2 2 1 1 4
 *   1 4 2 3 0 1
 */
static int read_problem(const char *filename, Problem *p) {
    FILE *f = fopen(filename, "r");
    if (!f) {
        fprintf(stderr, "Erro: nao foi possivel abrir '%s'\n", filename);
        return 0;
    }
    if (fscanf(f, "%d %d", &p->num_jobs, &p->num_machines) != 2) {
        fprintf(stderr, "Erro: cabecalho invalido\n");
        fclose(f); return 0;
    }
    for (int j = 0; j < p->num_jobs; j++) {
        int op = 0, machine, duration;
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

/* ─── Algoritmo Sequencial Puro ─────────────────────────────────────────── */

/*
 * sequential_schedule():
 *
 *   Percorre TODOS os jobs por ordem e, dentro de cada job, TODAS as
 *   operações por ordem. Cada operação começa logo após a anterior terminar,
 *   independentemente da máquina a que pertence.
 *
 *   Um único contador `cursor` avança linearmente:
 *     start[j][o] = cursor
 *     cursor     += duration[j][o]
 *
 *   Resultado final: cursor == soma de todas as durações == makespan.
 */
static void sequential_schedule(Problem *p) {
    int cursor = 0;

    for (int j = 0; j < p->num_jobs; j++) {
        for (int o = 0; o < p->num_ops[j]; o++) {
            p->ops[j][o].start = cursor;
            cursor += p->ops[j][o].duration;
        }
    }
}

/* ─── Cálculo do Makespan ────────────────────────────────────────────────── */

static int compute_makespan(const Problem *p) {
    int makespan = 0;
    for (int j = 0; j < p->num_jobs; j++)
        for (int o = 0; o < p->num_ops[j]; o++) {
            int end = p->ops[j][o].start + p->ops[j][o].duration;
            if (end > makespan) makespan = end;
        }
    return makespan;
}

/* ─── Escrita do ficheiro de saída ──────────────────────────────────────── */

/*
 * Formato de saída (cf. Anexo II do enunciado):
 *   <makespan>
 *   Para cada job, uma linha com os tempos de início separados por espaço.
 */
static int write_result(const char *filename, const Problem *p, int makespan) {
    FILE *f = fopen(filename, "w");
    if (!f) {
        fprintf(stderr, "Erro: nao foi possivel criar '%s'\n", filename);
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

/* ─── Utilitário: tempo wall-clock em segundos ──────────────────────────── */

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
 *   [repeticoes] — numero de execucoes para medicao de tempo (default: 1)
 *
 * Durante a medicao do tempo nao ha qualquer output para a consola.
 * O tempo reportado e a MEDIA das repeticoes.
 * O resultado escrito no ficheiro e o da ultima repeticao.
 *
 * Output para stdout (formato CSV, para captura em scripts):
 *   SEQ,<repeticoes>,<makespan>,<tempo_medio_s>,<tempo_total_s>
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

    /* ── Medição de tempo: N repetições, sem output para consola ── */
    double total_time = 0.0;
    Problem p_last;
    memset(&p_last, 0, sizeof(p_last));

    for (int r = 0; r < repeticoes; r++) {
        Problem p;
        memcpy(&p, &p_orig, sizeof(Problem));

        double t0 = now_sec();
        sequential_schedule(&p);          /* algoritmo sequencial puro */
        double t1 = now_sec();

        total_time += (t1 - t0);
        memcpy(&p_last, &p, sizeof(Problem));
    }

    double media = total_time / repeticoes;

    /* ── Makespan (= soma de todas as durações) ── */
    int makespan = compute_makespan(&p_last);

    /* ── Output para consola (só após medição) ── */
    fprintf(stderr, "Makespan (pior caso): %d\n", makespan);
    fprintf(stderr, "Tempo medio (%d reps): %.6f s\n", repeticoes, media);
    fprintf(stderr, "Tempo total          : %.6f s\n", total_time);

    /* ── Escrita do resultado ── */
    if (!write_result(argv[2], &p_last, makespan)) return EXIT_FAILURE;
    fprintf(stderr, "Resultado gravado em '%s'\n", argv[2]);

    /* ── Linha CSV para stdout ── */
    printf("SEQ,%d,%d,%.6f,%.6f\n", repeticoes, makespan, media, total_time);

    return EXIT_SUCCESS;
}