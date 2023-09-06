# Lab: xv6 lazy page allocation

## Eliminate allocation from sbrk()

本题绝大多数思路和代码已经在课堂上讲解过，lazy allocation 的核心是只增大 `p->sz` 而暂时不分配内存，等到使用时再分配。

```c
uint64
sys_sbrk(void)
{
  int addr;
  int n;

  if(argint(0, &n) < 0)
    return -1;

  addr = myproc()->sz;
  // lazy allocation
  myproc()->sz += n;

  /*
  if(growproc(n) < 0)  // 原先的代码是eager allocation机制，现在是lazy allocation机制，实际分配在trap中实现
	return -1
  */
  
  return addr;
}
```

## Lazy allocation

实现 lazy allocation，完成上一问后，在shell中执行 `echo hi`，输出为：

```
init: starting sh
$ echo hi
usertrap(): unexpected scause 0x000000000000000f pid=3
            sepc=0x0000000000001258 stval=0x0000000000004008
va=0x0000000000004000 pte=0x0000000000000000
panic: uvmunmap: not mapped
```

这表明程序陷入了一个trap，并且不知该如何处理，之所以会得到一个page fault是因为，在Shell中执行程序，Shell会先fork一个子进程，子进程会通过exec执行echo。
在这个过程中，Shell会申请一些内存，所以Shell会调用sys_sbrk，然后就出错了（注，因为前面修改了代码，调用sys_sbrk不会实际分配所需要的内存）

1. 我们应该在 `kernel/trap.c` 中修改 `usertrap()` 使得程序能够在这个过程中分配其所需要的内存。

```c
if(r_scause() == 8){
  ...
} else {
  // ok
} else if(r_scause() == 13 || r_scause() == 15){
  // 处理页面错误
  // stval 中保存的是，访问未分配物理页面的内存，因此需要为其分配物理页面
  // 需要用到 mappages，这是为va和pa添加映射，并存储到pagetable
  // 需要仿照 uvmalloc，uvmalloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz)
  uint64 va = r_stval();
  uint64 ka;
  
  // 判断出错的页面虚拟地址是否在栈空间之上，否则杀掉进程
  // 如果某个进程在高于sbrk()分配的任何虚拟内存地址上出现页错误，则终止该进程。
  // 没有物理内存可以分配了，out of memory，直接杀掉进程
  if(PGROUNDUP(p->trapframe->sp) - 1 < va && va < p->sz && (ka == (uint64)kalloc()) != 0){
    memset((void*)ka, 0, PGSIZE);  // 先将内容置0
	va = PGROUNDDOWN(va);
	if(mappages(p->pagetable, va, PGSIZE, ka, PTE_W|PTE_R|PTE_U|PTE_X) != 0){
	  kfree((void*)ka);
	  p->killed = 1;
	}
  } else {
    ...
  }
  ...
}
```

2. 修改 `kernel/vm.c` 中的 `uvmunmap()`，之所以修改这个代码是因为lazy allocation中首先并未实际分配内存，所以当解除映射关系的时候对于这部分内存要略过，而不是使系统崩溃，这部分在课程视频中已经解答。

```c
void
uvmunmap(pagetable_t pagetable, uint64 va, uint64 npages, int do_free)
{
  ...

  for(a = va; a < va + npages*PGSIZE; a += PGSIZE){
    if((pte = walk(pagetable, a, 0)) == 0)
      panic("uvmunmap: walk");
    if((*pte & PTE_V) == 0)
      continue;

    ...
  }
}
```

## Lazytests and Usertests

1. 处理 `sbrk()` 参数为负数的情况，参考 `growproc(int n)` 中 `n` 为负数的代码，调用 `uvmdealloc` 函数，但要保证缩减后内存不能小于0

```c
uint64
sys_sbrk(void)
{
  int addr;
  int n;

  if(argint(0, &n) < 0)
    return -1;

  struct proc *p = myproc();

  addr = p->sz;
  uint64 sz = p->sz;
  
  if (n > 0) {
    // lazy allocation
    p->sz += n;
  } else if (sz + n > 0){  // 如果n<0表示缩小用户内存，返回缩小后的sz，但缩小后的sz要>0
    sz = uvmdealloc(p->pagetable, sz, sz + n);
    p->sz = sz; 
  } else {
    return -1;
  }
  /*
  if(growproc(n) < 0)  // 原先的代码是eager allocation机制，现在是lazy allocation机制，实际分配在trap中实现
    return -1;
  */
  return addr;
}
```

2. 正确处理 `fork` 的内存拷贝：`fork` 调用了 `uvmcopy` 进行内存拷贝，所以修改 `uvmcopy` 如下：

```c
int
uvmcopy(pagetable_t old, pagetable_t new, uint64 sz)
{
  ...
  for(i = 0; i < sz; i += PGSIZE){
    if((pte = walk(old, i, 0)) == 0)
      continue;  // 正常情况，因为父进程的物理地址也尚未分配
    if((*pte & PTE_V) == 0)
      continue;
    ...
  }
  ...
}
```

3. 在上一问的基础上继续修改 `uvmunmap`，否则会运行出错。

