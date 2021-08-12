#pragma once

#include <chrono>
#include <stddef.h>
#include <thread>
#include <vector>
#include <functional>
#include <random>
#include <algorithm>
#include <memory>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <iostream>
// debug
#include <chrono>


namespace zbaselib {

namespace internal {
template<typename...> constexpr bool dependent_false = false;

template<typename T, size_t buffer_size>
class LockFreeCircularBuffer {
public:
  LockFreeCircularBuffer() :
    cap(buffer_size),
    circular_buffer(new(std::nothrow) T[buffer_size]) {
    // debug
    std::thread([&]() {
      std::cout << "check" << std::endl;
      // bool is_empty = IsEmpty();
      // bool is_full = IsFull();
      // std::this_thread::sleep_for(std::chrono::milliseconds(1000));
      // std::cout << "IsFull:" << is_full << " IsEmpty:" << is_empty << std::endl;
    }).detach();
    
  }

  ~LockFreeCircularBuffer() {
    std::cout << "~LockFreeCircularBuffer" << std::endl;
    delete [] circular_buffer;
    circular_buffer = nullptr;
    cap = 0;
  }

  // todo: need??
  // LockFreeCircularBuffer(const LockFreeCircularBuffer&) = delete;
  // LockFreeCircularBuffer& operator= (const LockFreeCircularBuffer) = delete;

  size_t GetCap() const {
    return cap;
  }

  bool IsEmpty() const {
    uint64_t pos = 0;
    BufferPos* pos_ptr = (BufferPos*)&pos;
    pos = buffer_pos.load(std::memory_order_relaxed);
    return static_cast<bool>(pos_ptr->is_empty);
  }

  bool IsFull() const {
    uint64_t pos = 0;
    BufferPos* pos_ptr = (BufferPos*)&pos;
    pos = buffer_pos.load(std::memory_order_relaxed);
    return static_cast<bool>(pos_ptr->is_full);
  }

  bool Push(T value) {
    while (true) {
      uint64_t old_pos = 0;
      uint64_t new_pos = 0;
      BufferPos* old_pos_ptr = (BufferPos*)&old_pos;
      BufferPos* new_pos_ptr = (BufferPos*)&new_pos;

      old_pos = buffer_pos.load(std::memory_order_relaxed);
      new_pos = old_pos;
      
      // buffer is full, so insert failed
      if (old_pos_ptr->is_full)
	return false;

      // the buffer is locked by other thread, try again
      if (old_pos_ptr->is_locked)
	continue;

      ++new_pos_ptr->tail_pos;
      // lock the buffer
      new_pos_ptr->is_locked = 1;
      new_pos_ptr->is_empty = 0;

      // adjust the tail pos, if the tail pointer point to the buffer's end, change it to the buffer's begin
      if (new_pos_ptr->tail_pos >= cap)
	new_pos_ptr->tail_pos -= cap;

      // change if the buffer is full
      if (new_pos_ptr->tail_pos == new_pos_ptr->head_pos)
	new_pos_ptr->is_full = 1;

      bool is_buffer_pos_not_changed = buffer_pos.compare_exchange_weak(old_pos, new_pos, std::memory_order_seq_cst);
      if (is_buffer_pos_not_changed) {
	circular_buffer[old_pos_ptr->tail_pos] = value;
	while (true) {
	  old_pos = buffer_pos.load(std::memory_order_relaxed);
	  new_pos = old_pos;
	  // free buffer lock
	  new_pos_ptr->is_locked = 0;

	  bool is_free_lock_succeed = buffer_pos.compare_exchange_weak(old_pos, new_pos, std::memory_order_seq_cst);
	  if (is_free_lock_succeed)
	    break;
	}
	break;
      }
    }
    return true;
  }

