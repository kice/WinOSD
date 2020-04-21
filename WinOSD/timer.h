// http://www.songho.ca/misc/timer/timer.html
#pragma once

#if defined(WIN32) || defined(_WIN32)   // Windows system specific
#include <windows.h>
#else          // Unix based system specific
#include <sys/time.h>
#endif

#include <atomic>
#include <thread>
#include <functional>
#include <mutex>
#include <condition_variable>
#include <cassert>

class StopWatch
{
public:
    StopWatch()
    {
#if defined(WIN32) || defined(_WIN32)
        QueryPerformanceFrequency(&frequency);
        startCount.QuadPart = 0;
        endCount.QuadPart = 0;
#else
        startCount.tv_sec = startCount.tv_usec = 0;
        endCount.tv_sec = endCount.tv_usec = 0;
#endif

        _stopped = 0;
        _start = 0;
        _end = 0;
    }

    inline void start()
    {
        _stopped = 0; // reset stop flag
#if defined(WIN32) || defined(_WIN32)
        QueryPerformanceCounter(&startCount);
#else
        gettimeofday(&startCount, NULL);
#endif
    }

    inline double stop()
    {
        _stopped = 1; // set timer stopped flag
#if defined(WIN32) || defined(_WIN32)
        QueryPerformanceCounter(&endCount);
#else
        gettimeofday(&endCount, NULL);
#endif
        return ms();
    }

    inline double us()
    {
#if defined(WIN32) || defined(_WIN32)
        if (!_stopped) {
            QueryPerformanceCounter(&endCount);
        }

        _start = startCount.QuadPart * (1000000.0 / frequency.QuadPart);
        _end = endCount.QuadPart * (1000000.0 / frequency.QuadPart);
#else
        if (!_stopped) {
            gettimeofday(&endCount, NULL);
        }

        _start = (startCount.tv_sec * 1000000.0) + startCount.tv_usec;
        _end = (endCount.tv_sec * 1000000.0) + endCount.tv_usec;
#endif

        return _end - _start;
    }

    inline double ms()
    {
        return us() * 0.001;
    }

    inline double sec()
    {
        return us() * 0.000001;
    }

private:
    double _start;
    double _end;
    int    _stopped;

#if defined(WIN32) || defined(_WIN32)
    LARGE_INTEGER frequency;
    LARGE_INTEGER startCount;
    LARGE_INTEGER endCount;
#else
    timeval startCount;
    timeval endCount;
#endif
};

class Timer
{
public:
    Timer(uint32_t interval_ms, std::function<void()> func, bool ignore_late = false)
        : interval(interval_ms)
    {
        ignoreLate = ignore_late;
        callback = func;

        stopFlag = false;

        pauser = std::thread([this] {
            while (true) {
                std::unique_lock<std::mutex> lk(m);
                cv.wait(lk, [this] {
                    return doPause || stopFlag.load();
                        });

                if (stopFlag.load()) {
                    break;
                }

                pauseMutex.lock();
            }
        });

        start();
    }

    void start(bool force_detch = false)
    {
        stopFlag = false;

        if (timer.joinable()) {
            if (force_detch) {
                timer.detach();
            } else {
                timer.join();
            }
        }

        timer = std::thread([this]() {
            using namespace std::chrono_literals;

            StopWatch s;
            s.start();

            while (true) {
                {
                    std::lock_guard _(pauseMutex);
                    if (stopFlag.load()) {
                        break;
                    }

                    auto delta = s.stop();
                    bool late = delta < interval;

                    if ((!late || !ignoreLate) && callback) {
                        callback();
                    }
                }

                s.start();

                // sleep time is (callback() + interval) ms
                std::this_thread::sleep_for(1ms * interval);
            }
        });
    }

    void disable(bool wait_pause = false)
    {
        // already pause, could throw an exception
        if (doPause) {
            return;
        }

        {
            std::unique_lock<std::mutex> lk(m);
            doPause = true;

            if (wait_pause) {
                pauseMutex.lock();
            }
        }
        cv.notify_one();
    }

    void enable()
    {
        if (doPause) {
            assert(!pauseMutex.try_lock());
            pauseMutex.unlock();
        }
    }

    void stop()
    {
        stopFlag = true;
        enable(); // allow the thread to exit
    }

    ~Timer()
    {
        stopFlag = true;

        // if we pause the timer, pause mutex must be locked
        if (doPause) {
            assert(!pauseMutex.try_lock());
            pauseMutex.unlock();
        }

        if (timer.joinable()) {
            timer.join();
        }

        cv.notify_one();
        if (pauser.joinable()) {
            pauser.join();
        }
    }

private:
    std::mutex m;
    std::condition_variable cv;

    std::mutex pauseMutex;

    bool doPause;
    std::atomic<bool> ignoreLate;
    std::atomic<bool> stopFlag;

    const uint32_t interval;

    std::function<void()> callback;

    std::thread pauser;
    std::thread timer;
};

#ifndef TIMEIT_NO_DEBUG_KEEP
#ifndef _DEBUG
#define TIMER_NO_TIMEIT
#endif
#endif

#ifndef TIMER_NO_TIMEIT
#define TIMEIT_START(name) static StopWatch __timer_##name; \
    auto __delta_##name = 0.0; \
    std::vector<std::tuple<double, const char *, const char *>> __stream_##name; \
    __timer_##name.start();

#define TIMEIT(name, text) \
    __stream_##name.push_back({__timer_##name.ms() - __delta_##name, text, __LOGGING_FILE}); \
    __delta_##name = __timer_##name.ms();

#define TIMEITF(name, text, lambda) __delta_##name = __timer_##name.ms(); lambda(); TIMEIT(##name, text)
#define TIMEITB(name, text, ...) __delta_##name = __timer_##name.ms(); __VA_ARGS__ TIMEIT(##name, text)

// the report will take around 1ms
#define TIMEIT_END(name) auto __elapsed_##name = __timer_##name.ms(); \
    for (const auto &[__t, __s, __f] : __stream_##name) \
        LogStream(__f, [](auto msg){ dprint(msg.c_str()); }).stream() \
            << std::setprecision(2) << std::fixed \
            << "[" << #name << ":" << __s << "] " << __t << "ms (" << 100 * __t / __elapsed_##name << "%)"; \
    DBG << #name << " Total time: " << __elapsed_##name << "ms + " << __timer_##name.stop() - __elapsed_##name << "ms";
#else
#define TIMEIT_START(name) ((void)(0));
#define TIMEIT(name, text) ((void)(0));
#define TIMEITF(name, text, lambda) lambda();
#define TIMEITB(name, text, ...) __VA_ARGS__
#define TIMEIT_END(name) ((void)(0));
#endif