```c
void
uvmunmap(pagetable_t pagetable, uint64 va, uint64 npages, int do_free)
{
  ...

  for(a = va; a < va + npages*PGSIZE; a += PGSIZE){
    if((pte = walk(pagetable, a, 0)) == 0)
      continue;
    if((*pte & PTE_V) == 0)
      continue;

    ...
  }
}
```

解释下这里为什么要用两个 `continue`：

利用 `vmprint` 打印出来的初始时刻用户进程的页表如下：


```
page table 0x0000000087f55000
..0: pte 0x0000000021fd3c01 pa 0x0000000087f4f000
.. ..0: pte 0x0000000021fd4001 pa 0x0000000087f50000
.. .. ..0: pte 0x0000000021fd445f pa 0x0000000087f51000
.. .. ..1: pte 0x0000000021fd4cdf pa 0x0000000087f53000
.. .. ..2: pte 0x0000000021fd900f pa 0x0000000087f64000
.. .. ..3: pte 0x0000000021fd5cdf pa 0x0000000087f57000
..255: pte 0x0000000021fd5001 pa 0x0000000087f54000
.. ..511: pte 0x0000000021fd4801 pa 0x0000000087f52000
.. .. ..510: pte 0x0000000021fd58c7 pa 0x0000000087f56000
.. .. ..511: pte 0x0000000020001c4b pa 0x0000000080007000
```

除去高地址的trapframe和trampoline页面，进程共计映射了4个有效页面，即添加了映射关系的虚拟地址范围是0x0000~0x3fff，
假如使用sbrk又申请了一个页面，由于lazy allocation，页表暂时不会改变，而不经过读写操作后直接释放进程，
进程将会调用 `uvmunmap` 函数，此时将会发生什么呢？

`uvmunmap` 首先使用 `walk` 找到虚拟地址对应的PTE地址，虚拟地址的最后12位表征了偏移量，前面每9位索引一级页表，
将0x4000的虚拟地址写为二进制（省略前面的无效位）：

```
{000 0000 00}[00 0000 000](0 0000 0100) 0000 0000 0000
```

- `{}`：页目录表索引(level==2)，为0
- `[]`：二级页表索引(level==1)，为0
- `()`：三级页表索引(level==0)，为4

我们来看一下 `walk` 函数，`walk` 返回指定虚拟地址的PTE，但我认为这个程序存在一定的不足。walk函数的代码如下所示

```c
pte_t *
walk(pagetable_t pagetable, uint64 va, int alloc)
{
  if(va >= MAXVA)
    panic("walk");

  for(int level = 2; level > 0; level--) {
    pte_t *pte = &pagetable[PX(level, va)];
    if(*pte & PTE_V) {
      pagetable = (pagetable_t)PTE2PA(*pte);
    } else {
      if(!alloc || (pagetable = (pde_t*)kalloc()) == 0)
        return 0;
      memset(pagetable, 0, PGSIZE);
      *pte = PA2PTE(pagetable) | PTE_V;
    }
  }
  return &pagetable[PX(0, va)];
}
```
 
这段代码中 `for` 循环执行 `level==2` 和 `level==1` 的情况，而对照刚才打印的页表，`level==2` 时索引为0的项是存在的，`level==1` 时索引为0的项也是存在的，最后执行 `return` 语句，
然而 `level==0` 时索引为4的项却是不存在的，此时 `walk` 不再检查 `PTE_V` 标志等信息，而是直接返回，因此即使虚拟地址对应的PTE实际不存在，`walk` 函数的返回值也可能不为0！

那么返回的这个地址是什么呢？`level` 为0时

有效索引为0~3，因此索引为4时返回的是最后一个有效PTE后面的一个地址。

因此我们不能仅靠PTE为0来判断虚拟地址无效，还需要再次检查返回的PTE中是否设置了 `PTE_V` 标志位。

4. 处理通过 `sbrk` 申请内存后还未实际分配就传给系统调用使用的情况

系统调用的处理会陷入内核，scause寄存器存储的值是8，如果此时传入的地址还未实际分配，就不能走到上文usertrap中判断scause是13或15后进行内存分配的代码，
syscall执行就会失败。相关系统调用通过 `argaddr()` 来获得地址，从而实现系统调用陷入trap，调用 `syscall()`，返回用户空间。
然后由于内存未实际分配->page fault->usertrap中 `r_scause==13 || r_scause==15`->分配内存，返回用户空间。

因此如果不在 `argaddr` 中分配内存，则无法执行syscall。

修改 `kernel/syscall.c` 中的 `argaddr`：

```c
int
argaddr(int n, uint64 *ip)
{
  *ip = argraw(n);

  struct proc *p = myproc();

  if(walkaddr(p->pagetable, *ip) == 0){  // 得到的虚拟地址*ip未被分配物理地址
    uint64 ka;
    if(PGROUNDUP(p->trapframe->sp) - 1 < *ip && *ip < p->sz && (ka = (uint64)kalloc()) != 0) {
      memset((void*)ka, 0, PGSIZE);

      if(mappages(p->pagetable, PGROUNDDOWN(*ip), PGSIZE, ka, PTE_W|PTE_R|PTE_U|PTE_X) != 0){
        kfree((void*)ka);
        return -1;
      }
    } else {
      return -1;
    }
  }

  return 0;
}
```

