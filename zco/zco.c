#include "zco.h"
#include <stdlib.h>
#include <assert.h>
#include <stddef.h>
#include <string.h>
#include <stdint.h>

#if __APPLE__ && __MACH__
    #include <sys/ucontext.h>
#else
    #include <ucontext.h>
#endif

#define STACK_SIZE (1024*1024)
#define DEFAULT_COROUTINE 16

struct coroutine;

// 协程调度器
struct co_schedule {
    char stack[STACK_SIZE]; // 共享栈，运行时使用
    ucontext_t main;        // 主协程上下文
    int nco;                // 协程的个数
    int cap;                // 协程管理器的容量
    int running;            // 正在运行的协程的 id
    struct coroutine **co;  // 协程数组
};

// 协程
struct coroutine {
    co_func func;            // 协程执行的函数
    void* ud;                // 协程参数
    ucontext_t ctx;          // 协程上下文
    struct co_schedule* sch; // 协程所属的调度器
    ptrdiff_t cap;           // 协程申请的堆内存大小
    ptrdiff_t size;          // 保存当前协程时使用的堆内存大小
    int status;              // 协程的运行状态
    char* stack_ptr;         // 协程切出后保存的运行时栈的地址
};

// 分配一个新协程，并分配他的内存
struct coroutine*
_co_new(struct co_schedule* S, co_func func, void* ud) {
    struct coroutine* co = malloc(sizeof(*co));
    co->func = func;
    co->ud = ud;
    co->sch = S;
    co->cap = 0; // 协程申请的堆空间大小
    co->size = 0; // 协程使用了的堆空间大小
    co->status = co_ready;
    co->stack_ptr = NULL;
    return co;
}

// 释放一个协程
void
_co_delete(struct coroutine* co) {
    free(co->stack_ptr);
    free(co);
}

// 创建一个协程调度器
struct co_schedule*
co_open(void) {
    struct co_schedule* S = malloc(sizeof(*S));
    S->nco = 0;
    S->cap = DEFAULT_COROUTINE;
    S->running = -1;
    // 分配协程数组
    S->co = malloc(sizeof(struct coroutine*) * S->cap);
    memset(S->co, 0, sizeof(struct coroutine*) * S->cap);
    return S;
}

// 销毁一个协程调度器
void
co_close(struct co_schedule* S) {
    // 释放管理的所有协程
    for (int i = 0; i < S->cap; i++) {
        struct coroutine* co = S->co[i];
        if (co) {
            _co_delete(co);
        }
    }
    free(S->co);
    S->co = NULL;
    free(S);
}

// 创建一个新的协程并将其交由协程管理器管理
int
co_new(struct co_schedule* S, co_func func, void* ud) {
    struct coroutine* co = _co_new(S, func, ud);
    // 如果当前协程数量超过了协程管理器的容量，则要对协程
    // 管理器进行扩容
    if (S->nco >= S->cap) {
        int id = S->cap;
        // 按 2 倍进行扩容
        S->co = realloc(S->co, S->cap * 2 * sizeof(struct coroutine*));
        memset(S->co + S->cap, 0, S->cap * sizeof(struct coroutine*));
        S->co[S->cap] = co;
        S->cap *= 2;
        S->nco += 1;
        return id;
    } else {
        for (int i = 0; i < S->cap; i++) {
            int id = (i + S->nco) % S->cap;
            if (S->co[id] == NULL) {
                S->co[id] = co;
                S->nco += 1;
                return id;
            }
        }
    }
    assert(0);
    return -1;
}

static void
mainfunc(uint32_t low32, uint32_t hi32) {
    uintptr_t ptr = (uintptr_t)low32 | ((uintptr_t)hi32 << 32);
    struct co_schedule* S = (struct co_schedule*)ptr;
    int id = S->running;
    struct coroutine* C = S->co[id];
    C->func(S, C->ud); // 真正启动一个协程
    _co_delete(C); // 协程执行完成之后进行释放
    S->co[id] = NULL;
    S->nco -= 1;
    S->running = -1;
}

