#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"

void my_print(char* str){
  printf("%s\n", str);
}

char*
fmtname(char* path){
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

char* strcat(char* dest, const char* src){
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

  printf("%d\n", st.type);
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
    if(stat(buf, &st) < 0){
      printf("find: cannot stat %s\n", buf);
      continue;
    }
    if(st.type == T_FILE){  // 是文件，判断是否是dest_file
      if(strcmp(fmtname(buf), file) == 0){  // 是
        printf("%s\n", buf);
        close(fd);
        exit(0);  // success
      }else {  // 不是，继续寻找
        continue;
      }
    }else if(st.type == T_DIR){
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
  exit(0);
}
