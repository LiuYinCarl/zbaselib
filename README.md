# zbaselib
base library for C/C++


## 编写规范

类名，函数名，结构体名使用驼峰命名法，变量名命名使用下划线命名法。



## ProcessLock.h

适用于 Windows/Linux 平台的进程锁实现，可用于防止进程多开，避免多进程操作文件产生冲突等场景。


## LockFreeRingQueue.h

使用 C++11 编写的，跨平台的无锁环形队列实现。


## Channel.h

> 开发中

模拟 Go 的 Channel 实现，参考[ChannelsCPP](https://github.com/Balnian/ChannelsCPP)。  
使用了无锁队列来实现 Channel Buffer。
