#include <assert.h>
#include <thread>
#include <iostream>
#include <chrono>

#include "LockFreeRingQueue.h"


const int push_thread_num = 5;
const int pop_thread_num = 5;
const int push_num = 10000;
const int pop_num = 10000;
const int queue_size = 1000;

void push_func(LockFreeRingQueue<int>& lf_queue) {
  for (int i = 0; i < push_num; i++) {
    // make sure we can push success finally
    while (lf_queue.Push(42) == false) {}
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}

void pop_func(LockFreeRingQueue<int>& lf_queue) {
  for (int i = 0; i < pop_num; i++) {
    int tmp;
    // make sure we can pop success finally
    while (lf_queue.Pop(&tmp) == false) {}
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}


int main() {
  LockFreeRingQueue<int> lf_queue(queue_size);
  
  for (int i = 0; i < push_thread_num; i++) {
    std::thread t(push_func, std::ref(lf_queue));
    t.detach();
  }

  for (int i = 0; i < pop_thread_num; i++) {
    std::thread t(pop_func, std::ref(lf_queue));
    t.detach();
  }

  size_t size = 0;
  size_t cap = 0;
  do {
    cap = lf_queue.GetCap();
    size = lf_queue.GetQueueSize();
    std::cout << "Cap: " << cap << "  QueueSize: " << size << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
  } while (true);

  return 0;
}
