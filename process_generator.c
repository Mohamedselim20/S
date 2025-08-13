#include "headers.h"

void clearResources(int);

int msq_id;

int main(int argc, char* argv[]) {
    signal(SIGINT, clearResources);

    // Initialize message queue
    key_t keyid = ftok("keyfile", 65);
    msq_id = msgget(keyid, 0666 | IPC_CREAT);
    if (msq_id == -1) {
        perror("Error in creating the message queue");
        exit(-1);
    }

    // Read processes from file
    FILE* file = fopen("processes.txt", "r");
    if (!file) {
        perror("Error opening processes file");
        exit(-1);
    }

    int Proc_num = 0;
    char buffer[200];
    fgets(buffer, sizeof(buffer), file); // Skip the first line

    while (fgets(buffer, sizeof(buffer), file)) {
        Proc_num++;
    }
    fclose(file);

    file = fopen("processes.txt", "r");
    processData* procs = malloc(sizeof(processData) * Proc_num);
    if (!procs) {
        perror("Failed to allocate memory");
        exit(-1);
    }

    fgets(buffer, sizeof(buffer), file); // Skip the first line again
    int count = 0;

    // Now read memsize as well
    while (fscanf(file, "%d\t%d\t%d\t%d\t%d",
                  &procs[count].id,
                  &procs[count].arrivaltime,
                  &procs[count].runningtime,
                  &procs[count].priority,
                  &procs[count].memsize) == 5) {
        count++;
    }
    fclose(file);

    // Ask the user for the chosen scheduling algorithm
    int algorithm;
    printf("Choose a scheduling algorithm:\n");
    printf("1. Highest Priority First (HPF).\n");
    printf("2. Shortest Job Next (SJF).\n");
    printf("3. Round Robin (RR).\n");
    scanf("%d", &algorithm);

    int timeSlot = 0;
    if (algorithm == 3) {
        printf("Enter the time slot for Round Robin: ");
        scanf("%d", &timeSlot);
    }

    // Start the clock process
    int pid_c = fork();
    if (pid_c == -1) {
        perror("Error in creating clock process");
    } else if (pid_c == 0) {
        execl("./clk.out", "clk", NULL);
        perror("Error executing clock");
        exit(-1);
    }

    // Initialize clock
    initClk();

    // Start the scheduler process
    int pid_s = fork();
    if (pid_s == -1) {
        perror("Error in creating scheduler process");
    } else if (pid_s == 0) {
        // Child process - Scheduler
        char algo_str[10], proc_str[10], time_str[10];
        snprintf(algo_str, sizeof(algo_str), "%d", algorithm);
        snprintf(proc_str, sizeof(proc_str), "%d", Proc_num);

        if (algorithm == 3) {
            snprintf(time_str, sizeof(time_str), "%d", timeSlot);
            execl("./scheduler.out", "scheduler", algo_str, proc_str, time_str, NULL);
        } else {
            execl("./scheduler.out", "scheduler", algo_str, proc_str, NULL);
        }
        perror("Error executing scheduler");
        exit(-1);
    }

    // Parent process continues

    // Send processes to the scheduler at the appropriate time
    int snd_num = 0;
    while (snd_num < Proc_num) {
        int current_time = getClk();
        if (procs[snd_num].arrivaltime <= current_time) {
            printf("Sending process ID %d at time %d\n", procs[snd_num].id, current_time);
            
            process_msgbuff msg;
            msg.mtype = 1;
            msg.process = procs[snd_num];

            if (msgsnd(msq_id, &msg, sizeof(msg.process), !IPC_NOWAIT) == -1) {
                perror("Error in sending message");
            }
            snd_num++;
        } else {
            sleep(1);
        }
    }

    // Wait for the scheduler to finish
    waitpid(pid_s, NULL, 0);

    // Clean up resources
    msgctl(msq_id, IPC_RMID, NULL);
    destroyClk(false);
    free(procs);

    return 0;
}

void clearResources(int signum) {
    msgctl(msq_id, IPC_RMID, NULL);
    destroyClk(false);
    exit(0);
}
