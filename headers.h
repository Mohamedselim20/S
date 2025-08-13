#ifndef HEADERS_H
#define HEADERS_H

#include <stdio.h>      // if you don't use scanf/printf change this include
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <sys/msg.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <string.h>
#include <math.h>
#include <stdbool.h>  // For standard bool, true, false

#define SHKEY 300

///==============================
// don't mess with this variable
int * shmaddr;                 
//===============================

int getClk()
{
    return *shmaddr;
}

/* Process info coming from process_generator to scheduler */
typedef struct {
    int arrivaltime;
    int priority;
    int runningtime;
    int id;
    int memsize;  /* NEW: memory size in bytes for Phase 2 */
} processData;

/* Message structure for process arrival */
typedef struct {
    long mtype;
    processData process;
} process_msgbuff;

/* Message structure for process termination */
typedef struct {
    long mtype;
    int pid;
    int status; // status = 1 means process has finished
} termination_msgbuff;

/* Clock initialization */
void initClk()
{
    int shmid = shmget(SHKEY, 4, 0444);
    while ((int)shmid == -1)
    {
        // Make sure that the clock exists
        printf("Wait! The clock not initialized yet!\n");
        sleep(1);
        shmid = shmget(SHKEY, 4, 0444);
    }
    shmaddr = (int *) shmat(shmid, (void *)0, 0);
}

/* Clock destroy */
void destroyClk(bool terminateAll)
{
    shmdt(shmaddr);
    if (terminateAll)
    {
        killpg(getpgrp(), SIGINT);
    }
}

/* Process states */
typedef enum {
    READY,
    RUNNING,
    BLOCKED,
    FINISHED
} ProcessState;

/* Process Control Block structure */
typedef struct {
    int pid;                 
    int arrival_time;      
    int runtime;           
    int priority;          
    int remaining_time;    
    int waiting_time;      
    int start_time;        
    ProcessState state;    
    int last_run;
    int id;   
    int memsize;       /* NEW: memory size in bytes */
    int mem_start;     /* NEW: starting address in memory */
} PCB;

/* ---------------------- Queue implementation ---------------------- */
#include <limits.h>
#include <stdlib.h>

/* A structure to represent a queue */
typedef struct {
    int front, rear, size;
    unsigned capacity;
    PCB** array;
} Queue;

/* Create a queue of given capacity */
Queue* createQueue(unsigned capacity) {
    Queue* queue = (Queue*)malloc(sizeof(Queue));
    queue->capacity = capacity;
    queue->front = queue->size = 0;
    queue->rear = capacity - 1;
    queue->array = (PCB**)malloc(queue->capacity * sizeof(PCB*));
    return queue;
}

/* Queue is full when size becomes equal to the capacity */
int isFull(Queue* queue) {
    return (queue->size == queue->capacity);
}

/* Queue is empty when size is 0 */
int isEmptyQ(Queue* queue) {
    return (queue->size == 0);
}

/* Enqueue for HPF (priority queue) */
void enqueue(Queue* queue, PCB* item) {
    if (isFull(queue)) {
        printf("Queue is full. Cannot enqueue item.\n");
        return;
    }

    int i;

    /* If the queue is empty, insert at position 0 */
    if (queue->size == 0) {
        queue->array[0] = item;
        queue->front = 0;
        queue->rear = 0;
        queue->size = 1;
        return;
    }

    /* Start from the last element and shift items to the right to make room */
    i = queue->size - 1;

    /* Adjust the condition based on priority and arrival time */
    while (i >= 0 && (queue->array[i]->priority > item->priority ||
          (queue->array[i]->priority == item->priority && queue->array[i]->arrival_time > item->arrival_time))) {
        queue->array[i + 1] = queue->array[i];
        i--;
    }

    /* Insert the new item at the correct position */
    queue->array[i + 1] = item;

    /* Update rear and size */
    queue->size++;
    queue->rear = queue->size - 1;
}

/* Enqueue for SJF (shortest remaining time first) */
void enqueue_SJF(Queue* queue, PCB* item) {
    if (isFull(queue)) {
        printf("Queue is full. Cannot enqueue item.\n");
        return;
    }

    int i;

    /* If the queue is empty, insert at position 0 */
    if (queue->size == 0) {
        queue->array[0] = item;
        queue->front = 0;
        queue->rear = 0;
        queue->size = 1;
        return;
    }

    /* Start from the last element and shift items to the right to make room */
    i = queue->size - 1;

    /* Adjust the condition based on remaining_time */
    while (i >= 0 && queue->array[i]->remaining_time > item->remaining_time) {
        queue->array[i + 1] = queue->array[i];
        i--;
    }

    /* Insert the new item at the correct position */
    queue->array[i + 1] = item;

    /* Update rear and size */
    queue->size++;
    queue->rear = queue->size - 1;
}

/* Enqueue for RR (FCFS) */
void enqueue_RR(Queue* queue, PCB* item) {
    if (isFull(queue)) {
        printf("Queue is full. Cannot enqueue item.\n");
        return;
    }

    queue->rear = (queue->rear + 1) % queue->capacity;
    queue->array[queue->rear] = item;
    queue->size++;
}

/* Dequeue from queue (front) */
PCB* dequeue(Queue* queue) {
    if (isEmptyQ(queue)) {
        printf("Queue is empty. Cannot dequeue item.\n");
        return NULL;
    }
    PCB* item = queue->array[queue->front];

    /* Shift all elements one position to the left */
    for (int i = 0; i < queue->size - 1; i++) {
        queue->array[i] = queue->array[i + 1];
    }

    queue->size--;
    queue->rear = queue->size - 1;

    /* Optional: Set the now-unused position to NULL */
    queue->array[queue->size] = NULL;

    /* If the queue is now empty, reset front and rear */
    if (queue->size == 0) {
        queue->front = 0;
        queue->rear = -1;
    }

    return item;
}

/* Get front of queue */
PCB* front(Queue* queue) {
    if (isEmptyQ(queue)) {
        return NULL;
    }
    return queue->array[queue->front];
}

/* Get rear of queue */
PCB* rear(Queue* queue) {
    if (isEmptyQ(queue)) {
        return NULL;
    }
    return queue->array[queue->rear];
}

#endif // HEADERS_H
