#pragma once

enum co_state {
    co_dead = 0,
    co_ready = 1,
    co_running = 2,
    co_suspend = 3
};

struct co_schedule; // co_schedule

typedef void (*co_func)(struct co_schedule*, void* ud);

struct co_schedule* co_open(void);
void co_close(struct co_schedule*);
int co_new(struct co_schedule*, co_func, void* ud);
void co_resume(struct co_schedule*, int id);
// 返回给定 id 的协程的状态
int co_status(struct co_schedule*, int id);
// 返回正在运行的协程的 id
int co_id(struct co_schedule*);
void co_yield(struct co_schedule*);
