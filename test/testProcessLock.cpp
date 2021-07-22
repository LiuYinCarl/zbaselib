#include "ProcessLock.h"

int main() {
  ProcessLock pl;

  bool ret = pl.CreateLock("ProcessLock.lock");
  if (!ret) {
	getc(stdin);
    exit(1);
  } else {
    while(1) {
      printf("tick\n");
#ifdef WIN32
    Sleep(1000);
#else
      sleep(1);
#endif
    }
  }
}

