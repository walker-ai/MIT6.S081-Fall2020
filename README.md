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
