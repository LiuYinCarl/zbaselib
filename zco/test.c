#include "zco.h"
#include <stdio.h>

struct args {
    int n;
};

static void
foo(struct co_schedule* S, void* ud) {
    struct args* arg = ud;
    int start = arg->n;
    for (int i = 0; i < 5; i++) {
        printf("coroutine %d: %d\n", co_id(S), start + i);
        co_yield(S);
    }
}

static void
test(struct co_schedule* S) {
    struct args arg1 = { 0 };
    struct args arg2 = { 100 };

    int co1 = co_new(S, foo, &arg1);
    int co2 = co_new(S, foo, &arg2);

    printf("main start\n");
    while (co_status(S, co1) && co_status(S, co2)) {
        co_resume(S, co1);
        co_resume(S, co2);
    }
    printf("main end\n");
}

int
main() {
    struct co_schedule* S = co_open();
    test(S);
    co_close(S);

    return 0;
}