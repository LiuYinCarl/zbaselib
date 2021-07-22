#include "ProcessLock.h"

#include <unistd.h>


int main() {
  ProcessLock pl;

  pl.CreateLock("test.lock");
  while(1) {
    printf("tick\n");
    sleep(1);
  }
}
