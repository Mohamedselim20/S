#include "headers.h"
#include <signal.h>
#include <math.h>

// Global variables
int msgq_id;
int received_processes = 0;
int finished_processes = 0;
int process_count = 0;
int first_arr_proc;
Queue* Ready_Queue;
PCB* running_process = NULL;
FILE* logfile;

// Variables for performance 
float total_runtime = 0;
float total_TA = 0;
float total_WTA = 0;
float total_waiting_time = 0;
float* WTA_values;  

void HPF();
void SJF();
void RR(int quantum);

void CreateMessageQueue() {
    key_t keyid = ftok("keyfile", 65);
    msgq_id = msgget(keyid, 0666 | IPC_CREAT);

    if (msgq_id == -1) {
        perror("Error in creating message queue");
        exit(-1);
    }
}

void Log_Process_Event(PCB* process, const char* state) {
    int time = getClk();
    int arr = process->arrival_time;
    int total = process->runtime;
    int remain = process->remaining_time;
    int wait = time - arr - (process->runtime - process->remaining_time);

    if (strcmp(state, "finished") == 0) {
        int TA = time - arr;
        float WTA = (float)TA / process->runtime;
        WTA = ((int)(WTA * 100 + 0.5)) / 100.0;

        fprintf(logfile, "At time %d process %d %s arr %d total %d remain %d wait %d TA %d WTA %.2f\n",
                time, process->id , state, arr, total, remain, wait, TA, WTA);
    } else {
        fprintf(logfile, "At time %d process %d %s arr %d total %d remain %d wait %d\n",
                time, process->id, state, arr, total, remain, wait);
    }

    fflush(logfile);
}

void Check_Process_Termination() {
    termination_msgbuff message;
    int rec_val = msgrcv(msgq_id, &message, sizeof(message) - sizeof(long), 5, IPC_NOWAIT);

    if (rec_val != -1) {
        if (running_process && running_process->pid == message.pid) {
            running_process->state = FINISHED;
            running_process->remaining_time = 0;
            Log_Process_Event(running_process, "finished");

            int TA = getClk() - running_process->arrival_time;
            float WTA = (float)TA / running_process->runtime;
            total_TA += TA;
            total_WTA += WTA;
            total_waiting_time += (running_process->start_time - running_process->arrival_time);
            WTA_values[finished_processes] = WTA;

            free(running_process);
            running_process = NULL;
            finished_processes++;
        } else {
            printf("Unknown process with PID %d reported termination.\n", message.pid);
        }
    }
}

PCB* Receive_process() {
    process_msgbuff message;
    PCB* rec_process = malloc(sizeof(PCB));

    int rec_val = msgrcv(msgq_id, &message, sizeof(message.process), 1, IPC_NOWAIT);

    if (rec_val != -1) {
        if (received_processes == 0) {
            first_arr_proc = message.process.arrivaltime;
        }
        received_processes++;

        rec_process->id = message.process.id;
        rec_process->arrival_time = message.process.arrivaltime;
        rec_process->priority = message.process.priority;
        rec_process->runtime = message.process.runningtime;
        rec_process->waiting_time = 0;
        rec_process->state = READY;
        rec_process->remaining_time = message.process.runningtime;
        rec_process->start_time = -1;
        rec_process->last_run = -1;

        total_runtime += message.process.runningtime;
    } else {
        free(rec_process);
        rec_process = NULL;
    }
    return rec_process;
}

float Round(float var) {
    return ((int)(var * 100 + 0.5)) / 100.0;
}

void ComputePerformanceMetrics() {
    FILE* perf_file = fopen("scheduler.perf", "w");
    if (!perf_file) {
        perror("Error opening performance file");
        exit(-1);
    }

    int total_time = getClk() - first_arr_proc;
    float cpu_utilization = ((float)total_runtime / total_time) * 100;
    cpu_utilization = Round(cpu_utilization);

    float avg_WTA = total_WTA / process_count;
    avg_WTA = Round(avg_WTA);

    float avg_waiting = (float)total_waiting_time / process_count;
    avg_waiting = Round(avg_waiting);

    float sum_squared_diff = 0;
    for (int i = 0; i < process_count; i++) {
        float diff = WTA_values[i] - avg_WTA;
        sum_squared_diff += diff * diff;
    }
    float std_WTA = sqrt(sum_squared_diff / process_count);
    std_WTA = Round(std_WTA);

    fprintf(perf_file, "CPU utilization = %.2f%%\n", cpu_utilization);
    fprintf(perf_file, "Avg WTA = %.2f\n", avg_WTA);
    fprintf(perf_file, "Avg Waiting = %.2f\n", avg_waiting);
    fprintf(perf_file, "Std WTA = %.2f\n", std_WTA);

    fclose(perf_file);
}

void SJF() {
    while (finished_processes < process_count) {
        PCB* current_p = Receive_process();
        while (current_p) {
            enqueue_SJF(Ready_Queue, current_p);
            current_p = Receive_process();
        }

        Check_Process_Termination();

        if (running_process == NULL && !isEmptyQ(Ready_Queue)) {
            running_process = dequeue(Ready_Queue);
            if (running_process->state == READY) {
                int pid = fork();
                if (pid == 0) {
                    char remaining_time_str[10];
                    sprintf(remaining_time_str, "%d", running_process->remaining_time);
                    execl("./process", "process", remaining_time_str, NULL);
                    perror("Error executing process");
                    exit(-1);
                } else if (pid < 0) {
                    perror("Error in fork");
                } else {
                    running_process->pid = pid;
                    running_process->state = RUNNING;
                    running_process->start_time = getClk();
                    running_process->last_run = getClk();
                    Log_Process_Event(running_process, "started");
                }
            }
        }

        if (running_process != NULL && running_process->remaining_time <= 0) {
            free(running_process);
            running_process = NULL;
            finished_processes++;
        }

        sleep(1);
    }
}
