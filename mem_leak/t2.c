#include <stdlib.h>

int DoSomething()
{
  // Heap-allocated so as not to exhaust stack.
  // Allocated only once.
  static const char * const error_buf = malloc(100);
  return 0;
}

int main()
{
  return DoSomething();
}