ن#include "headers.h"
#include <signal.h>
#include <math.h>


#define TOTAL_MEMORY_BYTES 1024   /* exactly as requested */
#define MIN_BLOCK_BYTES    1


#define MAX_ORDER 10  

typedef struct Block {
    int start;              
    int size;               
    int free;
    struct Block* next;
} Block;

static Block* free_list[MAX_ORDER + 1];
static FILE* memlog = NULL;


typedef struct PendingNode {
    PCB* p;
    struct PendingNode* next;
} PendingNode;

static PendingNode* pending_head = NULL;

 
int msgq_id;
int received_processes = 0;
int finished_processes = 0;
int process_count = 0;
int first_arr_proc;
Queue* Ready_Queue;
PCB* running_process = NULL;
FILE* logfile;


float total_runtime = 0;
float total_TA = 0;
float total_WTA = 0;
float total_waiting_time = 0;
float* WTA_values;


static int g_algorithm = 0;  

void HPF();
void SJF();
void RR(int quantum);

static int is_power_of_two(int x) { return x && ((x & (x - 1)) == 0); }

static int next_power_of_two(int x) {
    if (x <= 1) return 1;
    int p = 1;
    while (p < x) p <<= 1;
    return p;
}


static int size_to_index(int size) {
    if (size >= TOTAL_MEMORY_BYTES) return 0;
    if (size <= 1) return MAX_ORDER;
    int s = TOTAL_MEMORY_BYTES;
    int idx = 0;
    while (s > size) {
        s >>= 1;
        idx++;
    }
    return idx;
}


static int index_to_size(int idx) {
    return TOTAL_MEMORY_BYTES >> idx;
}


static void freelist_insert(Block* b) {
    int idx = size_to_index(b->size);
    b->free = 1;
    b->next = free_list[idx];
    free_list[idx] = b;
}


static Block* freelist_pop(int idx) {
    Block* b = free_list[idx];
    if (!b) return NULL;
    free_list[idx] = b->next;
    b->next = NULL;
    b->free = 0;
    return b;
}


static void buddy_init() {
    for (int i = 0; i <= MAX_ORDER; i++) free_list[i] = NULL;
    Block* initial = (Block*)malloc(sizeof(Block));
    initial->start = 0;
    initial->size = TOTAL_MEMORY_BYTES;
    initial->free = 1;
    initial->next = NULL;
    freelist_insert(initial);
}

static int buddy_allocate_bytes(int requested_bytes) {
    if (requested_bytes < MIN_BLOCK_BYTES) requested_bytes = MIN_BLOCK_BYTES;
    int need = next_power_of_two(requested_bytes);
    if (need > TOTAL_MEMORY_BYTES) return -1;

    int target_idx = size_to_index(need);

   
    int idx = target_idx;
    while (idx >= 0 && !free_list[idx]) idx--;
    if (idx < 0) return -1;

    
    while (idx < target_idx) {
        Block* big = freelist_pop(idx);
        if (!big) return -1; 
        int half = big->size / 2;

        Block* left = (Block*)malloc(sizeof(Block));
        left->start = big->start;
        left->size  = half;
        left->free  = 1;
        left->next  = NULL;

        Block* right = (Block*)malloc(sizeof(Block));
        right->start = big->start + half;
        right->size  = half;
        right->free  = 1;
        right->next  = NULL;

        free(big);

        freelist_insert(right);
        freelist_insert(left);

        idx++; /* move down */
    }

    Block* alloc = freelist_pop(target_idx);
    if (!alloc) return -1;
    int start_addr = alloc->start;
    free(alloc);
    return start_addr;
}


