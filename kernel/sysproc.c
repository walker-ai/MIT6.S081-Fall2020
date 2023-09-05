#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

uint64
sys_exit(void)
{
  int n;
  if(argint(0, &n) < 0)
    return -1;
  exit(n);
  return 0;  // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return fork();
}

uint64
sys_wait(void)
{
  uint64 p;
  if(argaddr(0, &p) < 0)
    return -1;
  return wait(p);
}

uint64
sys_sbrk(void)
{
  int addr;
  int n;

  if(argint(0, &n) < 0)
    return -1;
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

uint64
sys_sleep(void)
{
  int n;
  uint ticks0;

  if(argint(0, &n) < 0)
    return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(myproc()->killed){
      release(&tickslock);
      return -1;
    }
    backtrace();
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

uint64
sys_kill(void)
{
  int pid;

  if(argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

uint64
sys_sigalarm()
{
  int interval;
  uint64 handler;

  if (argint(0, &interval) < 0) {
    return -1;  // 获取参数0 - interval失败
  }

  if (argaddr(1, &handler) < 0) {
    return -1;  // 获取参数1 - handler失败
  }
  // 将interval和handler存储到proc中
  // printf("interval = %d\n", interval);
  // printf("handler = %p\n", handler);

  myproc()->alarm_interval = interval;
  myproc()->handler = handler;
  myproc()->ticks_number = ticks;  // 全局变量ticks表示当前的ticks，赋值给调用sigalarm时的ticks编号

  // 每个tick都会触发一次硬件时钟中断，相应的中断处理程序需要自己编写例如 usertrap
  
  // 每n个tick强制调用处理函数，然后等待一段时间

  // 调用了多少次usertrap，就是过去了多少次tick

  return 0;
}

uint64
sys_sigreturn(void) 
{
  return 0;
}