# vmprint

参照 `freewalk` 函数，不同的是需要给 `vmprint` 传入一个 `level` 参数表示当前页目录/页表的级别

```C
void
vmprint(pagetable_t pagetable, int level)
{
    level ++ ;
    // 共有512个条目
    for (int i = 0; i < 512; i ++ ){
        pte_t pte = pagetable[i];
        uint64 child = PTE2PA(pte);
        if((pte & PTE_V) && (pte & (PTE_R|PTE_W|PTE_X)) == 0){
            // 当前是顶级页目录 or 二级页目录
            for (int j = 0; j < level -1; j ++ )
                printf(".. ");
            printf("..%d: pte %p pa %p\n", i, pte, child);
            vmprint((pagetable_t)child, level);
        } else if (pte & PTE_V){
            printf(".. .. ..%d: pte %p pa %p\n", i, pte, child);
        }
    }
}
```

在 `defs.h` 中添加声明，以便 `exec.c` 中能调用：

```c
void            vmprint(pagetable_t, int);
```

然后在 `exec.c` 中进行调用：

```c
if (p->pid == 1) {
    printf("page table %p\n", p->pagetable);
    vmprint(p->pagetable, 0);
}

// 在 return argc 之前；经验证明添加顺序很重要
return argc
```

# kernel page table per process

xv6 中有一个单独的在内核中执行程序时的内核页表，也是所有进程共享的全局内核页表，当内核需要使用在系统调用中传递用户指针（例如，传递给 `write()` 的缓冲区指针）时，必须首先将指针转换成物理地址。因此为每个进程都维护一个内核页表是一个更好的选择，再在内核页表副本中维护一个关于内核栈的映射。

首先在 `proc.h` 的 `struct proc` 中添加一个字段

```c
struct proc {
  struct spinlock lock;
  // p->lock must be held when using these:
  enum procstate state;        // Process state
  struct proc *parent;         // Parent process
  void *chan;                  // If non-zero, sleeping on chan
  int killed;                  // If non-zero, have been killed
  int xstate;                  // Exit status to be returned to parent's wait
  int pid;                     // Process ID
  // these are private to the process, so p->lock need not be held.
  uint64 kstack;               // Virtual address of kernel stack
  uint64 sz;                   // Size of process memory (bytes)
  pagetable_t pagetable;       // User page table
  pagetable_t kernel_pagetable;// kernel pagetable per process       <-- add here
  struct trapframe *trapframe; // data page for trampoline.S
  struct context context;      // swtch() here to run process
  struct file *ofile[NOFILE];  // Open files
  struct inode *cwd;           // Current directory
  char name[16];               // Process name (debugging)
};
```

