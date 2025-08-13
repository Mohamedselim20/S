#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#define null 0

struct processData {
    int arrivaltime;
    int priority;
    int runningtime;
    int id;
    int memsize;  // New field for Phase 2
};

int main(int argc, char *argv[]) {
    FILE *pFile;
    pFile = fopen("processes.txt", "w");
    int no;
    struct processData pData;

    printf("Please enter the number of processes you want to generate: ");
    scanf("%d", &no);

    srand(time(null));

    // Aligned header
    fprintf(pFile, "%-5s %-8s %-8s %-8s %-8s\n",
            "id", "arrival", "runtime", "priority", "memsize");

    pData.arrivaltime = 1;
    for (int i = 1; i <= no; i++) {
        pData.id = i;
        pData.arrivaltime += rand() % 11; // processes arrive in order
        pData.runningtime = rand() % 30 + 1; // avoid 0 runtime
        pData.priority = rand() % 11;
        pData.memsize = (rand() % 256) + 1; // between 1 and 256 bytes

        fprintf(pFile, "%-5d %-8d %-8d %-8d %-8d\n",
                pData.id,
                pData.arrivaltime,
                pData.runningtime,
                pData.priority,
                pData.memsize);
    }

    fclose(pFile);
    return 0;
}
