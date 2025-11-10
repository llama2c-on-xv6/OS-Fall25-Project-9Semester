#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

// int main(void) {
//   double x = 3.5;
//   double y = x * 4.2;
//   printf("fptest => %d\n", (int)y);
//   exit(0);
// }

// FPU context-switch test for xv6 — use volatile accumulators to avoid register-only optimization
// 
// // Simple FPU context switch test for xv6
// Parent: sum i^2 from 1.0 to 100.0 step 0.5 (split into two halves)
// Child : sum i^3 from 1.0 to 50.0 step 0.5

static void print_double(double d)
{
  int sign = 0;
  if (d < 0) { sign = 1; d = -d; }
  int ipart = (int)d;
  double frac = d - (double)ipart;
  int frac6 = (int)(frac * 1000000.0 + 0.5);
  if (frac6 >= 1000000) { ipart += 1; frac6 -= 1000000; }
  if (sign) printf("-");
  printf("%d.", ipart);
  int div = 100000;
  int tmp = frac6;
  while (div > 0) {
    int digit = tmp / div;
    printf("%d", digit);
    tmp = tmp % div;
    div /= 10;
  }
}

static void print_result(const char *label, double val)
{
  printf("%s", label);
  print_double(val);
  printf("\n");
}

static int almost_equal(double a, double b, double eps) {
  double diff = a - b;
  if (diff < 0) diff = -diff;
  return diff <= eps;
}

int main(int argc, char *argv[])
{
  volatile double parent_sum = 0.0;   // sum of squares first half
  volatile double child_sum  = 0.0;   // sum of cubes
  double i;
  int pid, status;

  // Parent does first half of sum of squares: 1.0 .. 50.0 (inclusive)
  for (i = 1.0; i <= 50.0; i += 0.5)
    parent_sum += i * i;

  __asm__ volatile("" : : : "memory");

  pid = fork();
  if (pid < 0) {
    printf("fputest: fork failed\n");
    exit(1);
  }

  if (pid == 0) {
    // Child: compute sum of cubes from 1.0 .. 50.0 step 0.5
    for (i = 1.0; i <= 50.0; i += 0.5)
      child_sum += i * i * i;   // <-- ensure cube

    __asm__ volatile("" : : : "memory");

    print_result("fputest child: computed = ", (double)child_sum);

    // compute expected inside child for verification
    double exp_child = 0.0;
    for (i = 1.0; i <= 50.0; i += 0.5)
      exp_child += i * i * i;

    print_result("fputest child: expected = ", exp_child);

    if (almost_equal((double)child_sum, exp_child, 1e-5))
      printf("child verification: PASS\n");
    else {
      printf("child verification: FAIL\n");
      printf("child diff = ");
      print_double((double)(child_sum - exp_child));
      printf("\n");
    }

    exit(0);

  } else {
    // Parent continues second half: 50.5 .. 100.0 step 0.5
    for (i = 50.5; i <= 100.0; i += 0.5)
      parent_sum += i * i;

    __asm__ volatile("" : : : "memory");

    wait(&status);

    print_result("fputest parent: computed = ", (double)parent_sum);

    // compute expected parent sum
    double exp_parent = 0.0;
    for (i = 1.0; i <= 100.0; i += 0.5)
      exp_parent += i * i;

    print_result("fputest parent: expected = ", exp_parent);

    if (almost_equal((double)parent_sum, exp_parent, 1e-5))
      printf("parent verification: PASS\n");
    else {
      printf("parent verification: FAIL\n");
      printf("parent diff = ");
      print_double((double)(parent_sum - exp_parent));
      printf("\n");
    }

    exit(0);
  }
  return 0;
}