  bool Pop(T* ret_value) {
    while (true) {
      uint64_t old_pos = 0;
      uint64_t new_pos = 0;
      BufferPos* old_pos_ptr = (BufferPos*)&old_pos;
      BufferPos* new_pos_ptr = (BufferPos*)&new_pos;

      old_pos = buffer_pos.load(std::memory_order_relaxed);
      new_pos = old_pos;

      // buffer is empty
      if (old_pos_ptr->is_empty)
	return false;

      if (old_pos_ptr->is_locked)
	continue;

      ++new_pos_ptr->head_pos;
      // adjust head pointer's position
      if (new_pos_ptr->head_pos >= cap)
	new_pos_ptr->head_pos -= cap;

      new_pos_ptr->is_full = 0;

      if (new_pos_ptr->head_pos == new_pos_ptr->tail_pos)
	new_pos_ptr->is_empty = 1;

      *ret_value = circular_buffer[old_pos_ptr->head_pos];

      bool is_pop_succeed = buffer_pos.compare_exchange_weak(old_pos, new_pos, std::memory_order_seq_cst);
      if (is_pop_succeed)
	break;
    }
    return true;
  }

  T Pop() {
    T ret_value{};
    bool is_pop_succeed = Pop(&ret_value);
    if (is_pop_succeed)
      return std::move(ret_value);
    // todo: return T{} is suitable?
    return T{};
  }
  
private:
  size_t cap;
  T* circular_buffer;
  std::atomic_uint64_t buffer_pos;

#pragma pack(8)
  struct BufferPos {
    volatile uint64_t head_pos  : 20;
    volatile uint64_t tail_pos  : 20;
    volatile uint64_t is_locked :  1;
    volatile uint64_t is_empty  :  1;
    volatile uint64_t is_full   :  1;
    volatile uint64_t useless   : 21;
  };
#pragma pack()
};

  
template<typename T, size_t buffer_size = 0>
class ChannelBuffer {
public:
  ChannelBuffer() : is_closed(false) {}

  ~ChannelBuffer() = default;

  T GetNextValue() {
    std::unique_lock<std::mutex> ulock(buffer_lock);
    if (buffer.IsEmpty()) {
      if (is_closed)
	return {};
      reader_waiter.wait(ulock, [&]() { return !buffer.IsEmpty() || is_closed; });
      if (buffer.IsEmpty() && is_closed)
	{
	  writer_waiter.notify_one();
	  return {};
	}
    }

    T temp;
    T val = buffer.Pop();
    std::swap(temp, val);
    writer_waiter.notify_one();
    return temp;
  }

  std::unique_ptr<T> TryGetNextValue() {
    if (is_closed)
      return std::make_unique<T>(T{});

    std::unique_lock<std::mutex> ulock(buffer_lock);
    if (buffer.IsEmpty()) {
      writer_waiter.notify_one();
      return nullptr;
    }

    std::unique_ptr<T> temp = std::make_unique<T>(buffer.Pop());
    writer_waiter.notify_one();
    return std::move(temp);
  }

  void InsertValue(T in) {
    if (!is_closed) {
      {
	std::unique_lock<std::mutex> lock(buffer_lock);
	if (buffer.IsFull()) {
	  writer_waiter.wait(lock, [&]() { return !buffer.IsFull() || is_closed; });
	  if (is_closed) {
	    return;
	  }
	}
	buffer.Push(in);
      }
      reader_waiter.notify_one();
    }
  }

  void Close() {
    is_closed = true;
    // todo: why input_wait notify one, but output_wait notify all ?
    reader_waiter.notify_one();
    writer_waiter.notify_all();
  }

  bool IsClosed() {
    return is_closed;
  }


private:
  LockFreeCircularBuffer<T, buffer_size> buffer;
  std::mutex buffer_lock;
  std::condition_variable reader_waiter; // wait to read
  std::condition_variable writer_waiter; // wait to write
  std::atomic_bool is_closed;
};
  
} // namespace internal


class Case;
template<typename T, size_t buffer_size> class Chan;
template<typename T, size_t buffer_size> class OChan;
template<typename T, size_t buffer_size> class IChan;


  
class Case {
public:
  template<typename T, size_t buffer_size, typename FUNC>
  Case(IChan<T, buffer_size> ch, FUNC f) {
    task = [=]() {
      auto value = ch.buffer->TryGetNextValue();
      if (value) {
	// todo: need * ?
	f(*value);
      };
      return value == nullptr;
    };
  }

