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


  int ticks = atoi(argv[1]);

  while(ticks > 0){
    ticks = sleep(ticks);
    if(ticks < 0) exit(-1);
  }

  exit(0);

}
