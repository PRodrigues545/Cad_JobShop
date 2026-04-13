#include <stdio.h>
#include <stdlib.h>

#define MAX_JOBS 100
#define MAX_MACHINES 100
#define MAX_OPERATIONS 100

typedef struct {
    int machine;
    int duration;
    int startTime;
} Operation;

typedef struct {
    int numOperations;
    Operation operations[MAX_OPERATIONS];
} Job;

// Variáveis globais
int numJobs;
int numMachines;
Job jobs[MAX_JOBS];

int machineEndTime[MAX_MACHINES];
int jobEndTime[MAX_JOBS];

// Função para ler o ficheiro de entrada
void readInput(const char *filename) {
    FILE *file = fopen(filename, "r");
    if (!file) {
        printf("Erro ao abrir ficheiro de entrada.\n");
        exit(1);
    }

    fscanf(file, "%d %d", &numJobs, &numMachines);

    for (int i = 0; i < numJobs; i++) {
        jobs[i].numOperations = numMachines;

        for (int j = 0; j < numMachines; j++) {
            fscanf(file, "%d %d",
                   &jobs[i].operations[j].machine,
                   &jobs[i].operations[j].duration);

            jobs[i].operations[j].startTime = 0;
        }
    }

    fclose(file);
}

// Inicializar tempos
void initializeTimes() {
    for (int i = 0; i < numMachines; i++)
        machineEndTime[i] = 0;

    for (int i = 0; i < numJobs; i++)
        jobEndTime[i] = 0;
}

// Algoritmo sequencial (greedy)
void computeSchedule() {
    for (int i = 0; i < numJobs; i++) {
        for (int j = 0; j < jobs[i].numOperations; j++) {

            int m = jobs[i].operations[j].machine;
            int duration = jobs[i].operations[j].duration;

            int start = jobEndTime[i] > machineEndTime[m] ?
                        jobEndTime[i] : machineEndTime[m];

            int end = start + duration;

            jobs[i].operations[j].startTime = start;

            jobEndTime[i] = end;
            machineEndTime[m] = end;
        }
    }
}

// Calcular makespan
int computeMakespan() {
    int makespan = 0;

    for (int i = 0; i < numJobs; i++) {
        if (jobEndTime[i] > makespan)
            makespan = jobEndTime[i];
    }

    return makespan;
}

// Escrever output
void writeOutput(const char *filename, int makespan) {
    FILE *file = fopen(filename, "w");
    if (!file) {
        printf("Erro ao escrever ficheiro de saída.\n");
        exit(1);
    }

    fprintf(file, "%d\n", makespan);

    for (int i = 0; i < numJobs; i++) {
        for (int j = 0; j < jobs[i].numOperations; j++) {
            fprintf(file, "%d ", jobs[i].operations[j].startTime);
        }
        fprintf(file, "\n");
    }

    fclose(file);
}

// Função principal
int main(int argc, char *argv[]) {

    if (argc != 3) {
        printf("Uso: %s input.jss output.txt\n", argv[0]);
        return 1;
    }

    readInput(argv[1]);
    initializeTimes();
    computeSchedule();

    int makespan = computeMakespan();

    writeOutput(argv[2], makespan);

    printf("Escalonamento concluído. Makespan = %d\n", makespan);

    return 0;
}