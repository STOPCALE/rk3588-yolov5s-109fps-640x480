#ifndef RKNNPOOL_H
#define RKNNPOOL_H

#include "ThreadPool.hpp"
#include <vector>
#include <iostream>
#include <mutex>
#include <queue>
#include <memory>
#include <opencv2/core.hpp>

//输入数据私有化
template <typename T>
inline T cloneInput(const T &v) { return v; }

template <>
inline cv::Mat cloneInput<cv::Mat>(const cv::Mat &v) { return v.clone(); }

//rknn模型类，input输入，output输出
template <typename rknnModel, typename inputType, typename outputType>
class rknnPool
{
    private:
        int threadNum;
        std::string moedlPath;

        long long id;
        std::mutex idMtx, queueMtx;
        std::unique_ptr<dpool::ThreadPool> pool;
        std::queue<std::future<outputType>> futs;
        std::vector<std::shared_ptr<rknnModel>> models;

        //允许在途的最大帧数
        size_t maxPending_;
        //背压被拒收的新帧数
        size_t dropped_;

    protected:
        int getModelId();

    public:
        rknnPool(const std::string modelPath, int threadNum);
        int init();
        //模型推理
        int put(inputType inputData);
        //获取结果
        int get(outputType &outputData);
        //限制在途帧数上限：越小端到端延迟越低
        void setMaxPending(size_t n) { maxPending_ = n; }

        //开关转发给所以模型
        void setVerbose(bool v)
        {
            for (auto &m : models) m -> set_verbose(v);
        }

        //非阻塞结果
        int get_try(outputType &outputData)
        {
            std::future<outputType> f;
            {
                if (futs.empty()) return 1;
                if (futs.front().wait_for(std::chrono::milliseconds(0)) != std::future_status::ready)
                    return 2;
                f = std::move(futs.front());
                futs.pop();
            }
            outputData = f.get();   //已经ready，返回不阻塞
            return 0;
        }
        //当前在途帧数
        size_t pending();
        //累计被丢弃的帧数
        size_t dropped();
        ~rknnPool();
};

//这个是模型池的存放位置？
template <typename rknnModel, typename inputType, typename outputType>
rknnPool<rknnModel, inputType, outputType>::rknnPool(const std::string modelPath, int threadNum)
{
    this->moedlPath = modelPath;
    this->threadNum = threadNum;
    this->id = 0;
    this->maxPending_ = 0;
    this->dropped_ = 0;
}

//模型池初始化
template <typename rknnModel, typename inputType, typename outputType>
int rknnPool<rknnModel, inputType, outputType>::init()
{
    try
    {
        this->pool = std::make_unique<dpool::ThreadPool>(this->threadNum);
        for (int i = 0; i < this->threadNum; i++)
        {
            models.push_back(std::make_shared<rknnModel>(this->moedlPath.c_str()));
        }
    }
    catch (const std::bad_alloc &e)
    {
        std::cout << "Out of memory: " << e.what() << std::endl;
        return -1;
    }

    //初始化模型
    for (int i = 0, ret = 0; i < threadNum; i++)
    {
        ret = models[i]->init(models[0]->get_pctx(), i != 0);
        if (ret != 0)
            return ret;
    }

    return 0;
}

//得到模型ID
template <typename rknnMoedl, typename inputType, typename outputType>
int rknnPool<rknnMoedl, inputType, outputType>::getModelId()
{
    std::lock_guard<std::mutex> lock(idMtx);
    int modelId = id % threadNum;
    id++;
    return modelId;
}

//读取推理结果
template <typename rknnModel, typename inputType, typename outputType>
int rknnPool<rknnModel, inputType, outputType>::put(inputType inputData)
{
    std::lock_guard<std::mutex> lock(queueMtx);

    //背压限流
    if (this->maxPending_ > 0 && futs.size() >= this-> maxPending_)
    {
        this->dropped_++;
        return 1;   //拒收帧
    }

    //深拷贝，把像素给任务的主线程后再改原图
    inputType ownde = cloneInput(inputData);
    futs.push(pool->submit(&rknnModel::infer, models[this->getModelId()], ownde));
    return 0;
}

//这是什么？排队锁？
template <typename rknnModel, typename inputType, typename outputType>
size_t rknnPool<rknnModel, inputType, outputType>::pending()
{
    std::lock_guard<std::mutex> lock(queueMtx);
    return futs.size();
}

//这同样是什么？
template <typename rknnModel, typename inputType, typename outputType>
size_t rknnPool<rknnModel, inputType, outputType>::dropped()
{
    std::lock_guard<std::mutex> lock(queueMtx);
    return dropped_;
}

//
template <typename rknnModel, typename inputType, typename outputType>
int rknnPool<rknnModel, inputType, outputType>::get(outputType &outputData)
{
    std::future<outputType> f;  //
   {
         if (futs.empty() == true)
    {
        return 1;
    }
    f = std::move(futs.front());    //把future从队列搬出来
    futs.pop();

   }
    //等着必须放在锁外：f.get() 阻塞一整轮推理
    //抱着queueMtx等 = 把取帧线程的put（）堵死
    outputData = f.get();
    return 0 ;
}

template <typename rknnModel, typename inputType, typename outputType>
rknnPool<rknnModel, inputType, outputType>::~rknnPool()
{
    while (!futs.empty())
    {
        futs.front().get();
        futs.pop();
    }
}


#endif