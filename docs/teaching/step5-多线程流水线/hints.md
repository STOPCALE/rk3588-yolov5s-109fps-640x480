# step5 · 提示分级卡（卡壳时翻，不卡不看）

> L3 使用规则不变：翻到 L3 ＝ 当天合上资料凭记忆重做 + 登记到附录 E。

---

## L1 · 方向（一句话指路）

并发问题先做三问：
1. **共享的是谁？**（数据所有权：深/浅拷贝）——帧被偷换类；
2. **等在哪、锁住谁？**（等待与放锁的边界）——串行化类；
3. **队列有没有上界？**（延迟有没有天花板）——背压类。
把现象往这三问上靠，基本都能定位。

## L2 · 检查点

| 症状 | 先查什么 |
|---|---|
| 结果"随机错乱"、偶尔对 | 入队是否深拷贝（谁拥有像素） |
| 吞吐 ≈ 单帧速率（1/T） | 是不是"喂一帧等一帧"（阻塞 get） |
| 延迟越长越高、无上界 | 队列无界/没背压 |
| `front()` 空队列崩溃/UB | wait 醒来没重查条件（虚假唤醒） |
| 取结果线程把喂料线程堵住 | 等待是否发生在**持锁**状态下 |
| 显示一开就掉帧 | 显示没有解耦（在主管线上 imshow/写盘） |

## L3 · 关键片段（翻到此级 = 合上重做 + 登记）

```cpp
// 深拷贝：入队前复制像素（put() 内）
frame = frame.clone();              // cv::Mat 语义
data.assign(src.begin(), src.end()); // 裸 vector 语义

// 背压：满员拒收
if (pending.load() >= maxPending) return 1;   // 拒收新帧，保延迟上界

// 锁外等待：先把 future 搬出来，再 get()
std::future<R> f;
{ std::lock_guard<std::mutex> g(mtx_); f = std::move(futs_.front()); futs_.pop_front(); }
R r = f.get();                      // ← 等在这里，不持锁

// wait 谓词循环（防虚假唤醒）
cv_.wait(lk, [&]{ return quit_ || !tasks_.empty(); });
```

构建运行两个故障小项目：

```bash
cd ~/myproj/docs/teaching/fault-injection/step5-01-帧被偷换
g++ -std=c++14 -O2 steal.cc -o steal -pthread && ./steal
cd ../step5-02-流水线串行化
g++ -std=c++14 -O2 pipeline.cc -o pipeline -pthread && ./pipeline
```
