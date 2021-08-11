#pragma once

#include <assert.h>
#include <runetype.h>
#include <stddef.h>
#include <atomic>
#include <exception>

#define THREAD_SAFE

const static size_t max_queu_size = 1 << 20;

template<typename T>
class LockFreeRingQueue {
public:

  LockFreeRingQueue(size_t queue_size) {
    assert(queue_size < max_queu_size);

    ring_queue = new(std::nothrow) T[queue_size];
    assert(ring_queue);

    cap = queue_size;
    
    
  }

  ~LockFreeRingQueue() {
    delete []ring_queue;
    ring_queue = nullptr;
  }

  LockFreeRingQueue(const LockFreeRingQueue&) = delete;
  LockFreeRingQueue& operator=(const LockFreeRingQueue&) = delete; 

  
  THREAD_SAFE size_t GetCap() const {
    return cap;
  }

  THREAD_SAFE size_t GetQueueSize() const {
    uint64_t pos = 0;
    QueuePos* pos_ptr = (QueuePos*)&pos;

    pos = queue_pos.load(std::memory_order_relaxed);

    size_t size = 0;

    if (pos_ptr->head_pos <= pos_ptr->tail_pos)
      size = pos_ptr->tail_pos - pos_ptr->head_pos;
    else
      size = (pos_ptr->tail_pos - 0) + (cap - pos_ptr->head_pos);

    return size;
  }

  THREAD_SAFE bool Push(T value) {
    while (true) {
      // use uint64_t but not QueuePos is to avoid call QueuePos's constructor and destructor frequently.
      uint64_t old_pos = 0;
      uint64_t new_pos = 0;
      QueuePos* old_pos_ptr = (QueuePos*)&old_pos;
      QueuePos* new_pos_ptr = (QueuePos*)&new_pos;

      old_pos = queue_pos.load(std::memory_order_relaxed);
      new_pos = old_pos;

      if (old_pos_ptr->head_pos == old_pos_ptr->tail_pos && !old_pos_ptr->is_empty)
	return false;

      if (old_pos_ptr->is_locked)
	continue;

      ++new_pos_ptr->tail_pos;
      // lock queue
      new_pos_ptr->is_locked = 1;
      new_pos_ptr->is_empty = 0;

      // if we come to the end of the ring, set our 
      if (new_pos_ptr->tail_pos >= cap)
	new_pos_ptr->tail_pos -= cap;

      bool is_queue_pos_not_changed = queue_pos.compare_exchange_weak(old_pos, new_pos, std::memory_order_seq_cst);
      if (is_queue_pos_not_changed) {
	// set value
	ring_queue[old_pos_ptr->tail_pos] = value;
	// todo: do we need a memory barrier here?
	// unlock queue
	while (true) {
	  old_pos = 0;
	  new_pos = 0;
	  
	  old_pos = queue_pos.load(std::memory_order_relaxed);
	  new_pos = old_pos;

	  new_pos_ptr->is_locked = 0;

	  bool is_free_lock_succeed = queue_pos.compare_exchange_weak(old_pos, new_pos, std::memory_order_seq_cst);
	  if (is_free_lock_succeed)
	    break;
	}
	break;
      }      
    }
    return true;
  }

  THREAD_SAFE bool Pop(T* ret_value) {
    while (true) {
      uint64_t old_pos = 0;
      uint64_t new_pos = 0;
      QueuePos* old_pos_ptr = (QueuePos*)&old_pos;
      QueuePos* new_pos_ptr = (QueuePos*)&new_pos;

      old_pos = queue_pos.load(std::memory_order_relaxed);
      new_pos = old_pos;

      if (old_pos_ptr->is_empty)
	return false;

      if (old_pos_ptr->is_locked)
	continue;

      ++new_pos_ptr->head_pos;

      if (new_pos_ptr->head_pos >= cap)
	new_pos_ptr->head_pos -= cap;

      if (new_pos_ptr->head_pos == new_pos_ptr->tail_pos)
	new_pos_ptr->is_empty = 1;

      *ret_value = ring_queue[old_pos_ptr->head_pos];

      bool is_pop_succeed = queue_pos.compare_exchange_weak(old_pos, new_pos, std::memory_order_seq_cst);
      if (is_pop_succeed)
	break;
    }
    return true;
  }

  // THREAD_SAFE T Pop() {
  //   T* ret_value = nullptr;
  //   bool is_pop_succeed = Pop(ret_value);
  //   if (is_pop_succeed)
  //     return *ret_value;
  //   return nullptr;
  // }


private:
  size_t cap;
  T* ring_queue;
  std::atomic_uint64_t queue_pos;

#pragma pack(8)
  struct QueuePos {
    volatile uint64_t head_pos  : 20;
    volatile uint64_t tail_pos  : 20;
    volatile uint64_t is_locked :  1;
    volatile uint64_t is_empty  :  1;
    volatile uint64_t useless   : 22;
  };
#pragma pack()

};
