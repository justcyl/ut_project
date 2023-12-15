#include "co.h"
#include <assert.h>
#include <setjmp.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static inline void stack_switch_call(void *sp, void *entry, uintptr_t arg) {
  asm volatile(
#if __x86_64__
      "movq %%rsp,-0x10(%0); leaq -0x20(%0), %%rsp; movq %2, %%rdi ; call *%1; "
      "movq -0x10(%0) ,%%rsp;"                   // 恢复 rsp
      :                                          // 没有输出
      : "b"((uintptr_t)sp), "d"(entry), "a"(arg) // 输入给rbx, rdx, rax
      : "memory"
#else
      "movl %%esp, -0x8(%0); leal -0xC(%0), %%esp; movl %2, -0xC(%0); call "
      "*%1;movl -0x8(%0), %%esp"
      :
      : "b"((uintptr_t)sp), "d"(entry), "a"(arg)
      : "memory"
#endif
  );
}
enum co_status {
  CO_NEW = 1, // 新创建，还未执行过
  CO_RUNNING, // 已经执行过，且不属于等待状态
  CO_WAITING, // 调用 co_wait 并等待
  CO_DEAD,    // 已经结束，但还未释放资源
};

#define STACK_SIZE 64 * 1024

struct co {
  struct co *next;       // 环形链表记录下一个协程
  void (*func)(void *);  // 协程的入口函数
  void *arg;             // 协程的参数，仅一个
  enum co_status status; // 协程的状态
  struct co *waiter; // 是否有其他协程在等待该协程, 即是否有协程调用了
                     // co_wait(该协程)
  const char *name, *padding; // 协程的名字，同时要满足堆栈16字节(x64)的对齐

  jmp_buf context;           // 寄存器现场 (setjmp.h)
  uint8_t stack[STACK_SIZE]; // 协程的堆栈, 64KiB
};

struct co *current = NULL;

// 新建协程并插入到环形列表
struct co *co_start(const char *name, void (*func)(void *), void *arg) {
  assert(current != NULL);

  struct co *start = (struct co *)malloc(sizeof(struct co));
  if (start == NULL) {
    fprintf(stderr, "malloc failed\n");
    exit(1);
  }

  start->arg = arg;
  start->func = func;
  start->status = CO_NEW;
  start->name = name;
  // 环形链表插入使得 start->next=current
  struct co *h = current;
  while (h->next != current) {
    h = h->next;
    assert(h != NULL);
  }
  assert(h);
  h->next = start;
  start->next = current;
  return start;
}

// current 等待协程 co 结束
void co_wait(struct co *co) {
  assert(current != NULL);

  current->status = CO_WAITING; // 调用中，设置 waiting 防止再次被调度
  co->waiter = current; // 等待 co 结束后，还需要切换为原来 current
  while (co->status != CO_DEAD) {
    co_yield ();
  }
  current->status = CO_RUNNING;

  // 从环形链表中删除 co 并释放资源
  struct co *h = current;
  while (h->next != co) {
    h = h->next;
    assert(h != NULL);
  }
  h->next = h->next->next;
  free(co);
}

// current 主动让出
void co_yield () {
  assert(current != NULL);

  int val = setjmp(current->context); // setjmp 保存现场
  if (val == 0) // co_yield() 被调用时，setjmp 保存寄存器现场后会立即返回
                // 0，此时我们需要选择下一个待运行的协程 (相当于修改
                // current)，并切换到这个协程运行。
  {
    // choose co_next
    struct co *co_next = current;
    do {
      co_next = co_next->next;
    } while (co_next->status == CO_DEAD || co_next->status == CO_WAITING);
    current = co_next;

    if (current->status == CO_NEW) {
      ((struct co volatile *)current)->status =
          CO_RUNNING; // volatile 防止编译器优化
      stack_switch_call(&current->stack[STACK_SIZE], current->func,
                        (uintptr_t)current->arg);
      ((struct co volatile *)current)->status =
          CO_DEAD; // 从 call 返回说明已经结束了
      if (current->waiter) {
        current = current->waiter;
        longjmp(current->context, 1); // 返回原先调用 co_wait 的协程
      } else {
        co_yield (); // 重新调度，直至遇到一个longjmp
      }
    } else { // 执行过setjmp, 即co_yield()被调用过
      longjmp(current->context, 1);
    }
  } else // longjmp returned(1) , 说明 yield 结束
  {
    return;
  }
}

// main 也是一个协程，同时令 current 为 main
static __attribute__((constructor)) void co_constructor(void) {
  assert(current == NULL);

  current = (struct co *)malloc(sizeof(struct co));
  if (current == NULL) {
    fprintf(stderr, "malloc failed\n");
    exit(1);
  }

  current->status = CO_RUNNING;
  current->waiter = NULL;
  current->name = "main";
  current->next = current;
}

// 释放所有资源
static __attribute__((destructor)) void co_destructor(void) {
  if (current == NULL)
    return;
  if (current == current->next) {
    free(current);
    return;
  }
  struct co *it = current->next;
  current->next = NULL;
  current = it;
  while (current) {
    it = current;
    current = current->next;
    free(it);
  }
}