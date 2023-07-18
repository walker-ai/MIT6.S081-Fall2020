#include "kernel/types.h"
#include "user/user.h"
#include "kernel/stat.h"


void sive(int pL[]) {
    // pL is left neighbor
    int pR[2];
    pipe(pR);

    close(pL[1]);
    int prime, n, pid;

    if (read(pL[0], &prime, sizeof(int)) == 0) {
        close(pL[0]);
        exit(0);
    }

    printf("prime %d\n", prime);

    pid = fork();
    if (pid > 0) {
        close(pR[0]);
        while (read(pL[0], &n, sizeof(int))) {
            if (n % prime) write(pR[1], &n, sizeof(int));
        }
        close(pL[0]);
        close(pR[1]);
        wait((int*)0);
        exit(0);

    } else if (pid == 0) {
        sive(pR);
        close(pL[0]);
    }
}

int main(int argc, char* argv[]) {
    int pf[2];
    pipe(pf);
    

    int prime, n, pid;
    pid = fork();

    prime = 2;
    printf("prime: %d\n", prime);

    if (pid > 0) {
        close(pf[0]);
        for (n = 2; n <= 35; n ++ ) {
            if (n % prime)
                write(pf[1], &n, sizeof(int));
        }
        close(pf[1]);
        wait((int*)0);
    } else if (pid == 0) {
        sive(pf);
    }
    exit(0);
}
