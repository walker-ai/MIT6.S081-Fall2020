# Lab: system calls

## System call tracing

实现一个系统调用追踪函数 `trace()`, 接受一个参数 `mask`, 用二进制位来表示追踪哪些系统调用.

首先在 `kernel/proc.h` 下新建数据字段 `mask`, 并在 `sys_trace()` 的实现中实现参数的保存

```c
struct proc {
  ... 
  int mask;  // trace 系统调用参数
};

// kernel/sysproc.c
uint64
sys_trace(void)  // add sys_trace
{
  int mask;
  if(argint(0, &mask) < 0)  // get the mask
    return -1;
  
  myproc()->mask = mask;
  return 0;
}
```

根据提示, 需要修改 `fork` 函数, 因为 `fork` 的时候 `mask` 变量也要传递到子进程中:

```c
// kernel/proc.c
int 
fork(void)
{
  ...
  pid = np->pid;
  np->mask = p->mask;  // add here, copy the trace mask from parent to the child process
  ...
  return pid;
}
```

在 `kernel/syscall.c` 中考虑如何实现 `trace` 系统调用.

```c
// kernel/syscall.c

void
syscall(void)
{
  int num;
  struct proc *p = myproc();

  num = p->trapframe->a7;
  if(num > 0 && num < NELEM(syscalls) && syscalls[num]) {
    p->trapframe->a0 = syscalls[num]();

    // if the syscall was set int the mask then print
    if (p->mask >> num & 1) {  // 判断mask的第num位是否为1
      printf("%d: syscall %s -> %d\n", p->pid, syscall_name[num], p->trapframe->a0);
    }
  } else {
    printf("%d %s: unknown sys call %d\n",
            p->pid, p->name, num);
    p->trapframe->a0 = -1;
  }
}
```

除此之外, 还需要在各个文件中添加一些参数和定义以及函数的声明, 详情见 commit 信息或者 hints.

## Sysinfo

实现一个打印系统信息的系统调用. 该调用主要打印两个信息, 一个是空闲内存大小(可用内存字节数), 另一个是状态不是 UNUSED 的进程数量. 


首先在 `kernel/kalloc.c` 中添加一个函数 `freemem_collect` 用于获取空闲内存大小:

```c
// kernel/kalloc.c

uint64
freemem_collect(void)
{
  uint64 freemem_size = 0;
  struct run *current_block = kmem.freelist;  // kmem.freelist为内存块的链表头部，一个内存块为一页，一页是4096字节

  while(current_block != 0){
    freemem_size += PGSIZE;
    current_block = current_block->next;
  }
  return freemem_size;
}
```

再在 `kernel/proc.c` 中添加一个函数 `nproc_collect` 用于获取可用进程数量.

```c
// kernel/proc.c

uint64
nproc_collect(void)
{
  struct proc* p;
  uint64 cnt = 0;
  for(p = proc; p < &proc[NPROC]; p++){
    if(p->state != UNUSED) cnt++;
  }
  return cnt;
}
```

接着, 由于是实现系统调用, 需要添加相应系统调用号、添加系统调用函数的声明等, 详情见 commit 信息或 hints. 最后实现 `sys_sysinfo` 函数:

```c
// kernel/sysproc.c

uint64
sys_sysinfo(void)
{
  uint64 addr;  // addr is a user virtual address, pointing to a struct sysinfo
  struct sysinfo info;

  if(argaddr(0, &addr))  // get the addr
    return -1;

  info.nproc = nproc_collect();
  info.freemem = freemem_collect();

  if(copyout(myproc()->pagetable, addr, (char*)&info, sizeof(info)) < 0)  // copy sysinfo back to user space
    return -1;

  return 0;
}

```