为一个新进程生成一个内核页表，参照初始化全局内核页表函数 `kvminit`，编写 `ukvminit`，并在 `alloc` 函数中进行调用，由于 `kvminit` 用到了 `kvmmap` 函数，作用是将虚拟地址和实际物理地址相映射起来，由于 `kvmmap` 内部是直接向全局内核页表 `kernel_pagetable` 添加条目，因此需要写一个辅助函数 `ukvmmap``：

```c
void
ukvmmap(pagetable_t pagetable, uint64 va, uint64 pa, uint64 sz, int perm)
{
    if (mappages(pagetable, va, sz, pa, perm) != 0)
        panic("ukvmmap");
}
```

编写 `ukvminit`：

```c
pagetable_t
ukvminit()
{
    pagetable_t pagetable = (pagetable_t) kalloc();
    memset(pagetable, 0, PGSIZE);

    // uart registers
    ukvmmap(pagetable, UART0, UART0, PGSIZE, PTE_R | PTE_W);
    // virtio mmio disk interface
    ukvmmap(pagetable, VIRTIO0, VIRTIO0, PGSIZE, PTE_R | PTE_W);
    // CLINT
    ukvmmap(pagetable, CLINT, CLINT, 0x10000, PTE_R | PTE_W);
    // PLIC
    ukvmmap(pagetable, PLIC, PLIC, 0x400000, PTE_R | PTE_W);
    // map kernel text executable and read-only.
    ukvmmap(pagetable, KERNBASE, KERNBASE, (uint64)etext-KERNBASE, PTE_R | PTE_X);
    // map kernel data and the physical RAM we'll make use of.
    ukvmmap(pagetable, (uint64)etext, (uint64)etext, PHYSTOP-(uint64)etext, PTE_R | PTE_W);
    // map the trampoline for trap entry/exit to
    // the highest virtual address in the kernel.
    ukvmmap(pagetable, TRAMPOLINE, (uint64)trampoline, PGSIZE, PTE_R | PTE_X);
    return pagetable;
}
```

添加到 `kernel/defs.h` 中，以便 `proc.c` 中 `alloc` 函数能够调用：

```c
void            ukvmmap(pagetable_t, uint64, uint64, uint64, int);
pagetable_t     ukvminit();
```

将 `procinit` 函数中映射内核栈的代码部分迁移至 `alloc` 中实现，`procinit` 中移除映射内核栈的部分：

```c
for (p = proc; p < &proc[NPROC]; p ++ ) {
    initlock(&p->lock, "proc");
    for(p = proc; p < &proc[NPROC]; p++) {
      initlock(&p->lock, "proc");
      // Allocate a page for the process's kernel stack.
      // Map it high in memory, followed by an invalid
      // guard page.


      /*
      char *pa = kalloc();
      if(pa == 0)
        panic("kalloc");
      uint64 va = KSTACK((int) (p - proc));
      kvmmap(va, (uint64)pa, PGSIZE, PTE_R | PTE_W);
      p->kstack = va;
      */
  }
  kvminithart();
}
```

在创建每个进程的过程中，实现内核栈的映射（这里的实现一定要在 `memset(&p->context, 0, sizeof(p->context));` 之前，否则调度函数 `scheduler` 无法进行进程上下文切换）：

```c
p->kernel_pagetable = ukvminit();  // 初始化进程的内核页表
if(p->kernel_pagetable == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
}

char* pa = kalloc();
if (pa == 0)
    panic("kalloc");
uint64 va = KSTACK((int)(p - proc));
ukvmmap(p->kernel_pagetable, va, (uint64)pa, PGSIZE, PTE_R | PTE_W);
p -> kstack = va;


