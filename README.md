# Util

## Boot xv6

搭建环境

## sleep

这里编写的是用户层面的 `util`, 直接调用内核态的系统调用 `sleep` 即可.

```C
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(int argc, char* argv[])
{
  if(argc < 2){
    fprintf(2, "Usage: sleep number of ticks...\n");
    exit(1);
  }
  sleep(atoi(argv[1]));
  exit(0);
}
```

## pingpong

使用两个管道进行父子进程通信, 需要注意的是如果管道的写端没有close, 那么管道中数据为空时对管道的读取将会阻塞. 因此对于不需要的管道描述符, 要尽可能早的关闭.

```c
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
```

## primes

primes 要求用并行的方式实现埃式筛. 可以先理解一下 C++ 版本的[埃式筛](https://www.acwing.com/activity/content/code/content/1257847/)实现思路 ，再对照着实现. 

[Bell Labs and CSP Threads](https://swtch.com/~rsc/thread/) 中指出了并行实现埃式筛的伪代码:

```
p = get a number from left neighbor
print p
loop:
    n = get a number from left neighbor
    if (p does not divide n)
        send n to right neighbor
```

一个生成过程可以将数字2、3、4、...、1000输入到管道的左端: 管道中的第一个过程消除2的倍数, 第二个过程消除3的倍数, 第三个过程消除5的倍数, 依此类推. 如下图所示:


![埃式筛](https://i.postimg.cc/pdMDmrDL/primes.png)

```c
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
                write(pf[1], &n, sizeof(int));  // 未筛掉的数由创建的子进程继续筛
        }
        close(pf[1]);
        wait((int*)0);  // 父进程要等待子进程结束
    } else if (pid == 0) {
        sive(pf);
    }
    exit(0);
}
```

## find

仿照 `user/ls.c` 来编写如何读取文件夹或者文件.

```c
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"

void my_print(char* str){  // 辅助函数1
  printf("%s\n", str);
}

char*
fmtname(char* path){  // 辅助函数2 - 规范化文件名
  static char buf[DIRSIZ + 1];
  char* ptr;

  // Find first character after last slash
  for(ptr=path+strlen(path); ptr >= path && *ptr != '/'; ptr--)
    ;
  ptr++;

  // Return blank-padded name.
  if(strlen(ptr) >= DIRSIZ)
    return ptr;
  memmove(buf, ptr, strlen(ptr));
  memset(buf+strlen(ptr), ' ', DIRSIZ-strlen(ptr));
  return buf;
}

char* strcat(char* dest, const char* src){  // 辅助函数3 - 拼接字符串
  char* ptr = dest;

  while(*ptr != '\0') ptr++;

  while(*src != '\0'){
    *ptr = *src;
    ptr++;
    src++;
  }

  *ptr = '\0';
  return dest;
}

void removeEndSpace(char* str){  // 辅助函数4 - 去除末尾空格
  int cnt = 0;
  for (int i = 0; str[i]; i ++ ){
    if (str[i] != ' ') str[cnt ++ ] = str[i];
  }
  str[cnt] = '\0';
}

void find(char* path, char* file){
  char buf[512], *p;
  int fd;
  struct dirent de;
  struct stat st;

  if((fd = open(path, 0)) < 0){
    fprintf(2, "find: cannot open %s\n", path);
    return;
  }

  if(fstat(fd, &st) < 0){
    fprintf(2, "find: cannot stat %s\n", path);
    close(fd);
    return;
  }
  // st.type 一定是文件夹
  if(st.type == T_FILE){
    fprintf(2, "find: <path> <file>");
    close(fd);
    return;
  }

  if(strlen(path) + 1 + DIRSIZ + 1 > sizeof(buf)){
    printf("find: path too long\n");
    close(fd);
    return;
  }

  strcpy(buf, path);
  p = buf + strlen(buf);
  *p++ = '/';
  while(read(fd, &de, sizeof(de)) == sizeof(de)){
    if(de.inum == 0) continue;
    memmove(p, de.name, DIRSIZ);
    p[DIRSIZ] = 0;
    char* filename = fmtname(buf);
    removeEndSpace(filename);

    if(stat(buf, &st) < 0){
      printf("find: cannot stat %s\n", buf);
      continue;
    }
    if(st.type == T_FILE){  // 是文件，判断是否是dest_file
      if(strcmp(filename, file) == 0){  // 是
        // printf("current file(dir) is %s %d %d\n", filename, strcmp(filename, "."), strlen(filename));
        printf("%s\n", buf);
        // close(fd);
        // exit(0);  // success
      }else {  // 不是，继续寻找
        continue;
      }
    }else if(st.type == T_DIR){
      if (*filename == '.') continue;
      find(buf, file);
    }
  }

  close(fd);
}

int
main(int argc, char* argv[]){

  if(argc < 2){
    fprintf(2, "find: find search_path dest_file...\n");
    exit(1);
  }
  find(argv[1], argv[2]);

  // TODO
  // in xargs, there is a bug in find.c: when type "sh < xargstest.sh", the output is
  // $ $ $ $ $ $ hello
  // hello
  // hello
  // $ $
  exit(0);
}
```


## xargs

见代码注释.

```c
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
```
