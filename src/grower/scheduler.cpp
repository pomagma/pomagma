#include "scheduler.hpp"
#include "threading.hpp"
#include <vector>
#include <thread>
#include <chrono>
#include <tbb/concurrent_queue.h>


namespace pomagma
{

namespace Scheduler
{

static size_t g_worker_count = DEFAULT_THREAD_COUNT;

static std::atomic<bool> g_working_flag(false);
static std::atomic<uint_fast64_t> g_working_count(0);
static std::mutex g_working_mutex;
static std::condition_variable g_working_condition;

static SharedMutex g_strict_mutex;

static std::atomic<uint_fast64_t> g_merge_count(0);
static std::atomic<uint_fast64_t> g_enforce_count(0);
static std::atomic<uint_fast64_t> g_sample_count(0);
static std::atomic<uint_fast64_t> g_cleanup_count(0);


void set_thread_count (size_t worker_count)
{
    POMAGMA_ASSERT_LE(1, worker_count);
    g_worker_count = worker_count;
}

void reset_stats ()
{
    g_merge_count = 0;
    g_enforce_count = 0;
    g_sample_count = 0;
    g_cleanup_count = 0;
}

void log_stats ()
{
    POMAGMA_INFO("processed " << g_merge_count.load() << " merge tasks");
    POMAGMA_INFO("processed " << g_enforce_count.load() << " enforce tasks");
    POMAGMA_INFO("processed " << g_sample_count.load() << " sample tasks");
    POMAGMA_INFO("processed " << g_cleanup_count.load() << " cleanup tasks");
}

template<class Task>
class TaskQueue
{
    tbb::concurrent_queue<Task> m_queue;

public:

    void push (const Task & task)
    {
        m_queue.push(task);
        g_working_condition.notify_one();
    }

    bool try_execute ()
    {
        SharedMutex::SharedLock lock(g_strict_mutex);
        Task task;
        if (m_queue.try_pop(task)) {
            execute(task);
            g_enforce_count.fetch_add(1, relaxed);
            return true;
        } else {
            return false;
        }
    }

    void cancel_referencing (Ob ob)
    {
        tbb::concurrent_queue<Task> queue;
        std::swap(queue, m_queue);
        for (Task task; queue.try_pop(task);) {
            if (not task.references(ob)) {
                push(task);
            }
        }
    }
};

void cancel_tasks_referencing (Ob ob);

template<>
class TaskQueue<MergeTask>
{
    tbb::concurrent_queue<MergeTask> m_queue;

public:

    void push (const MergeTask & task)
    {
        m_queue.push(task);
        g_working_condition.notify_one();
    }