// Set up new context to start executing at forkret,
// which returns to user space.
memset(&p->context, 0, sizeof(p->context));
p->context.ra = (uint64)forkret;
p->context.sp = p->kstack + PGSIZE;
```

接下来应该考虑实现将进程的内核页表写入 `satp` 寄存器，参照 `kvminithart` 实现辅助函数 `ukvminithart`，并将其添加至 `kernel/defs.h`：

```c
void
ukvminithart(pagetable_t pagetable)
{
    w_satp(MAKE_SATP(pagetable));
    sfence_vma();
}
```

在调度函数 `scheduler` 中，加载进程的内核页表至核心的 `satp` 寄存器：

```c
void
scheduler(void)
{
    struct proc* p;
    struct cpu* c = mycpu();
    c->proc = 0;
    for(;;){
        intr_on();

        int found = 0;
        for (p = proc; p < &proc[NPROC]; p ++ ) {
            acquire(&p->lock);
            if (p->state == RUNNABLE) {
                // Switch to chosen process.  It is the process's job
                // to release its lock and then reacquire it
                // before jumping back to us.
                p->state = RUNNING;
                c->proc = p;

                // 加载satp寄存器
                ukvminithart(p->kernel_pagetable);

                swtch(&c->context, &p->context);

                // 加载回全局内核页面
                kvminithart();

                // Process is done running for now.
                // It should have changed its p->state before coming back.
                c->proc = 0;
                found = 1;
            }
        }
    }
}
```

接下来还需要释放进程对应的内核页表，在释放进程对应的内核页表之前，首先需要释放进程映射的内核栈，在 `proc.c` 中的 `freeproc` 函数中，需要将 `walk` 函数在 `defs.h` 中声明：

```c
static void
freeproc(struct proc *p)
{
    if(p->trapframe)
        kfree((void*)p->trapframe);
    p->trapframe = 0;
    if(p->pagetable)
        proc_freepagetable(p->pagetable, p->sz);
    p->pagetable = 0;

    // 释放内核栈
    if(p->kstack) {
        pte_t* pte = walk(p->kernel_pagetable, p->kstack, 0);
        kfree((void*)PTE2PA(*pte));
    }
    p->kstack = 0;

    p->sz = 0;
    p->pid = 0;
    p->parent = 0;
    p->name[0] = 0;
    p->chan = 0;
    p->killed = 0;
    p->xstate = 0;
    p->state = UNUSED;
}
```

释放进程对应的内核页表，可以仿照 `freewalk` 函数，编写释放内核页表的函数 `free_user_kernel_pagetable`，然后声明加入到 `proc.c` 开头：

```c
void
free_user_kernel_pagetable(pagetable_t pagetable)
{
    for (int i = 0; i < 512; i ++ ) {
        pte_t pte = pagetable[i];
        if((pte & PTE_V) && (pte & (PTE_R|PTE_W|PTE_X)) == 0){
            uint64 child = PTE2PA(pte);
            free_user_kernel_pagetable((pagetable_t)child);
            pagetable[i] = 0;
        } else if (pte & PTE_V) {
            pagetable[i] = 0;
        }
    }
    kfree((void*)pagetable);
}
```

添加至 `freeproc` 函数：

```c
static void
freeproc(struct proc *p)
{
    if(p->trapframe)
        kfree((void*)p->trapframe);
    p->trapframe = 0;
    if(p->pagetable)
        proc_freepagetable(p->pagetable, p->sz);
    p->pagetable = 0;

    // 释放内核栈
    if(p->kstack) {
        pte_t* pte = walk(p->kernel_pagetable, p->kstack, 0);
        kfree((void*)PTE2PA(*pte));
    }
    p->kstack = 0;

    // 释放内核页表

    if(p->kernel_pagetable) {
        free_user_kernel_pagetable(p->kernel_pagetable);
    }
    p->kernel_pagetable = 0;

    p->sz = 0;
    p->pid = 0;
    p->parent = 0;
    p->name[0] = 0;
    p->chan = 0;
    p->killed = 0;
    p->xstate = 0;
    p->state = UNUSED;
}
```

最后，由于进程的内核页表的虚拟地址写入到 `satp` 寄存器中，需要用到 `vm.c` 中的 `kvmpa` 函数将其翻译成物理地址，这里 `kvmpa` 默认的输入是 `kernel_pagetable`，我们需要换成每个进程自己的内核页表的虚拟地址（首先需要在 `vm.c` 中添加头文件）：

```c
#include "spinlock.h"  // spinlock.h 必须在 proc.h 之前定义
#include "proc.h"

...
...