static void buddy_free_bytes(int start, int size_pow2) {
    if (size_pow2 < MIN_BLOCK_BYTES) size_pow2 = MIN_BLOCK_BYTES;
    int size = next_power_of_two(size_pow2);
    int idx = size_to_index(size);

    
    Block* blk = (Block*)malloc(sizeof(Block));
    blk->start = start;
    blk->size  = size;
    blk->free  = 1;
    blk->next  = NULL;
    freelist_insert(blk);

    while (idx > 0) {
        int block_size = index_to_size(idx);
        int buddy_addr = ( (start / block_size) % 2 == 0 )
                         ? start + block_size
                         : start - block_size;

      
        Block **prev = &free_list[idx], *cur = free_list[idx], *buddy = NULL;
        while (cur) {
            if (cur->start == buddy_addr && cur->free && cur->size == block_size) {
                buddy = cur;
                break;
            }
            prev = &cur->next;
            cur = cur->next;
        }
        if (!buddy) break;

        
        *prev = buddy->next;
        free(buddy);

        
        prev = &free_list[idx]; cur = free_list[idx];
        while (cur) {
            if (cur->start == start && cur->size == block_size && cur->free) {
                *prev = cur->next;
                free(cur);
                break;
            }
            prev = &cur->next;
            cur = cur->next;
        }

        
        start = (buddy_addr < start) ? buddy_addr : start;
        size  = block_size * 2;
        idx--; 

        Block* merged = (Block*)malloc(sizeof(Block));
        merged->start = start;
        merged->size  = size;
        merged->free  = 1;
        merged->next  = NULL;
        freelist_insert(merged);
    }
}


static void log_memory_event(int time, int pid, const char* action, int start, int size) {
    int end = start + size - 1;
    fprintf(memlog, "At time %d %s %d bytes for process %d from %d to %d\n",
            time, action, size, pid, start, end);
    fflush(memlog);
}


static void pending_push(PCB* p) {
    PendingNode* node = (PendingNode*)malloc(sizeof(PendingNode));
    node->p = p;
    node->next = NULL;
    if (!pending_head) {
        pending_head = node;
    } else {
        PendingNode* t = pending_head;
        while (t->next) t = t->next;
        t->next = node;
    }
}


static void try_allocate_pending() {
    PendingNode *prev = NULL, *cur = pending_head;
    while (cur) {
        PCB* p = cur->p;

         
        if (p->memsize <= 0) {
            if (!prev) pending_head = cur->next;
            else prev->next = cur->next;
            PendingNode* to_free = cur;
            cur = cur->next;
            free(to_free);

            if (g_algorithm == 1) enqueue(Ready_Queue, p);
            else if (g_algorithm == 2) enqueue_SJF(Ready_Queue, p);
            else enqueue_RR(Ready_Queue, p);
            continue;
        }

        int addr = buddy_allocate_bytes(p->memsize);
        if (addr != -1) {
            p->mem_start = addr;
            log_memory_event(getClk(), p->id, "allocated", addr, p->memsize);

            if (!prev) {
                pending_head = cur->next;
            } else {
                prev->next = cur->next;
            }
            PendingNode* to_free = cur;
            cur = cur->next;
            free(to_free);

            if (g_algorithm == 1) enqueue(Ready_Queue, p);
            else if (g_algorithm == 2) enqueue_SJF(Ready_Queue, p);
            else enqueue_RR(Ready_Queue, p);
        } else {
            prev = cur;
            cur = cur->next;
        }
    }
}

/* ----------------------- IPC & Logging (yours) ------------------------ */
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
        WTA = ((int)(WTA * 100 + 0.5)) / 100.0; // Round to 2 dp
        fprintf(logfile, "At time %d process %d %s arr %d total %d remain %d wait %d TA %d WTA %.2f\n",
                time, process->id , state, arr, total, remain, wait, TA, WTA);
    } else {
        fprintf(logfile, "At time %d process %d %s arr %d total %d remain %d wait %d\n",
                time, process->id, state, arr, total, remain, wait);
    }
    fflush(logfile);
}

/* ----------------------- Termination handler -------------------------- */
void Check_Process_Termination() {
    termination_msgbuff message;
    int rec_val = msgrcv(msgq_id, &message, sizeof(message) - sizeof(long), 5, IPC_NOWAIT);

    if (rec_val != -1) {
        if (running_process && running_process->pid == message.pid) {
            running_process->state = FINISHED;
            running_process->remaining_time = 0;

            
            Log_Process_Event(running_process, "finished");

            
            if (running_process->memsize > 0 && running_process->mem_start >= 0 && memlog) {
                buddy_free_bytes(running_process->mem_start, running_process->memsize);
                log_memory_event(getClk(), running_process->id, "freed",
                                 running_process->mem_start, running_process->memsize);
            }

            
            int TA = getClk() - running_process->arrival_time;
            float WTA = (float)TA / running_process->runtime;
            total_TA += TA;
            total_WTA += WTA;
            total_waiting_time += (running_process->start_time - running_process->arrival_time);
            WTA_values[finished_processes] = WTA;

            free(running_process);
            running_process = NULL;
            finished_processes++;

         
            try_allocate_pending();
        }
    }
}

