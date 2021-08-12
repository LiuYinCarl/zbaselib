#include <algorithm>
#include <iostream>
#include <thread>
#include <chrono>
#include "Channel.h"

using namespace zbaselib;


void testLockFreeCircularBuffer() {
  const int push_thread_num = 5;
  const int pop_thread_num = 5;
  const int push_num = 10000;
  const int pop_num = 10000;
  const int queue_size = 1000;
}


void fibonacci()
{
  Chan<int> ch;
  Chan<int> quit;

  std::thread([&]() {
    for (size_t i = 0; i < 10; i++) {
      //std::this_thread::sleep_for(std::chrono::milliseconds(50));
      std::cout << ch << std::endl;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    // send quit signal
    quit << 0;
    std::cout << "-> quit send" << std::endl;
  }).detach();


  int x = 0, y = 1;
  int quit_flag = 0;
  for (bool break_ = false; !break_; ) {
    std::cout << "select\n";
    Select {
      Case { ch << x, [&]() {
	std::cout << "<- ch send: " << x << std::endl;
	int t = x;
	x = y;
	y += t;
      }},
	Case { quit, [&](auto v) {
	  std::cout << "<- quit recv" << std::endl; 
	  break_ = true;
      }}
    };
    std::cout << "select end\n";
  }
  std::cout << "break!!" << std::endl;
}


int main() {
  std::cout << "----- Demo fibonacci -----" << std::endl;
  fibonacci();

  return 0;
}
