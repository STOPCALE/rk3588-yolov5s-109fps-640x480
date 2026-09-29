#ifndef CORENUM_H
#define CORENUM_H

#include <stdio.h>
#include <mutex>

#include "rknn_api.h"

const int RK3588 = 3;

//设置绑定核心
inline int get_core_num()
{
    //设置绑核变量
    static int core_num = 0;
    static std::mutex mtx;

    //自动锁RALL，构造时调用mts.look
    std::lock_guard<std::mutex> lock(mtx);

    //0-2轮询
    int temp = core_num % RK3588;
    core_num++;
    return temp;
}

#endif