uint64
kvmpa(uint64 va)
{
    uint64 off = va % PGSIZE;
    pte_t *pte;
    uint64 pa;

    // pte = walk(kernel_pagetable, va, 0);
    pte = walk(myproc()->kernel_pagetable, va, 0);
    if(pte == 0)
        panic("kvmpa");
    if((*pte & PTE_V) == 0)
        panic("kvmpa");
    pa = PTE2PA(*pte);
    return pa+off;
}
```

# simplify copyin/copyinstr

首先要将用户映射添加到每个进程的内核页表中，以使得程序能够直接解引用用户指针。

内核页表中，内核自身指令和数据的虚拟地址位于高位，用户空间的虚拟地址从 0 开始，不会和内核重叠。

首先编写映射函数 `map_user_kerlnel`，实际上就是把用户空间虚拟地址对应 pte 条目一一写入内核页表对应的 pte

```C
void
map_user_kernel(pagetable_t user_pagetable, pagetable_t kernel_pagetable, uint64 oldsz, uint64 newsz)
{
    pte_t* pte_user;
    pte_t* pte_kernel;
    uint64 pa, i;
    uint flags;

    // 为什么可以拿sz来作为地址，因为用户空间的虚拟地址是从0开始的，因此sz等于末尾地址

    oldsz = PGROUNDUP(oldsz);  // 从下一个页的起始点开始
    for (i = oldsz; i < newsz; i += PGSIZE) {
        if((pte_user = walk(user_pagetable, i, 0)) == 0)  // 拿到用户空间物理地址的pte
            panic("map_user_kernel: src pte does not exist");
        if((pte_kernel = walk(kernel_pagetable, i, 1)) == 0)  // 拿到内核页表的对应虚拟地址i的pte
            panic("map_user_kernel: dist pte does not exist");

        flags = (PTE_FLAGS(*pte_user)) & (~PTE_U);  // 在内核模式下，无法访问设置了PTE_U的页面，因此需要去掉该标志位
        pa = PTE2PA(*pte_user);
        *pte_kernel = PA2PTE(pa) | flags;  // ppn加上标志位得到pte
    }
}
```

在内核更改进程的用户映射的每一处，都以相同的方式更改进程的内核页表

在 `kernel/proc.c` 中的 `fork()` 处

```C
// Copy user memory from parent to child.
if(uvmcopy(p->pagetable, np->pagetable, p->sz) < 0){
    freeproc(np);
    release(&np->lock);
    return -1;
}
np->sz = p->sz;

// 添加用户映射到内核空间
map_user_kernel(np->pagetable, np->kernel_pagetable, 0, np->sz);
```

在 `kernel/exec.c` 中

```C
int
exec(char* path, char** argv)
{
    ...

    // Commit to the user image.
    oldpagetable = p->pagetable;
    p->pagetable = pagetable;
    p->sz = sz;
    p->trapframe->epc = elf.entry;  // initial program counter = main
    p->trapframe->sp = sp; // initial stack pointer
    proc_freepagetable(oldpagetable, oldsz);

    // 添加用户映射到内核空间
    map_user_kernel(p->pagetable, p->kernel_pagetable, 0, p->sz);

    ...
}
```

在 `kernel/proc.c` 中的 `growproc()` 处

```C
int
growproc(int n)
{
  uint sz;
  struct proc *p = myproc();

  sz = p->sz;
  if(n > 0){
    // 加上PLIC限制
    if(PGROUNDUP(sz + n) >= PLIC)  // 之所以要PGROUNDUP是因为uvmalloc会PGROUNDUP取下一页开始
      return -1;
    if((sz = uvmalloc(p->pagetable, sz, sz + n)) == 0) {
      return -1;
    }
  } else if(n < 0){
    sz = uvmdealloc(p->pagetable, sz, sz + n);
  }
  // 添加用户映射到内核空间
  map_user_kernel(p->pagetable, p->kernel_pagetable, p->sz, sz);
  p->sz = sz;
  return 0;
}
```

在 `kernel/proc.c` 中的 `userinit` 中包含第一个进程的用户页表

```C
void
userinit(void)
{
  struct proc *p;

  p = allocproc();
  initproc = p;
--
  // allocate one user page and copy init's instructions
  // and data into it.
  uvminit(p->pagetable, initcode, sizeof(initcode));
  p->sz = PGSIZE;

  // prepare for the very first "return" from kernel to user.
  p->trapframe->epc = 0;      // user program counter
  p->trapframe->sp = PGSIZE;  // user stack pointer

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  p->state = RUNNABLE;

  map_user_kernel(p->pagetable, p->kernel_pagetable, 0, p->sz);
  release(&p->lock);
}
```

完成映射后对 `copyin` 和 `copyinstr` 做修改：

```C
int
copyin(pagetable_t pagetable, char *dst, uint64 srcva, uint64 len)
{
    if (copyin_new(pagetable, dst, srcva, len) < 0) return -1;
    return 0;
}
```

```C
int
copyinstr(pagetable_t pagetable, char *dst, uint64 srcva, uint64 max)
{
    if(copyinstr_new(pagetable, dst, srcva, max) < 0) return -1;
    return 0;
}
```
