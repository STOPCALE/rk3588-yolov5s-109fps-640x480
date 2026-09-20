#ifndef RKNNPOOL_H
#define RKNNPOOL_H

#include "ThreadPool.hpp"
#include <vector>
#include <iostream>
#include <mutex>
#include <queue>
#include <memory>
#include <opencv2/core.hpp>

//输入数据的私有化，cv::Mat必须clone()
template <typename T>
inline T cloneInput(const T &v) { return v; }

template <>
inline cv::Mat cloneInput<cv::Mat>(const cv::Mat &v) { return v.clone(); }


// rknnModel模型类, inputType模型输入类型, outputType模型输出类型
template <typename rknnModel, typename inputType, typename outputType>
class rknnPool
{
private:
    int threadNum;
    std::string modelPath;

    long long id;
    std::mutex idMtx, queueMtx;
    std::unique_ptr<dpool::ThreadPool> pool;
    std::queue<std::future<outputType>> futs;
    std::vector<std::shared_ptr<rknnModel>> models;

    // 允许同时"在途"（已提交、未取回）的最大帧数; 0 表示不限制
    size_t maxPending_;
    // 因背压被"拒收"（根本没进队列）的新帧数
    size_t dropped_;

protected:
    int getModelId();

public:
    rknnPool(const std::string modelPath, int threadNum);
    int init();
    // 模型推理/Model inference
    int put(inputType inputData);
    // 获取推理结果/Get the results of your inference
    int get(outputType &outputData);
    // 限制在途帧数上限: 越小端到端延迟越低, 吞吐略降; 0 = 不限制
    void setMaxPending(size_t n) { maxPending_ = n; }

    //把开关转发给所有模型
    void setVerbose(bool v)
    {
        for (auto &m : models) m->set_verbose(v);
    }

    // 当前在途帧数(可用它估算端到端延迟)
    size_t pending();
    // 累计被丢弃的帧数
    size_t dropped();
    ~rknnPool();
};

template <typename rknnModel, typename inputType, typename outputType>
rknnPool<rknnModel, inputType, outputType>::rknnPool(const std::string modelPath, int threadNum)
{
    this->modelPath = modelPath;
    this->threadNum = threadNum;
    this->id = 0;
    this->maxPending_ = 0;
    this->dropped_ = 0;
}

template <typename rknnModel, typename inputType, typename outputType>
int rknnPool<rknnModel, inputType, outputType>::init()
{
    try
    {
        this->pool = std::make_unique<dpool::ThreadPool>(this->threadNum);
        for (int i = 0; i < this->threadNum; i++)
            models.push_back(std::make_shared<rknnModel>(this->modelPath.c_str()));
    }
    catch (const std::bad_alloc &e)
    {
        std::cout << "Out of memory: " << e.what() << std::endl;
        return -1;
    }
    // 初始化模型/Initialize the model
    for (int i = 0, ret = 0; i < threadNum; i++)
    {
        ret = models[i]->init(models[0]->get_pctx(), i != 0);
        if (ret != 0)
            return ret;
    }

    return 0;
}

template <typename rknnModel, typename inputType, typename outputType>
int rknnPool<rknnModel, inputType, outputType>::getModelId()
{
    std::lock_guard<std::mutex> lock(idMtx);
    int modelId = id % threadNum;
    id++;
    return modelId;
}

template <typename rknnModel, typename inputType, typename outputType>
int rknnPool<rknnModel, inputType, outputType>::put(inputType inputData)
{
    std::lock_guard<std::mutex> lock(queueMtx);

    // 背压/限流：在途帧越多，端到端延迟越大（延迟 ≈ 在途帧数 × 单轮耗时）。
    // 旧写法只 pop future、不拦提交 —— 任务照样进 dpool 的无界队列被计算 → 延迟无限涨。
    // 现在改为：满员直接"拒收新帧"（返回 1），拦住提交本身。
    if (this->maxPending_ > 0 && futs.size() >= this->maxPending_)
    {
        this->dropped_++;
        return 1;       //这帧被拒收了
    }

    //深拷贝：把像素给任务的主线程后再改原图
    inputType owned = cloneInput(inputData);
    futs.push(pool->submit(&rknnModel::infer, models[this->getModelId()], owned));
    return 0;
}

template <typename rknnModel, typename inputType, typename outputType>
size_t rknnPool<rknnModel, inputType, outputType>::pending()
{
    std::lock_guard<std::mutex> lock(queueMtx);
    return futs.size();
}

template <typename rknnModel, typename inputType, typename outputType>
size_t rknnPool<rknnModel, inputType, outputType>::dropped()
{
    std::lock_guard<std::mutex> lock(queueMtx);
    return dropped_;
}

template <typename rknnModel, typename inputType, typename outputType>
int rknnPool<rknnModel, inputType, outputType>::get(outputType &outputData)
{
    std::future<outputType> f;      //先占位
    {
        std::lock_guard<std::mutex> lock(queueMtx);
        if(futs.empty() == true)
            return 1;
        f = std::move(futs.front());    //把future从队列搬出来
        futs.pop();
    }
    //等着必须放在锁外：f.get() 阻塞一整轮推理
    //抱着queueMtx等 = 把取帧线程的put（）堵死
    outputData = f.get();
    return 0;
}

template <typename rknnModel, typename inputType, typename outputType>
rknnPool<rknnModel, inputType, outputType>::~rknnPool()
{
    while (!futs.empty())
    {
        futs.front().get();         //算完再丢
        futs.pop();
    }
}

#endif
