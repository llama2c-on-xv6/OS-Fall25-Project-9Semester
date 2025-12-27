#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

void hello(void *arg) {
    printf("Hello from thread %d\n", (int)arg);
    thread_exit();
}

int main() {
    int t1 = thread_create(hello, (void*)1);
    int t2 = thread_create(hello, (void*)2);

    thread_join(t1);
    thread_join(t2);

    printf("Main exiting\n");
    exit(0);
}
