#include "kernel/types.h"
#include "user/user.h"

int main() {
//   printf("cycles: %d\n", rdcycle());
  printf("time: %ld\n", rdtime());
//   printf("instret: %d\n", rdinstret());
  exit(0);
}