    bool try_execute ()
    {
        MergeTask task;
        if (m_queue.try_pop(task)) {
            SharedMutex::UniqueLock lock(g_strict_mutex);
            execute(task);
            g_merge_count.fetch_add(1, relaxed);
            cancel_tasks_referencing(task.dep);
            return true;
        } else {
            return false;
        }
    }
};


static TaskQueue<MergeTask> g_merge_tasks;
static TaskQueue<ExistsTask> g_exists_tasks;
static TaskQueue<PositiveOrderTask> g_positive_order_tasks;
static TaskQueue<NegativeOrderTask> g_negative_order_tasks;
static TaskQueue<NullaryFunctionTask> g_nullary_function_tasks;
static TaskQueue<InjectiveFunctionTask> g_injective_function_tasks;
static TaskQueue<BinaryFunctionTask> g_binary_function_tasks;
static TaskQueue<SymmetricFunctionTask> g_symmetric_function_tasks;

inline void cancel_tasks_referencing (Ob ob)
{
    g_exists_tasks.cancel_referencing(ob);
    g_nullary_function_tasks.cancel_referencing(ob);
    g_injective_function_tasks.cancel_referencing(ob);
    g_binary_function_tasks.cancel_referencing(ob);
    g_symmetric_function_tasks.cancel_referencing(ob);
    g_positive_order_tasks.cancel_referencing(ob);
    g_negative_order_tasks.cancel_referencing(ob);
}

inline bool enforce_tasks_try_execute ()
{
    if (g_exists_tasks.try_execute() or
        g_nullary_function_tasks.try_execute() or
        g_injective_function_tasks.try_execute() or
        g_binary_function_tasks.try_execute() or
        g_symmetric_function_tasks.try_execute() or
        g_positive_order_tasks.try_execute() or
        g_negative_order_tasks.try_execute())
    {
        if (g_worker_count > 1) {
            cleanup_tasks_push_all();
        }
        return true;
    } else {
        return false;
    }
}

inline bool sample_tasks_try_execute ()
{
    SampleTask task;
    if (sample_tasks_try_pop(task)) {
        SharedMutex::SharedLock lock(g_strict_mutex);
        execute(task);
        g_sample_count.fetch_add(1);
        return true;
    } else {
        return false;
    }
}

inline bool cleanup_tasks_try_execute ()
{
    CleanupTask task;
    if (cleanup_tasks_try_pop(task)) {
        SharedMutex::SharedLock lock(g_strict_mutex);
        execute(task);
        g_cleanup_count.fetch_add(1);
        return true;
    } else {
        return false;
    }
}

bool try_grow_work ()
{
    return g_merge_tasks.try_execute()
        or enforce_tasks_try_execute()
        or sample_tasks_try_execute()
        or cleanup_tasks_try_execute();
}

bool try_cleanup_work ()
{
    return g_merge_tasks.try_execute()
        or enforce_tasks_try_execute()
        or cleanup_tasks_try_execute();
}

void do_work (bool (*try_work)())
{
    g_working_flag.store(true);
    while (likely(g_working_flag.load())) {
        ++g_working_count;
        while (try_work()) {}
        if (unlikely(--g_working_count)) {
            std::unique_lock<std::mutex> lock(g_working_mutex);
            // HACK the timeout should be longer, but something is broken...
            //const auto timeout = std::chrono::seconds(60);
            const auto timeout = std::chrono::milliseconds(100);
            g_working_condition.wait_for(lock, timeout);
        } else {
            g_working_flag.store(false);
            g_working_condition.notify_all();
        }
    }
}

void cleanup ()
{
    POMAGMA_INFO("assuming core facts");
    assume_core_facts();
    POMAGMA_INFO("starting " << g_worker_count << " cleanup threads");
    reset_stats();
    std::vector<std::thread> threads;
    for (size_t i = 0; i < g_worker_count; ++i) {
        threads.push_back(std::thread(do_work, try_cleanup_work));
    }
    for (auto & thread : threads) {
        thread.join();
    }
    POMAGMA_INFO("finished " << g_worker_count << " cleanup threads");
    log_stats();
}

void grow ()
{
    POMAGMA_INFO("starting " << g_worker_count << " grow threads");
    reset_stats();
    std::vector<std::thread> threads;
    for (size_t i = 0; i < g_worker_count; ++i) {
        threads.push_back(std::thread(do_work, try_grow_work));
    }
    for (auto & thread : threads) {
        thread.join();
    }
    POMAGMA_INFO("finished " << g_worker_count << " grow threads");
    log_stats();
}

void load ()
{
    TODO("load structure");
}

void dump ()
{
    TODO("dump structure");
}

} // namespace Scheduler


void schedule (const MergeTask & task)
{
    Scheduler::g_merge_tasks.push(task);
}

void schedule (const ExistsTask & task)
{
    Scheduler::g_exists_tasks.push(task);
}

void schedule (const PositiveOrderTask & task)
{
    Scheduler::g_positive_order_tasks.push(task);
}

void schedule (const NegativeOrderTask & task)
{
    Scheduler::g_negative_order_tasks.push(task);
}

void schedule (const NullaryFunctionTask & task)
{
    Scheduler::g_nullary_function_tasks.push(task);
}

void schedule (const InjectiveFunctionTask & task)
{
    Scheduler::g_injective_function_tasks.push(task);
}

void schedule (const BinaryFunctionTask & task)
{
    Scheduler::g_binary_function_tasks.push(task);
}

void schedule (const SymmetricFunctionTask & task)
{
    Scheduler::g_symmetric_function_tasks.push(task);
}

} // namespace pomagma