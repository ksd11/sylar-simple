/**
 * @file fiber.h
 * @brief 协程封装
 * @author sylar.yin
 * @email 564628276@qq.com
 * @date 2019-05-24
 * @copyright Copyright (c) 2019年 sylar.yin All rights reserved (www.sylar.top)
 */
#ifndef __SYLAR_FIBER_H__
#define __SYLAR_FIBER_H__

#include <functional>
#include <memory>
#include <ucontext.h>

namespace sylar {

class Scheduler;

/**
 * @brief 协程类
 */
class Fiber : public std::enable_shared_from_this<Fiber> {
    friend class Scheduler;

  public:
    typedef std::shared_ptr<Fiber> ptr;

    /**
     * @brief 协程状态
     */
    enum State {
        /// 初始化状态
        INIT,
        /// 暂停状态
        HOLD,
        /// 执行中状态
        EXEC,
        /// 结束状态
        TERM,
        /// 异常状态
        EXCEPT
    };

  private:
    /**
     * @brief 无参构造函数
     * @attention 每个线程第一个协程的构造
     */
    Fiber();

  public:
    /**
     * @brief 构造函数
     * @param[in] cb 协程执行的函数
     * @param[in] stacksize 协程栈大小
     * @param[in] not_run_in_scheduler 是否和scheduler协程切换
     */
    Fiber(std::function<void()> cb, size_t stacksize = 0,
          bool not_run_in_scheduler = false);

    /**
     * @brief 析构函数
     */
    ~Fiber();

    /**
     * @brief 重置协程执行函数,并设置状态
     * @pre getState() 为 INIT, TERM, EXCEPT
     * @post getState() = INIT
     */
    void reset(std::function<void()> cb);

    /**
     * @brief 将当前协程切换到运行状态
     * @pre getState() != EXEC
     * @post getState() = EXEC
     */
    void Resume();

    /**
     * @brief 将当前协程切换到后台
     */
    void Yield();

    static void YieldToHold(){
        auto cur = GetThis();
        auto ptr = cur.get();
        cur.reset();
        ptr->Yield();
        // GetThis()->Yield();
    }

    /**
     * @brief 返回协程id
     */
    uint64_t getId() const { return m_id; }

    /**
     * @brief 返回协程状态
     */
    State getState() const { return m_state; }

  public:
    /**
     * @brief 设置当前线程的运行协程
     * @param[in] f 运行协程
     */
    static void SetThis(Fiber *f);

    /**
     * @brief 返回当前所在的协程
     */
    static Fiber::ptr GetThis();

    /**
     * @brief 返回当前协程的总数量
     */
    static uint64_t TotalFibers();

    /**
     * @brief 协程执行函数
     * @post 执行完成返回到线程主协程
     */
    static void MainFunc();

    /**
     * @brief 获取当前协程的id
     */
    static uint64_t GetFiberId();

  private:
    /// 协程id
    uint64_t m_id = 0;
    /// 协程运行栈大小
    uint32_t m_stacksize = 0;
    /// 协程状态
    State m_state = INIT; // 默认状态
    /// 协程上下文
    ucontext_t m_ctx;
    /// 协程运行栈指针
    void *m_stack = nullptr;
    /// 协程运行函数
    std::function<void()> m_cb;
    /// 是否返回scheduler协程
    bool m_run_in_scheduler;
};

} // namespace sylar

#endif