  template<typename T, size_t buffer_size, typename FUNC>
  Case(OChan<T, buffer_size> ch, FUNC f) {
    task = [=]() {
      f();
      return true;
    };
  }

  template<typename T, size_t buffer_size, typename FUNC>
  Case(Chan<T, buffer_size> ch, FUNC f) :
    Case(IChan<T, buffer_size>(ch), std::forward<FUNC>(f)) {
  }

  Case(const Case&) = default;

  Case() {
    task = []() {
      return true;
    };
  }
  
  bool operator() () {
    return task();
  }
  
private:
  std::function<bool()> task;   
};


class Default {
public:
  template<typename FUNC>
  Default(FUNC f) {
    task = f;
  }

  void operator() () {
    task();
  }

private:
  std::function<void()> task;
};


class Select {
public:
  template<typename ...T>
  Select(T&&... params) {
    // todo: what is reserve?
    cases.reserve(sizeof...(params));
    Execute(std::forward<T>(params)...);
  }
  
private:
  std::vector<Case> cases;

  bool RandomExecute() {
    std::random_device rd;
    std::mt19937 g(rd());
    std::shuffle(std::begin(cases), std::end(cases), g);
    for (auto& cas : cases) {
      if (!cas()) return true;
    }
    return false;
  }

  template<typename ...T>
  void Execute(Case&& cas, T&&... params) {
    cases.emplace_back(cas);
    Execute(std::forward<T>(params)...);
  }

  void Execute(Case&& cas) {
    cases.emplace_back(cas);
    RandomExecute();
  }

  void Execute(Default&& defaul) {
    if (!RandomExecute())
      defaul();
  }

  template<typename ...T>
  void Execute(Default&& defaul, T&&... params) {
    static_assert(internal::dependent_false<T...>, "There should be only atmost 1 Default case which must be the last paramter of the Select");
  }
};


template<typename T, size_t buffer_size>
void Close(OChan<T, buffer_size> ch) {
  ch.close();
}

// todo: can we use it now ? maybe it it unavailable 
template<typename T, size_t buffer_size = 1>
Chan<T, buffer_size>&& make_Chan() {
  return Chan<T, buffer_size>();
}


template<typename T, size_t buffer_size = 0>
class IChan_Iterator : public std::iterator<std::input_iterator_tag, T> {
public:
  IChan_Iterator(std::shared_ptr<internal::ChannelBuffer<T, buffer_size>> buff, bool is_end = false) :
    buffer(buff) {
    if (!is_end) operator++();
  }

  IChan_Iterator(const IChan_Iterator&) = default;

  T& operator*() {
    return value;
  }

  IChan_Iterator& operator++() {
    // todo: can use use pop?
    value = buffer->GetNextValue();
    return *this;
  }

  IChan_Iterator operator++(int) {
    IChan_Iterator temp_iter(*this);
    operator++();
    return temp_iter;
  }

  inline bool operator==(const IChan_Iterator& rhs) const {
    return buffer->IsClosed();
  }

  inline bool operator!=(const IChan_Iterator& rhs) const {
    return !operator==(rhs);
  }

  
private:
  std::shared_ptr<internal::ChannelBuffer<T, buffer_size>> buffer;
  T value;
};


template<typename T, size_t buffer_size = 0>
class IChan {
protected:
  std::shared_ptr<internal::ChannelBuffer<T, buffer_size>> buffer;

  IChan(std::shared_ptr<internal::ChannelBuffer<T, buffer_size>> buff) :
    buffer(buff) {
  }

public:
  friend class zbaselib::Case;
  
  IChan() = default;

  IChan(const IChan<T, buffer_size>& ch) = default;

  IChan(IChan<T, buffer_size>&& ch) {
    std::swap(buffer, ch.buffer);
  }

