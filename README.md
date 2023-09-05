# Lab: traps

## RISC-V assembly

分析见 `answers-traps.txt`

## Backtrace 

此函数的作用就是日常debug过程中程序打印的报错信息, 此处为简化版本, 只要求打印程序地址, 可选练习中要求实现打印程序文件名.

lecture中所展示的栈帧结构: ![栈帧结构](https://i.postimg.cc/4xKpBM6q/QQ-20230903225440.png)

首先根据提示在 `kernel/riscv.h` 中添加辅助函数用于读取fp中的值:

```c
static inline uint64
r_fp()
{
  uint64 x;
  asm volatile("mv %0, s0" : "=r" (x) );
  return x;
}
```

实现 `backtrace()`, 返回地址位于栈帧帧指针的固定偏移(-8)位置，并且保存的帧指针位于帧指针的固定偏移(-16)位置. 先使用 `r_fp()` 读取当前的帧指针, 然后读出返回地址并打印, 再将fp定位到前一个帧指针的位置继续读取即可.

```c
void backtrace()
{
  printf("backtrace:\n");
  uint64 fp = r_fp();
  uint64 stack_bottom = PGROUNDUP(fp);

  while (1) {  // while (PGROUNDUP(fp) - PGROUNDDOWN(fP) == PGSIZE)  

    if (fp >= stack_bottom)
      break;

    printf("%p\n", *(uint64*)(fp - 8));
    fp = *(uint64*)(fp - 16);
  }
}
```

XV6在内核中以页面对齐的地址为每个栈分配一个页面, 因此这里也可以使用 `PGROUNDUP(fp) - PGROUNDDOWN(fp) == PGSIZ` 来判断当前的 `fp` 是否被分配了一个页面来终止循环.



## Alarm

Alarm是要实现一个周期警报程序。整个trap过程已经记录在了 `note.txt` 中。

test0要求在trap返回到用户空间后执行 `handler` 函数，这里不能手动获得 `handler` 地址然后执行，因为该函数地址可能为0，无法访问。根据trap过程，题目是想让我们首先回到用户空间去执行 `handler` 函数，这个函数就是中断处理程序，可以让用户编写时钟中断时程序逻辑。
然后再通过执行 `sigreturn` 系统调用返回到中断触发时的系统状态（恢复用户寄存器）。

所以要在 `usertrap` 中将 `p->trapframe->epc` 修改为 `handler` 的地址。这样 `usertrapret` 后，就转而执行 `handler` 了。`p->trapframe->epc` 的变化路程是这样的：

1. `ecall` 指令将PC保存至SEPC

2. 在 `usertrap` 中将SEPC保存到 `p->trapframe->epc`

3. `p->trapframe->epc += 4` 以让程序返回用户空间时直接执行下一条指令

4. 执行系统调用（触发trap有3种可能，这里以系统调用为例）

5. 在 `usertrapret` 中将SEPC改为 `p->trapframe->epc` 的值

6. 在 `sret` （`trampoline` 中的代码）中将PC设置为SEPC的值

test0主要就是设置 `p->trapframe->epc` 的值。

- 首先在 `struct proc` 中添加字段，并在 `allocproc` 和 `freeproc` 中初始化和销毁
```c
int alarm_interval;  // 警报间隔
uint64 handler;  // 处理函数的指针
int ticks_number;  // 自从上次调用sigalarm过去了多少ticks
```
- 在 `sys_sigalarm` 中获取参数 

```c
uint64
sys_sigalarm()
{
  if (argint(0, &myproc()->alarm_interval) < 0) {
    return -1;  // 获取参数0 - interval失败
  }

  if (argaddr(1, &myproc()->handler) < 0) {
    return -1;  // 获取参数1 - handler失败
  }
  return 0;
}
```

- 修改 `usertrap()`

这里要在 `if(which_dev == 2)` 中实现，`which_dev == 2` 表示发生中断的设备是硬件时钟，在 `else if((which_dev = deintr()) != 0)` 中除了计时器可能还包含其他设备。

```c
// give up the CPU if this is a timer interrupt.
if(which_dev == 2) {
    if(++p->ticks_count == p->alarm_interval) {
        // 更改陷阱帧中保留的程序计数器
        p->trapframe->epc = p->handler;
        p->ticks_count = 0;
    }
    yield();
}
```

test1/2主要是实现执行了中断处理程序 `handler` 以后，如何恢复到中断触发前的状态（用户寄存器），这里有个重要的点是 `handler` 中可能会修改用户寄存器，因此为了全部恢复，需要对整个陷阱帧进行备份。
另外一个问题就是为了防止重复调用 `handler`，需要加一个标识当前 `handler` 的调用是否已经返回。

考虑一下没有alarm时运行的大致过程


1. 进入内核空间，保存用户寄存器到进程陷阱帧
2. 陷阱处理过程
3. 恢复用户寄存器，返回用户空间

而当添加了alarm后，变成了以下过程


1. 进入内核空间，保存用户寄存器到进程陷阱帧
2. 陷阱处理过程
3. 恢复用户寄存器，返回用户空间，但此时返回的并不是进入陷阱时的程序地址，而是处理函数handler的地址，而handler可能会改变用户寄存器


重点在于，`handler` 里面很可能会修改用户寄存器，因此需要将整个trapframe保存下来。这里做题过程中我没有意识到 `handler` 可能会修改用户寄存器，题目中的 `handler` 没有修改用户寄存器，也能成功继续循环，但不是返回到中断触发处，根据debug得到一个相对合理的解释是，在 `sigreturn` 没有实现之前，可能是依照编译器或者操作系统其他的逻辑，在中断处理程序执行完毕后，利用ra的值返回到原来中断触发位置（但不是原处）。


- 新增 `struct proc` 字段
```c
int is_alarm;  // 是否正在调用handler
struct trapframe* alarm_trapframe;  // 陷阱帧备份
```

- 在 `allocproc` 和 `freeproc` 中初始化和销毁

```c
/**
 * allocproc.c
 */
// 初始化告警字段
if((p->alarm_trapframe = (struct trapframe*)kalloc()) == 0) {
    release(&p->lock);
    return 0;
}
p->is_alarm = 0;
p->alarm_interval = 0;
p->handler = 0;
p->ticks_number = 0;

/**
 * freeproc.c
 */
if(p->alarm_trapframe)
    kfree((void*)p->alarm_trapframe);
p->alarm_trapframe = 0;
p->is_alarm = 0;
p->alarm_interval = 0;
p->handler = 0;
p->ticks_number = 0;
```

- 更改 `usertrap()`

```c

```c
// give up the CPU if this is a timer interrupt.
if(which_dev == 2) {
  if (p->alarm_interval !=0 && p->is_alarm == 0 && ++ p->ticks_number == p->alarm_interval) {
    memmove(p->alarm_trapframe, p->trapframe, sizeof(struct trapframe));
    p->is_alarm = 1;
    p->trapframe->epc = p->handler;
    p->ticks_number = 0;
  }
  yield();
}
```

- 编写 `sys_sigreturn`

```c
uint64
sys_sigreturn(void) {
  memmove(myproc()->trapframe, myproc()->alarm_trapframe, sizeof(struct trapframe));
  myproc()->is_alarm = 0;
  return 0;
}
```
