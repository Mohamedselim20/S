#include "headers.h"
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <unistd.h>
#include <time.h>

int msgq_id;

void terminateProcess(int signum)
{
    destroyClk(false);
    exit(0);
}
int last_time;

void cont (int);

int main(int argc, char *argv[])
{
    initClk();


    signal(SIGCONT,cont);
    // Create message queue


    key_t keyid = ftok("keyfile", 65);
    msgq_id = msgget(keyid, 0666);

    if (msgq_id == -1) {
        perror("Error in getting message queue in process");
        exit(-1);
    }

    // Check if the remaining time is passed as an argument
    if (argc < 2)
    {
        fprintf(stderr, "Usage: %s remaining_time\n", argv[0]);
        exit(-1);
    }

    int remaining_time = atoi(argv[1]);
    last_time = getClk();

    // printf("Process %d started at time %d with remaining time %d\n", getpid(), last_time, remaining_time);

    while (remaining_time > 0)
    {
        int current_time = getClk();
        if (current_time > last_time)
        {
            remaining_time -= current_time - last_time;
            last_time = current_time;
            // printf("Process %d at time %d, remaining time %d\n", getpid(), current_time, remaining_time);
        }
    }

    // // Process has finished execution
    // printf("Process %d has finished execution at time %d.\n", getpid(), getClk());




    // Send message to scheduler to notify termination
    termination_msgbuff message;
    message.mtype = 5; 
    message.pid = getpid();
    message.status = 1; // 1 indicates process has finished

    int send_val = msgsnd(msgq_id, &message, sizeof(message) - sizeof(long), !IPC_NOWAIT);

    if (send_val == -1) {
        perror("Error in sending termination message from process");
    } else {
        printf("Process %d sent termination message to scheduler.\n", getpid());
    }

    destroyClk(false);
    exit(0);
}
void cont(int signum)
{

    last_time=getClk();
    
}