// 启动一个协程，并将控制权交给该协程
void
co_resume(struct co_schedule* S, int id) {
    assert(S->running == -1);
    assert(id >= 0 && id < S->cap);
    // 从协程管理器中找到需要执行的协程
    struct coroutine* C = S->co[id];
    if (C == NULL)
        return;
    int status = C->status;
    switch(status) {
    case co_ready: // 这个协程之前没有运行过
        //初始化ucontext_t结构体,将当前的上下文放到C->ctx里面
        getcontext(&C->ctx);
        // 将协程管理器的 stack 成员设置为当前的运行时栈顶
        // 这个协程管理器管理的所有协程都将 stack 作为自己的运行时栈
        C->ctx.uc_stack.ss_sp = S->stack;
        // 设置运行时栈的大小
        C->ctx.uc_stack.ss_size = STACK_SIZE;
        // 设置协程执行完之后切换到 S->main 协程（主协程）继续执行
        C->ctx.uc_link = &S->main;
        S->running = id;
        C->status = co_running;
        uintptr_t ptr = (uintptr_t)S;
        makecontext(&C->ctx, (void(*)(void))mainfunc, 2, (uint32_t)ptr, (uint32_t)(ptr>>32));
        // 将当前的上下问放入 S->main, 然后用 C->ctx 替换当前上下文
        // 等 C->ctx 执行完之后，就会跳回到此处继续执行
        swapcontext(&S->main, &C->ctx);
        break;
    case co_suspend: // 这个协程之前运行过，现在睡眠了
        // 将协程上次保存的运行时栈的信息拷贝回运行时栈
        memcpy(S->stack + STACK_SIZE - C->size, C->stack_ptr, C->size);
        S->running = id;
        C->status = co_running;
        swapcontext(&S->main, &C->ctx);
        break;
    default:
        assert(0);
    }
}

// 保存运行时栈
static void
_save_stack(struct coroutine* C, char* top) {
    // dummy 变量是被分配在栈上的，由于 Linux 栈内存
    // 的增长方向时从高地址到低地址，所以现在分配的 dummy
    // 的地址一定是栈顶。那么 top - & dummy 就是当前的运行
    // 时栈的大小
    char dummy = 0;
    assert(top - &dummy <= STACK_SIZE);
    // 如果协程之前分配的栈空间不够用来保存本次切换
    // 时的栈的话，则重新申请空间
    if (C->cap < top - &dummy) {
        free(C->stack_ptr);
        C->cap = top - &dummy; // 本次需要的栈的大小
        C->stack_ptr = malloc(C->cap);
    }
    C->size = top - &dummy; // 当前运行时栈实际使用的大小
    // 将运行时栈拷贝到当前协程的数据结构中
    // 接下来就当前写成就会让出 CPU, 协程管理器的运行时栈
    // 就会被下一个分配到 CPU 的协程使用
    memcpy(C->stack_ptr, &dummy, C->size);
}

// 当前运行中的协程让出 CPU，切换到主协程继续运行
void
co_yield(struct co_schedule* S) {
    int id = S->running;
    assert(id >= 0);
    // 从协程管理器中取出当前协程
    struct coroutine* C = S->co[id];
    // todo 这行 assert 的意义是什么？
    assert((char*)&C > S->stack);
    _save_stack(C, S->stack + STACK_SIZE);
    C->status = co_suspend;
    S->running = -1;
    swapcontext(&C->ctx, &S->main);
}

// 获取给定 id 的协程的运行状态
int
co_status(struct co_schedule* S, int id) {
    assert(id >= 0 && id < S->cap);
    if (S->co[id] == NULL) {
        return co_dead;
    }
    return S->co[id]->status;
}

// 获取正在运行的协程的 id
int
co_id(struct co_schedule* S) {
    return S->running;
}