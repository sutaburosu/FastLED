#include "fl/task/executor.h"
#include "fl/stl/functional.h"
#include "fl/stl/singleton.h"
#include "fl/stl/scope_exit.h"
#include "fl/stl/algorithm.h"
#include "fl/task/task.h"
#include "fl/stl/chrono.h"
#include "fl/system/log.h"
#include "fl/system/log.h"

#include "fl/stl/new.h"
#include "fl/system/yield.h"
#include "platforms/coroutine_runtime.h"

namespace fl {
namespace task {

namespace detail {


/// @brief Get reference to thread-local await recursion depth
/// @return Reference to the thread-local await depth counter
int& await_depth_tls() {
    return SingletonThreadLocal<int>::instance();
}
} // namespace detail

Executor& Executor::instance() {
    return fl::Singleton<Executor>::instance();
}

void Executor::register_runner(Runner* r) {
    if (r && fl::find(mRunners.begin(), mRunners.end(), r) == mRunners.end()) {
        mRunners.push_back(r);
    }
}

void Executor::unregister_runner(Runner* r) {
    auto it = fl::find(mRunners.begin(), mRunners.end(), r);
    if (it != mRunners.end()) {
        mRunners.erase(it);
    }
}

void Executor::update_all() {
    // Update all registered runners
    for (auto* r : mRunners) {
        if (r) {
            r->update();
        }
    }
}

bool Executor::has_active_tasks() const {
    for (const auto* r : mRunners) {
        if (r && r->has_active_tasks()) {
            return true;
        }
    }
    return false;
}

size_t Executor::total_active_tasks() const {
    size_t total = 0;
    for (const auto* r : mRunners) {
        if (r) {
            total += r->active_task_count();
        }
    }
    return total;
}

// Public API functions

void run(fl::u32 microseconds, ExecFlags flags) {
    // Re-entrancy guard: detect if run is called from within run
    bool& running = SingletonThreadLocal<bool>::instance();
    if (running) {
        FL_WARN_ONCE("task::run re-entrancy detected, skipping nested call");
        return;
    }
    running = true;
    auto guard = fl::make_scope_exit([&running]() { running = false; });

    const bool do_tasks = flags & ExecFlags::TASKS;
    const bool do_coroutines = flags & ExecFlags::COROUTINES;
    const bool do_system = flags & ExecFlags::SYSTEM;

    // Calculate start time with rollover protection
    fl::u32 begin_time = fl::micros();

    // Lambda to get elapsed time (rollover-safe)
    auto elapsed = [begin_time]() {
        return fl::micros() - begin_time;
    };

    // Lambda to get remaining time until deadline expires
    auto remaining = [elapsed, microseconds]() -> fl::u32 {
        fl::u32 e = elapsed();
        if (e >= microseconds) {
            return 0;
        }
        return microseconds - e;
    };

    // Lambda to check if deadline has expired
    auto expired = [remaining]() {
        return remaining() == 0;
    };

    do  {
        // TASKS: Scheduler (fl::task timers) + Executor (fetch, HTTP server, audio)
        if (do_tasks) {
            Scheduler::instance().update();
            Executor::instance().update_all();
        }

        // SYSTEM: Pure OS-level yield (vTaskDelay(0), thread yield, etc.)
        if (do_system) {
            fl::yield();
        }

        // COROUTINES: Platform cooperative coroutines (pumpCoroutines)
        if (do_coroutines) {
            auto time_left = remaining();
            if (time_left) {
                fl::u32 sleep_us = fl::min(1000u, time_left);
                fl::platforms::ICoroutineRuntime::instance().pumpCoroutines(sleep_us);
            }
        }
    } while (!expired());
}

size_t active_tasks() {
    return Executor::instance().total_active_tasks();
}

bool has_tasks() {
    return Executor::instance().has_active_tasks();
}


} // namespace task
} // namespace fl
