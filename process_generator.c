#include "headers.h"
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

void clearResources(int);
static int read_processes_file(const char* filename, processData** out, int* out_count);

int msq_id;

int main(int argc, char* argv[]) {
    signal(SIGINT, clearResources);

    // Create/Get message queue
    key_t keyid = ftok("keyfile", 65);
    msq_id = msgget(keyid, 0666 | IPC_CREAT);
    if (msq_id == -1) {
        perror("Error in creating the message queue");
        exit(EXIT_FAILURE);
    }

    // Read processes (including memsize) from file
    processData* procs = NULL;
    int Proc_num = 0;
    if (read_processes_file("processes.txt", &procs, &Proc_num) != 0) {
        fprintf(stderr, "Failed to read processes.txt\n");
        clearResources(0);
    }

    // Select scheduling algorithm
    int algorithm = 0;
    printf("Choose a scheduling algorithm:\n");
    printf("1. Highest Priority First (HPF).\n");
    printf("2. Shortest Job Next (SJF).\n");
    printf("3. Round Robin (RR).\n");
    fflush(stdout);
    if (scanf("%d", &algorithm) != 1 || algorithm < 1 || algorithm > 3) {
        fprintf(stderr, "Invalid algorithm choice.\n");
        clearResources(0);
    }

    int timeSlot = 0;
    if (algorithm == 3) {
        printf("Enter the time slot for Round Robin: ");
        fflush(stdout);
        if (scanf("%d", &timeSlot) != 1 || timeSlot <= 0) {
            fprintf(stderr, "Invalid time slot.\n");
            clearResources(0);
        }
    }

    // Fork clock
    int pid_c = fork();
    if (pid_c == -1) {
        perror("Error in forking clock process");
        clearResources(0);
    } else if (pid_c == 0) {
        execl("./clk.out", "clk", (char*)NULL);
        perror("Error executing clock");
        exit(EXIT_FAILURE);
    }

    // Initialize logical clock
    initClk();

    // Fork scheduler
    int pid_s = fork();
    if (pid_s == -1) {
        perror("Error in forking scheduler process");
        clearResources(0);
    } else if (pid_s == 0) {
        // Child -> exec scheduler.out with args: algo, proc_count, [timeslot]
        char algo_str[16], proc_str[16], ts_str[16];
        snprintf(algo_str, sizeof(algo_str), "%d", algorithm);
        snprintf(proc_str, sizeof(proc_str), "%d", Proc_num);

        if (algorithm == 3) {
            snprintf(ts_str, sizeof(ts_str), "%d", timeSlot);
            execl("./scheduler.out", "scheduler", algo_str, proc_str, ts_str, (char*)NULL);
        } else {
            execl("./scheduler.out", "scheduler", algo_str, proc_str, (char*)NULL);
        }
        perror("Error executing scheduler");
        exit(EXIT_FAILURE);
    }

    // Send processes to scheduler at arrival times
    int snd_idx = 0;
    while (snd_idx < Proc_num) {
        int now = getClk();

        // Send all processes that have arrival <= now (handles multiple same-time arrivals)
        while (snd_idx < Proc_num && procs[snd_idx].arrivaltime <= now) {
            process_msgbuff msg;
            msg.mtype = 1;                // agreed-on mtype for new process arrival
            msg.process = procs[snd_idx]; // includes memsize now

            if (msgsnd(msq_id, &msg, sizeof(msg.process), !IPC_NOWAIT) == -1) {
                perror("Error in msgsnd (process arrival)");
                // don't exit immediately; try next tick
            } else {
                printf("Sent process ID %d (mem=%d) at time %d\n",
                       procs[snd_idx].id, procs[snd_idx].memsize, now);
                fflush(stdout);
            }
            snd_idx++;
        }

        if (snd_idx < Proc_num) {
            // Wait a tick to advance time
            sleep(1);
        }
    }

    // Wait for scheduler to finish
    waitpid(pid_s, NULL, 0);

    // Cleanup
    msgctl(msq_id, IPC_RMID, NULL);
    destroyClk(false);
    free(procs);

    return 0;
}

void clearResources(int signum) {
    // Safe cleanup even if partially initialized
    if (msq_id > 0) msgctl(msq_id, IPC_RMID, NULL);
    destroyClk(false);
    exit(0);
}

/**
 * Reads processes from 'filename' into a dynamically allocated array.
 * Skips lines starting with '#'.
 * Expected columns (tab-separated): id, arrival, runtime, priority, memsize
 * Returns 0 on success, non-zero on failure.
 */
static int read_processes_file(const char* filename, processData** out, int* out_count) {
    FILE* f = fopen(filename, "r");
    if (!f) {
        perror("Error opening processes file");
        return -1;
    }

    // First pass: count valid (non-comment) lines
    char line[256];
    int count = 0;
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#') continue;
        // skip blank lines
        char* p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\n' || *p == '\0') continue;
        count++;
    }

    if (count == 0) {
        fclose(f);
        *out = NULL;
        *out_count = 0;
        return 0;
    }

    // Allocate array
    processData* arr = (processData*)malloc(sizeof(processData) * count);
    if (!arr) {
        fclose(f);
        perror("malloc");
        return -1;
    }

    // Second pass: parse values
    rewind(f);
    int idx = 0;
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#') continue;

        // skip blank
        char* p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\n' || *p == '\0') continue;

        // Expect 5 integers separated by tabs
        processData tmp;
        int matched = sscanf(p, "%d\t%d\t%d\t%d\t%d",
                             &tmp.id, &tmp.arrivaltime, &tmp.runningtime,
                             &tmp.priority, &tmp.memsize);
        if (matched == 5) {
            arr[idx++] = tmp;
        } else {
            fprintf(stderr, "Warning: malformed line ignored: %s", line);
        }
    }
    fclose(f);

    *out = arr;
    *out_count = idx;
    return 0;
}
