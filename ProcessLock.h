#pragma once

#include <assert.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#define PROCESS_LOCK_BUF_SIZE 16
#ifndef ERR_EXIT
#define ERR_EXIT(msg)           \
  do {                          \
    printf(msg "\n");	        \
    exit(1);                    \
  } while(0)
#endif

class ProcessLock {
 public:
  ProcessLock();
  bool CreateLock(const char* filename);
  void FreeLock();

 private:
  #ifdef WIN32
    HANDLE handle;
  #else
    int fd;
  #endif
};


#ifdef WIN32
ProcessLock::ProcessLock() {
  hLockFile = INVALID_HANDLE_VALUE;  
}

bool ProcessLock::CreateLock(const char* filename) {
  bool result = false;
  
  handle = CreateFile(filename, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_ALWAYS, 0, 0);
  if (handle == INVALID_HANDLE_VALUE)
    goto exit;

  if (!LockFile(handle, 0, 0, 1, 0)) {
    printf("CreateLock() failed. there are another process.");
    goto exit;
  }

  result = true;
exit:
  if (!result && handle != INVALID_HANDLE_VALUE) {
    if (CloseHandle(handle))
      printf("CreateLock() failed. CloseHandle() failed\n");

    handle = INVALID_HANDLE_VALUE;
  }
  
  return result;
} 

void ProcessLock::FreeLock() {
  if (handle == INVALID_HANDLE_VALUE)
    ERR_EXIT("FreeLock() failed. handle == INVALID_HANDLE_VALUE");

  if (!UnlockFile(handle, 0, 0, 1, 0))
    ERR_EXIT("FreeLock() failed. UnlockFile() failed");

  CloseHandle(handle))
    ERR_EXIT("FreeLock() failed. CloseHandle() failed");

  handle = INVALID_HANDLE_VALUE;
}

#else // Linux OS
ProcessLock::ProcessLock() {
  fd = -1;
}

bool ProcessLock::CreateLock(const char* filename) {
  bool result = false;
  struct flock file_lock;
  char buf[PROCESS_LOCK_BUF_SIZE];
  memset(buf, 0, sizeof(buf));
  
  memset(&file_lock, 0, sizeof(file_lock));

  fd = creat(filename, S_IWUSR);
  if (fd == -1)
    ERR_EXIT("CreateLock() failed. creat() failed");

  file_lock.l_type = F_WRLCK;
  file_lock.l_whence = SEEK_SET;
  file_lock.l_start = 0;
  file_lock.l_len = 0;

  if (fcntl(fd, F_SETLK, &file_lock)) {
    if (errno == EACCES || errno == EAGAIN)
      ERR_EXIT("CreateLock() failed. there are another process");
    else
      ERR_EXIT("CreateLock() failed. fcntl() failed");
    
    goto exit;
  }

  if (ftruncate(fd, 0)) {
    ERR_EXIT("CreateLock() failed. ftruncate() failed");
    goto exit;
  }

  snprintf(buf, PROCESS_LOCK_BUF_SIZE, "%ld\n", (long)getpid());
  if (write(fd, buf, strlen(buf)) != strlen(buf))
    ERR_EXIT("CreateLock() failed. write() failed");
  
  result = true;
exit:
  if (!result && fd != -1) {
    close(fd);
    fd = -1;
  }
  return result;
}

void ProcessLock::FreeLock() {
  int ret = false;
  struct flock file_lock;

  if (fd == -1) {
    ERR_EXIT("FreeLock() failed. fd == -1");
    exit(1);
  }
  
  file_lock.l_type = F_UNLCK;
  file_lock.l_whence = SEEK_SET;
  file_lock.l_start = 0;
  file_lock.l_len = 0;

  if (fcntl(fd, F_SETLK, &file_lock) == -1) {
    ERR_EXIT("FreeLock() failed. fcntl() failed");
    exit(1);
  }
  
  if (close(nLockFile) == -1) {
    ERR_EXIT("FreeLock() failed. close() failed");
    exit(1);
  }

  nLockFile = -1;
}

#endif // ifdef WIN32