/* ----------------------- Receive process (arrival) -------------------- */
PCB* Receive_process() {
    process_msgbuff message;
    PCB* rec_process = (PCB*)malloc(sizeof(PCB));
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
        rec_process->memsize = message.process.memsize; /* requires updated headers.h */
        rec_process->mem_start = -1;

        total_runtime += message.process.runningtime;

        
        if (rec_process->memsize <= 0) {
            return rec_process; 
        }

        
        int addr = buddy_allocate_bytes(rec_process->memsize);
        if (addr == -1) {
            pending_push(rec_process);
            return NULL;  
        }
        rec_process->mem_start = addr;
        if (memlog) {
            log_memory_event(getClk(), rec_process->id, "allocated", addr, rec_process->memsize);
        }

        return rec_process;
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
    if (total_time <= 0) total_time = 1; 

    float cpu_utilization = ((float)total_runtime / (float)total_time) * 100.0f;
    cpu_utilization = Round(cpu_utilization);

    float avg_WTA = total_WTA / process_count;
    avg_WTA = Round(avg_WTA);

    float avg_waiting = (float)total_waiting_time / process_count;
    avg_waiting = Round(avg_waiting);

    float sum_squared_diff = 0.0f;
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


int main(int argc, char* argv[]) {
    if (argc < 3) {
        printf("Too few arguments to scheduler\n");
        exit(-1);
    }

    Ready_Queue = createQueue(100);

    
    initClk();

    g_algorithm = atoi(argv[1]);
    process_count = atoi(argv[2]);

    
    WTA_values = (float*)malloc(sizeof(float) * process_count);
    if (WTA_values == NULL) {
        perror("Error allocating memory for WTA_values");
        exit(-1);
    }

    int quantum = 0;
    if (g_algorithm == 3) quantum = atoi(argv[3]);

    
    CreateMessageQueue();

    
    logfile = fopen("scheduler.log", "w");
    if (!logfile) {
        perror("Error opening log file");
        exit(-1);
    }
    fprintf(logfile, "#At time x process y state arr w total z remain y wait k\n");

    
    memlog = fopen("memory.log", "w");
    if (!memlog) {
        perror("Error opening memory log file");
        exit(-1);
    }
    fprintf(memlog, "#At time x allocated y bytes for process z from i to j\n");
    fprintf(memlog, "#At time x freed y bytes for process z from i to j\n");

    /* Init buddy allocator (safe even if no process uses it) */
    buddy_init();

    /* Run selected algorithm */
    switch (g_algorithm) {
        case 1:
            HPF();
            break;
        case 2:
            SJF();
            break;
        case 3:
            RR(quantum);
            break;
        default:
            printf("Invalid algorithm selected.\n");
            exit(-1);
    }

    
    ComputePerformanceMetrics();
    fclose(logfile);
    fclose(memlog);
    free(WTA_values);
    msgctl(msgq_id, IPC_RMID, NULL);
    destroyClk(true);

    return 0;
}

/* ----------------------- Schedulers (yours) --------------------------- */
void HPF() {
    while (finished_processes < process_count) {
        
        PCB* current_p = Receive_process();
        while (current_p) {
            enqueue(Ready_Queue, current_p); /* enqueue based on priority */
            current_p = Receive_process();
        }

   
        try_allocate_pending();

        
        Check_Process_Termination();

        
        if (running_process == NULL && !isEmptyQ(Ready_Queue)) {
            running_process = dequeue(Ready_Queue);
            if (running_process->state == READY) {
                int pid = fork();
                if (pid == 0) {
                    char remaining_time_str[16];
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
            } else if (running_process->state == BLOCKED) {
                kill(running_process->pid, SIGCONT);
                running_process->state = RUNNING;
                running_process->last_run = getClk();
                Log_Process_Event(running_process, "resumed");
            }
        }

        
        if (running_process != NULL && !isEmptyQ(Ready_Queue)) {
            PCB* highest_priority_process = front(Ready_Queue);
            if (highest_priority_process->priority < running_process->priority) {
                kill(running_process->pid, SIGSTOP);
                int current_time = getClk();
                int elapsed_time = current_time - running_process->last_run;
                running_process->remaining_time -= elapsed_time;
                running_process->state = BLOCKED;
                Log_Process_Event(running_process, "stopped");
                enqueue(Ready_Queue, running_process);

                running_process = dequeue(Ready_Queue);
                if (running_process->state == READY) {
                    int pid = fork();
                    if (pid == 0) {
                        char remaining_time_str[16];
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
                } else if (running_process->state == BLOCKED) {
                    kill(running_process->pid, SIGCONT);
                    running_process->state = RUNNING;
                    running_process->last_run = getClk();
                    Log_Process_Event(running_process, "resumed");
                }
            }
        }
    }
}

void SJF() {
    while (finished_processes < process_count) {
        
        PCB* current_p = Receive_process();
        while (current_p) {
            enqueue_SJF(Ready_Queue, current_p);
            current_p = Receive_process();
        }

        
        try_allocate_pending();

        
        Check_Process_Termination();

        
        if (running_process != NULL) {
            int current_time = getClk();
            int elapsed_time = current_time - running_process->last_run;
            if (elapsed_time > 0) {
                running_process->remaining_time -= elapsed_time;
                if (running_process->remaining_time < 0) running_process->remaining_time = 0;
                running_process->last_run = current_time;
            }
        }

        
        if (running_process != NULL && !isEmptyQ(Ready_Queue)) {
            PCB* shortest_process = front(Ready_Queue);
            int current_time = getClk();
            if (shortest_process->arrival_time <= current_time &&
                shortest_process->remaining_time < running_process->remaining_time &&
                running_process->remaining_time > 0) {
                kill(running_process->pid, SIGSTOP);
                running_process->state = BLOCKED;
                Log_Process_Event(running_process, "stopped");
                enqueue_SJF(Ready_Queue, running_process);
                running_process = NULL;
            }
        }

        
        if (running_process == NULL && !isEmptyQ(Ready_Queue)) {
            int current_time = getClk();
            PCB* next_process = front(Ready_Queue);
            if (next_process && next_process->arrival_time <= current_time) {
                running_process = dequeue(Ready_Queue);
                if (running_process->state == READY) {
                    int pid = fork();
                    if (pid == 0) {
                        char remaining_time_str[16];
                        sprintf(remaining_time_str, "%d", running_process->remaining_time);
                        execl("./process", "process", remaining_time_str, NULL);
                        perror("Error executing process");
                        exit(-1);
                    } else if (pid < 0) {
                        perror("Error in fork");
                    } else {
                        running_process->pid = pid;
                        running_process->state = RUNNING;
                        if (running_process->start_time == -1) {
                            running_process->start_time = getClk();
                        }
                        running_process->last_run = getClk();
                        Log_Process_Event(running_process, "started");
                    }
                } else if (running_process->state == BLOCKED) {
                    kill(running_process->pid, SIGCONT);
                    running_process->state = RUNNING;
                    running_process->last_run = getClk();
                    Log_Process_Event(running_process, "resumed");
                }
            }
        }
    }
}

void RR(int quantum) {
    while (finished_processes < process_count) {
        
        PCB* current_p = Receive_process();
        while (current_p) {
            enqueue_RR(Ready_Queue, current_p); /* FCFS */
            current_p = Receive_process();
        }

        
        try_allocate_pending();

      
        Check_Process_Termination();

        int current_time = getClk();

        
        if (running_process != NULL) {
            int elapsed_time = current_time - running_process->last_run;
            if (elapsed_time >= quantum) {
                kill(running_process->pid, SIGSTOP);
                running_process->remaining_time -= elapsed_time;
                if (running_process->remaining_time > 0) {
                    running_process->state = BLOCKED;
                    enqueue_RR(Ready_Queue, running_process);
                }
                Log_Process_Event(running_process, "stopped");
                running_process = NULL;
            }
        }

        /* Dispatch if idle */
        if (running_process == NULL && !isEmptyQ(Ready_Queue)) {
            running_process = dequeue(Ready_Queue);
            if (running_process->state == READY) {
                int pid = fork();
                if (pid == 0) {
                    char remaining_time_str[16];
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
            } else if (running_process->state == BLOCKED) {
                kill(running_process->pid, SIGCONT);
                running_process->state = RUNNING;
                running_process->last_run = getClk();
                Log_Process_Event(running_process, "resumed");
            }
        }

   
        sleep(1);
    }
}
