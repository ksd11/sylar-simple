#include "fiber.h"
#include "config.h"
#include "log.h"
#include "macro.h"
#include "scheduler.h"
#include <atomic>

namespace sylar {

static Logger::ptr g_logger = SYLAR_LOG_NAME("system");

static std::atomic<uint64_t> s_fiber_id{0};    // 协程id, 自增
static std::atomic<uint64_t> s_fiber_count{0}; // 协程数量

static thread_local Fiber *t_fiber = nullptr;           // 当前协程
static thread_local Fiber::ptr t_threadFiber = nullptr; // 主协程

// 栈大小配置，默认128K
static ConfigVar<uint32_t>::ptr g_fiber_stack_size = Config::Lookup<uint32_t>(
    "fiber.stack_size", 128 * 1024, "fiber stack size");

// 栈分配器
class MallocStackAllocator {
  public:
    static void *Alloc(size_t size) { return malloc(size); }

    static void Dealloc(void *vp, size_t size) { return free(vp); }
};

using StackAllocator = MallocStackAllocator;

// 返回协程id
uint64_t Fiber::GetFiberId() {
    if (t_fiber) {
        return t_fiber->getId();
    }
    return 0;
}

// 只有Main协程才会调用，其他协程走的都是带cb的构造函数
Fiber::Fiber() {
    m_state = EXEC; // 标记协程在运行
    SetThis(this);  // 设置当前协程为自己

    if (getcontext(&m_ctx)) {
        SYLAR_ASSERT2(false, "getcontext");
    }

    ++s_fiber_count;

    SYLAR_LOG_DEBUG(g_logger) << "Fiber::Fiber main";
}

// 子协程初始化
Fiber::Fiber(std::function<void()> cb, size_t stacksize,
             bool not_run_in_scheduler)
    : m_id(++s_fiber_id), m_cb(cb), m_run_in_scheduler(!not_run_in_scheduler) {
    ++s_fiber_count;
    m_stacksize = stacksize ? stacksize : g_fiber_stack_size->getValue();

    m_stack = StackAllocator::Alloc(m_stacksize);
    if (getcontext(&m_ctx)) {
        SYLAR_ASSERT2(false, "getcontext");
    }
    m_ctx.uc_link = nullptr;
    m_ctx.uc_stack.ss_sp = m_stack;
    m_ctx.uc_stack.ss_size = m_stacksize;

    makecontext(&m_ctx, &Fiber::MainFunc, 0);

    SYLAR_LOG_DEBUG(g_logger) << "Fiber::Fiber id=" << m_id;
}

Fiber::~Fiber() {
    --s_fiber_count;

    // 子协程
    if (m_stack) {
        SYLAR_ASSERT(m_state == TERM || m_state == EXCEPT || m_state == INIT);

        StackAllocator::Dealloc(m_stack, m_stacksize);
    } else {
        // 主协程
        SYLAR_ASSERT(!m_cb);
        SYLAR_ASSERT(m_state == EXEC);

        Fiber *cur = t_fiber;
        if (cur == this) {
            SetThis(nullptr);
        }
    }
    SYLAR_LOG_DEBUG(g_logger)
        << "Fiber::~Fiber id=" << m_id << " total=" << s_fiber_count;
}

// 重置协程函数，并重置状态
//  INIT，TERM, EXCEPT, HOLD?
void Fiber::reset(std::function<void()> cb) {
    SYLAR_ASSERT(m_stack);
    // SYLAR_ASSERT(m_state == TERM || m_state == EXCEPT || m_state == INIT);
    m_cb = cb;
    if (getcontext(&m_ctx)) {
        SYLAR_ASSERT2(false, "getcontext");
    }

    m_ctx.uc_link = nullptr;
    m_ctx.uc_stack.ss_sp = m_stack;
    m_ctx.uc_stack.ss_size = m_stacksize;

    makecontext(&m_ctx, &Fiber::MainFunc, 0);
    m_state = INIT;
}

// 切换到当前协程执行，子协程调用，使主协程让出执行权
void Fiber::Resume() {
    SetThis(this);
    SYLAR_ASSERT(m_state != EXEC);
    m_state = EXEC;
    if (m_run_in_scheduler) {
        if (swapcontext(&Scheduler::GetMainFiber()->m_ctx, &m_ctx)) {
            SYLAR_ASSERT2(false, "swapcontext");
        }
    } else {
        if (swapcontext(&t_threadFiber->m_ctx, &m_ctx)) {
            SYLAR_ASSERT2(false, "swapcontext");
        }
    }
}

// 切换到后台执行，子协程让出执行权
void Fiber::Yield() {
    // yield后当前协程变为HOLD暂停状态
    Fiber::ptr cur = GetThis();
    SYLAR_ASSERT(!(cur->m_state == INIT));
    if(cur -> m_state == EXEC)
        cur->m_state = HOLD;
    else{
        cur.reset(); // 难点：这里要reset，否则协程退出时cur持有shared_ptr，导致
    }

    if (m_run_in_scheduler) {
        SetThis(Scheduler::GetMainFiber());
        if (swapcontext(&m_ctx, &Scheduler::GetMainFiber()->m_ctx)) {
            SYLAR_ASSERT2(false, "swapcontext");
        }
    } else {
        SetThis(t_threadFiber.get());
        if (swapcontext(&m_ctx, &t_threadFiber->m_ctx)) {
            SYLAR_ASSERT2(false, "swapcontext");
        }
    }
}

// 设置当前协程
void Fiber::SetThis(Fiber *f) { t_fiber = f; }

// 返回当前协程
Fiber::ptr Fiber::GetThis() {
    if (t_fiber) {
        return t_fiber->shared_from_this();
    }
    Fiber::ptr main_fiber(new Fiber);
    SYLAR_ASSERT(t_fiber == main_fiber.get());
    t_threadFiber = main_fiber; // 设置主协程
    return t_fiber->shared_from_this();
}

// 总协程数
uint64_t Fiber::TotalFibers() { return s_fiber_count; }

// swapIn <-> swapOut
void Fiber::MainFunc() {
    Fiber::ptr cur = GetThis();
    SYLAR_ASSERT(cur);
    try {
        cur->m_cb();
        cur->m_cb = nullptr;
        cur->m_state = TERM;
    } catch (std::exception &ex) {
        cur->m_state = EXCEPT;
        SYLAR_LOG_ERROR(g_logger) << "Fiber Except: " << ex.what()
                                  << " fiber_id=" << cur->getId() << std::endl
                                  << sylar::BacktraceToString();
    } catch (...) {
        cur->m_state = EXCEPT;
        SYLAR_LOG_ERROR(g_logger) << "Fiber Except"
                                  << " fiber_id=" << cur->getId() << std::endl
                                  << sylar::BacktraceToString();
    }

    auto raw_ptr = cur.get();
    // SYLAR_LOG_DEBUG(g_logger) << "----" << cur.use_count();
    cur.reset();
    raw_ptr->Yield();

    SYLAR_ASSERT2(false,
                  "never reach fiber_id=" + std::to_string(raw_ptr->getId()));
}

} // namespace sylar
