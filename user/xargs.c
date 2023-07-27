#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/param.h"
#include <stddef.h>


int main(int argc, char* argv[]) {
    char stdin_buf[512];
    read(0, stdin_buf, sizeof stdin_buf);


    // debug: output the argv
    // for (int i = 0; i < argc; i ++ ) printf("argv[%d] is %s, ", i, argv[i]);
    // printf("\n");

    // debug: output the buf
    // printf("the content of buf is %s\n", stdin_buf);

    char* arguments[MAXARG];
    int count = 0;
    char word[512];
    for (int i = 0, j = 0; stdin_buf[i] != '\0'; i ++ , j ++ ) {
        if (stdin_buf[i] == ' ' || stdin_buf[i] == '\n') {
           word[j] = '\0';
           arguments[count] = malloc(strlen(word) + 1);
           strcpy(arguments[count], word);
           j = -1;
           count ++ ;

        } else {
            word[j] = stdin_buf[i];
        }

    }

    // debug: output the arguments
    // for (int i = 0; i < count; i ++ ) printf("arguments[%d] is %s, length is %d\n", i, arguments[i], strlen(arguments[i]));
    
    

    // get the latter length of the argv
    int len = 0;
    for (int i = 1; argv[i] != NULL; i ++ ) len ++ ;
    
    // debug: test the output len
    // printf("%d\n", len);
    
    // get the command
    char* command = "";
    strcpy(command, argv[1]);
    // debug: test output command
    // printf("command is %s\n", command);

    // combine the former argv and the latter argv
    char* new_arg[count + len];
    for (int i = 0; i < len; i ++ ) {
        new_arg[i] = malloc(strlen(argv[i + 1] + 1));
        if (new_arg[i] == NULL) {
            printf("failed to allocate memory\n");
            exit(1);
        }
        strcpy(new_arg[i], argv[i + 1]);
    }

    for (int i = 0; i < count; i ++ ) {
        new_arg[len + i] = malloc(strlen(arguments[i] + 1));
        if (new_arg[len + i] == NULL) {
            printf("failed to allocate memory\n");
            exit(1);
        }
        strcpy(new_arg[len + i], arguments[i]);
    }
    new_arg[len + count] = 0;  // set end flag 0 for the new_arg
   
    // debug: test output new_arg
    // for (int i = 0; new_arg[i] != NULL; i ++ ) printf("new_arg[%d] is %s\n", i, new_arg[i]);
    

    // free memory
    for (int i = 0; i < count; i ++ ) free(arguments[i]);
    for (int i = 0; i < count + len; i ++ ) free(new_arg[i]);


    // exec
    
    int pid, status;
    pid = fork();

    if (pid == 0) {  // child
        exec(command, new_arg);
        printf("exec failed\n");
        exit(1);
    } else {
        // printf("parent waiting\n");
        wait(&status);
        // printf("the child exited with status %d\n", status);
    }
   

    // TODO
    // I don't know why when I type the "echo 1 2 | xargs echo bye", the stdout isn't correct
    exit(0);
}