  friend IChan<T, buffer_size>& operator>> (IChan<T, buffer_size>& ch, T& obj) {
    obj = ch.buffer->GetNextValue();
    return ch;
  }

  friend IChan<T, buffer_size>& operator<< (T& obj, IChan<T, buffer_size>& ch) {
    obj = ch.buffer->GetNextValue();
    return ch;
  }

  template<size_t out_buffer_size>
  friend IChan<T, buffer_size>& operator>> (IChan<T, buffer_size>& ch, OChan<T, out_buffer_size>& obj) {
    T temp;
    ch >> temp;
    obj << temp;
    return ch;
  }

  template<size_t out_buffer_size>
  friend IChan<T, buffer_size>& operator<< (OChan<T, out_buffer_size>& obj, IChan<T, buffer_size>& ch) {
    T temp;
    ch >> temp;
    obj << temp;
    return ch;
  }

  friend std::istream& operator>> (std::istream& is, IChan<T, buffer_size>& ch) {
    T temp;
    is >> temp;
    ch << temp;
    return is;
  }

  using IChan_EndIterator = IChan_Iterator<T, buffer_size>;

  IChan_Iterator<T, buffer_size> begin() {
    return IChan_Iterator<T, buffer_size> { buffer };
  }

  IChan_EndIterator end() {
    return { buffer, true };
  }
};  


template<typename T, size_t buffer_size = 0>
class OChan {
protected:
  std::shared_ptr<internal::ChannelBuffer<T, buffer_size>> buffer;

  OChan(std::shared_ptr<internal::ChannelBuffer<T, buffer_size>> buff)
    : buffer(buff) {}

public:
  OChan() = default;

  OChan(const OChan<T, buffer_size>& ch) = default;

  OChan(OChan<T, buffer_size>&& ch) {
    std::swap(buffer, ch.buffer);
  }
  // todo: why we need friend functions?
  friend OChan<T, buffer_size>& operator<< (OChan<T, buffer_size>& ch, const T& obj) {
    ch.buffer->InsertValue(obj);
    return ch;
  }

  friend OChan<T, buffer_size>& operator>> (const T& obj, OChan<T, buffer_size>& ch) {
    ch.buffer->InsertValue();
    return ch;
  }

  template<size_t in_buffer_size>
  friend OChan<T, buffer_size>& operator<< (OChan<T, buffer_size>& ch, const IChan<T, in_buffer_size>& obj) {
    T temp;
    temp << obj;
    ch << temp;
    return ch;
  }

  template<size_t in_buffer_size>
  friend OChan<T, buffer_size>& operator>> (const IChan<T, in_buffer_size>& obj, OChan<T, buffer_size>& ch) {
    T temp;
    temp << obj;
    ch << temp;
    return ch;
  }

  friend std::ostream& operator<< (std::ostream& os, OChan<T, buffer_size>& ch) {
    os << ch.buffer->GetNextValue();
    return os;
  }

  void close() {
    buffer->close();
  }
};


template<typename T, size_t buffer_size = 1>
class Chan : public IChan<T, buffer_size>, public OChan<T, buffer_size> {
public:
  Chan() {
    Chan::IChan::buffer = Chan::OChan::buffer
      = std::make_shared<internal::ChannelBuffer<T, buffer_size>>();
  }

  ~Chan() = default;

  friend OChan<T, buffer_size>& operator<< (Chan<T, buffer_size>& ch, const T& obj) {
    return static_cast<OChan<T, buffer_size>&>(ch) << obj;
  }

  friend OChan<T, buffer_size>& operator>> (const T& obj, Chan<T, buffer_size>& ch) {
    return static_cast<OChan<T, buffer_size>&>(ch) << obj;
  }

  friend std::ostream& operator<< (std::ostream& os, Chan<T, buffer_size>& ch) {
    return os << static_cast<OChan<T, buffer_size>&>(ch);
  }

  friend std::istream& operator>> (std::istream& is, Chan<T, buffer_size>& ch) {
    return is >> static_cast<IChan<T, buffer_size>&>(ch);
  }
};

} // namespace zbaselib
