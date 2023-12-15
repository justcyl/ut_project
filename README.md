# UT_project

## 项目背景
在这个项目中，我们实现轻量级的用户态线程，也称为协程 (coroutine，“协同程序”，以下统一用协程指代)，可以在一个不支持线程的操作系统上实现共享内存多任务并发。即我们希望实现 C 语言的 “函数”，它能够：

* 被 start() 调用，从头开始运行；
* 在运行到中途时，调用 yield() 被 “切换” 出去；
* 稍后有其他协程调用 yield() 后，选择一个先前被切换的协程继续执行。



## 项目要求
实现协程库 co.h 中定义的 API：

```c
struct co *co_start(const char *name, void (*func)(void *), void *arg);
void       co_yield();
void       co_wait(struct co *co);
```

协程库的使用和线程库非常类似：

1. `co_start(name, func, arg)` 创建一个新的协程，并返回一个指向 `struct co` 的指针 (类似于 `pthread_create`)。
   - 新创建的协程从函数 `func` 开始执行，并传入参数 `arg`。新创建的协程不会立即执行，而是调用 `co_start` 的协程继续执行。
2. `co_wait(co)` 表示当前协程需要等待，直到 co 协程的执行完成才能继续执行 (类似于 `pthread_join`)。
3. `co_yield()` 实现协程的切换。协程运行后一直在 CPU 上执行，直到 func 函数返回或调用 `co_yield` 使当前运行的协程暂时放弃执行。
4. `main` 函数的执行也是一个协程，因此可以在 main 中调用 `co_yield` 或 `co_wait`。`main` 函数返回后，无论有多少协程，进程都将直接终止。

## 项目实现


1. 使用循环列表实现 FCFS 调度，对于每一个协程记录以下信息(类似线程的 TCB)：

```c
enum co_status {
  CO_NEW = 1, // 新创建，还未执行过
  CO_RUNNING, // 已经执行过，且不属于等待状态
  CO_WAITING, // 调用 co_wait 并等待
  CO_DEAD,    // 已经结束，但还未释放资源
};

struct co {
  struct co *next; // 环形链表记录下一个协程
  void (*func)(void *);  // 协程的入口函数
  void *arg;             // 协程的参数，仅一个
  enum co_status status; // 协程的状态
  struct co *waiter; // 是否有其他协程在等待该协程, 即是否有协程调用了
                     // co_wait(该协程)
  const char *name, *padding; // 协程的名字，同时要满足堆栈16字节(x64)的对齐

  jmp_buf context;               // 寄存器现场 (setjmp.h)
  uint8_t stack[STACK_SIZE]; // 协程的堆栈, 64KiB
};
```
2. 使用 c 语言标准的 setjmp/longjmp 实现上下文保存/切换。
3. 用内联汇编的形式让 co_start 创建的协程，切换到指定的堆栈执行。以64位系统为例，伪代码如下：
```c
stack_switch_call(void *sp, void *entry, void *arg)
{
    //把三个参数保存到rbp、rdx、rax中
    rbp = sp;
    rdx = entry;
    rax = arg;

    //把old_rsp保存到co1->stack[STACK_SIZE]数组表示的新栈帧中，等call返回时可以进行恢复（栈由高地址向低地址生长）
    mov    %rsp,-0x10(%rbx);
    
    //把新的栈帧顶赋值给rsp寄存器，完成堆栈的切换
    lea    -0x20(%rbx),%rsp;
    
    //把参数保存到rdi寄存器中, 此处只有arg一个参数
    mov    %rax,%rdi;
    //执行流切换
    callq  *%rdx;
    
    //把新的栈帧顶赋值给rsp寄存器，完成堆栈的切换
    mov    -0x10(%rbx),%rsp;
}
```
API 的工作流程图如下：
![flow](./flow.svg)

## 项目演示
项目大纲如下：
```
uthread
├── README.md
├── demo
│   └── demo.c
├── flow.svg
├── src
│   ├── Makefile
│   ├── co.c
│   └── co.h
└── tests
    ├── Makefile
    ├── co-test.h
    └── main.c
```

首先编译共享库 (shared object, 动态链接库) libco-32.so 和 libco-64.so：

```bash
cd uthread/src
make all
test -f "libco-32.so" && echo "libco-32.so exists" || echo "libco-32.so does not exist"
test -f "libco-64.so" && echo "libco-64.so exists" || echo "libco-64.so does not exist"
```
> 64 位处理器上编译 x86-32 位程序需有 gcc-multilib

然后在终端输入以下命令:(64 位系统)

```bash
cd uthread/demo
gcc -I../src -L../src -m64 demo.c -o demo-64 -lco-64
LD_LIBRARY_PATH=../src ./demo-64
```

预期结果如下：
> a[1] b[2] a[3] b[4] a[5] b[6] a[7] b[8] a[9] b[10] Done

## 项目测试

在 `tests` 文件夹包含了两组测试样例
1. 创建两个协程，每个协程会循环 100 次，然后打印当前协程的名字和全局计数器 `g_count` 的数值，然后执行 `g_count++`。
2. 创建两个生产者、两个消费者。每个生产者每次会向队列中插入一个数据，然后执行 `co_yield()` 让其他 (随机的) 协程执行；每个消费者会检查队列是否为空，如果非空会从队列中取出头部的元素。无论队列是否为空，之后都会调用 `co_yield()` 让其他 (随机的) 协程执行

`src`编译成功后，在 `tests`中执行 `make test` 会在 x86-64 和 x86-32 两个环境下运行代码 —— 如果看到第一个测试用例打印出数字 X/Y-0 到 X/Y-199、第二个测试用例打印出 libco-200 到 libco-399，说明测试通过。

## 项目参考

https://jyywiki.cn/OS/2022/labs/M2.html

https://github.com/SiyuanYue/NJUOSLab-M2-libco/tree/master

https://zhuanlan.zhihu.com/p/490475991?theme=dark

https://jiaweihawk.gitee.io/2021/08/06/%E6%93%8D%E4%BD%9C%E7%B3%BB%E7%BB%9F-%E8%AE%BE%E8%AE%A1%E4%B8%8E%E5%AE%9E%E7%8E%B0-%E4%B8%89/#co-yield%E5%87%BD%E6%95%B0%E7%9A%84%E8%AE%BE%E8%AE%A1%E4%B8%8E%E5%AE%9E%E7%8E%B0

https://www.noicdi.com/posts/5e8e42b3.html