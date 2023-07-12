#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(int argc, char* argv[])
{
  int fd[] = {3, 4};  
  pipe(fd);
  write(fd[1], "ping", 4);
  int pid = fork();
  int status;
  if (pid == 0) {
    // child
    int pid = getpid();
    char word1[10];
    read(fd[0], word1, 4);
    printf("%d: received %s\n", pid, word1);
    write(fd[1], "pong", 4);
    exit(0);
  } else {
    // parent
    int pid = getpid();
    wait(&status);
    char word2[10];
    if (status == 0) {
      read(fd[0], word2, 4);
      printf("%d: received %s\n", pid, word2);
    } else {
      exit(-1);
    }
  }
  exit(0);
}
