#pragma once
// ============================================================
// 线程池测试套件（自检完备，含 end() 安全测试）
// 使用方式：将本文件与 po_threadpool.h / po_threadpool.cpp 一起编译
// 运行 Release x64，C++20
// ============================================================

#include "threadpool.h"
#include <iostream>
#include <chrono>
#include <atomic>
#include <cassert>
#include <vector>
#include <thread>
#include <future>
#include <cmath>

using namespace std::chrono;

namespace te {
    // ============================================================
    // 全局测试参数（集中修改）
    // ============================================================
    constexpr size_t kTaskMaximum = 5000000;      // 必须与 threadpool::task_maximum 保持一致
    constexpr size_t kThreadMaximum = 5000;       // 仅参考

    // --- 可配置任务耗时 ---
    constexpr int kTaskCostUs = 50;     // 模拟任务耗时（微秒），可改为 10/50/100/200
    constexpr int kLongTaskCostUs = 200;    // 长任务耗时（微秒）

    // --- 基本功能 ---
    constexpr size_t kBasicThreads = 4;
    constexpr int    kBasicA = 3, kBasicB = 5;

    // --- 并发正确性 ---
    constexpr size_t kConcurrentThreads = 8;
    constexpr int    kConcurrentTasks = 2000;   // 远小于 task_maximum

    // --- 原子递增 ---
    constexpr size_t kAtomicThreads = 4;
    constexpr int    kAtomicTasks = 50000;

    // --- 异常隔离 ---
    constexpr size_t kExceptionThreads = 2;

    // --- 单次吞吐量 ---
    constexpr size_t kThroughputThreads = 16;
    constexpr size_t kThroughputTasks = 500000; // 必须 < task_maximum
    constexpr bool   kPreallocFutures = true;

    // --- 满队列拒绝 ---
    constexpr size_t kRejectThreads = 1;
    constexpr size_t kRejectExtra = 20;

    // --- 高竞争 ---
    constexpr size_t kContentionThreads = 2;
    constexpr int    kContentionTasks = 200000;

    // --- 多次吞吐量取平均 ---
    constexpr int    kMultiRunCount = 5;
    constexpr size_t kMultiRunTasks = 100000;

    // --- end() 测试 ---
    constexpr size_t kEndBasicThreads = 4;
    constexpr int    kEndBasicTasks = 100;
    constexpr int    kEndTimeoutSec = 5;       // 超时阈值（秒）

    constexpr size_t kEndStressThreads = 8;
    constexpr size_t kEndStressTasks = 50000;
    constexpr int    kEndStressTimeout = 10;

    constexpr size_t kEndRestartThreads = 4;
    constexpr int    kEndRestartTasks1 = 50;
    constexpr int    kEndRestartTasks2 = 50;

    constexpr size_t kEndEmptyThreads = 2;

    // --- 延迟（调度延迟）测试 ---
    constexpr size_t kLatencyThreads = 4;
    constexpr int    kLatencySamples = 1000;

    // --- 长任务混合测试 ---
    constexpr size_t kMixedThreads = 8;
    constexpr int    kMixedTasks = 5000;
    constexpr int    kMixedLongUs = 100;        // 长任务模拟耗时（微秒）

    // ============================================================
    // 辅助函数：带超时的 end() 调用（用于死锁检测）
    // ============================================================
    bool try_end_with_timeout(po::threadpool& pool, int timeout_seconds) {
        std::atomic<bool> finished{ false };
        std::thread end_thread([&]() {
            try {
                pool.end();
            }
            catch (const std::exception& e) {
                std::cerr << "  end() threw: " << e.what() << std::endl;
            }
            catch (...) {
                std::cerr << "  end() unknown exception." << std::endl;
            }
            finished.store(true, std::memory_order_release);
            });

        auto start = steady_clock::now();
        while (!finished.load(std::memory_order_acquire)) {
            if (duration_cast<seconds>(steady_clock::now() - start).count() > timeout_seconds) {
                std::cerr << "  [TIMEOUT] end() blocked >" << timeout_seconds
                    << "s — DEADLOCK LIKELY!" << std::endl;
                end_thread.detach(); // 让它残留，进程退出时回收
                return false;
            }
            std::this_thread::sleep_for(milliseconds(50));
        }
        end_thread.join();
        return true;
    }

    // ============================================================
    // 1. 基本提交与结果
    // ============================================================
    void test_basic_submit() {
        std::cout << "[Test 1] Basic submit..." << std::endl;
        po::threadpool pool;
        pool.start(kBasicThreads);
        auto f = pool.enqueue(
            std::function<int(int, int)>([](int a, int b) { return a + b; }),
            kBasicA, kBasicB);
        assert(f.get() == kBasicA + kBasicB);
        std::cout << "  Passed." << std::endl;
    }

    // ============================================================
    // 2. 并发任务正确性
    // ============================================================
    void test_concurrent_correctness() {
        std::cout << "[Test 2] Concurrent tasks correctness..." << std::endl;
        po::threadpool pool;
        pool.start(kConcurrentThreads);
        std::vector<std::future<int>> futs;
        futs.reserve(kConcurrentTasks);
        for (int i = 0; i < kConcurrentTasks; ++i)
            futs.push_back(pool.enqueue(
                std::function<int(int)>([](int x) { return x * x; }), i));
        for (int i = 0; i < kConcurrentTasks; ++i)
            assert(futs[i].get() == i * i);
        std::cout << "  Passed: " << kConcurrentTasks << " tasks." << std::endl;
    }

    // ============================================================
    // 3. 原子递增（无数据竞争）
    // ============================================================
    void test_atomic_increment() {
        std::cout << "[Test 3] Atomic increment..." << std::endl;
        po::threadpool pool;
        pool.start(kAtomicThreads);
        std::atomic<int> cnt{ 0 };
        std::vector<std::future<void>> futs;
        futs.reserve(kAtomicTasks);
        for (int i = 0; i < kAtomicTasks; ++i)
            futs.push_back(pool.enqueue(
                std::function<void()>([&]() { cnt.fetch_add(1, std::memory_order_relaxed); })));
        for (auto& f : futs) f.get();
        assert(cnt == kAtomicTasks);
        std::cout << "  Passed: counter = " << cnt << std::endl;
    }

    // ============================================================
    // 4. 异常隔离
    // ============================================================
    void test_exception_isolation() {
        std::cout << "[Test 4] Exception isolation..." << std::endl;
        po::threadpool pool;
        pool.start(kExceptionThreads);
        auto good = pool.enqueue(std::function<int()>([] { return 42; }));
        auto bad = pool.enqueue(std::function<int()>([]() -> int {
            throw std::runtime_error("test error");
            }));
        assert(good.get() == 42);
        bool caught = false;
        try { bad.get(); }
        catch (const std::runtime_error& e) { caught = true; }
        assert(caught);
        std::cout << "  Passed." << std::endl;
    }

    // ============================================================
    // 5. 吞吐量（空任务）
    // ============================================================
    void test_throughput_computational() {
        std::cout << "[Test 5b] Throughput (~" << kTaskCostUs
            << "μs/task)..." << std::endl;

        po::threadpool pool;
        pool.start(kThroughputThreads);

        constexpr size_t total = 50000;  // 任务数减少，因为每个任务变重了
        std::atomic<size_t> counter{ 0 };

        auto task = [&counter]() {
            // 模拟有意义计算：质数判定（不可被编译器优化掉）
            int prime_check = 0;
            for (int n = 1000; n < 1100; ++n) {
                bool is_prime = true;
                for (int d = 2; d * d <= n; ++d) {
                    if (n % d == 0) { is_prime = false; break; }
                }
                if (is_prime) ++prime_check;
            }
            counter.fetch_add(prime_check, std::memory_order_relaxed);
            };

        auto t0 = high_resolution_clock::now();

        std::vector<std::future<void>> futs;
        futs.reserve(total);
        for (size_t i = 0; i < total; ++i) {
            futs.push_back(pool.enqueue(std::function<void()>(task)));
        }
        for (auto& f : futs) f.get();

        auto t1 = high_resolution_clock::now();
        auto ms = duration_cast<milliseconds>(t1 - t0).count();

        double tasks_per_sec = total / (ms / 1000.0);
        double avg_us_per_task = (ms * 1000.0) / total;

        std::cout << "  Tasks: " << total << ", time: " << ms << " ms" << std::endl;
        std::cout << "  Throughput: " << tasks_per_sec / 1000.0 << " K tasks/sec" << std::endl;
        std::cout << "  Avg time/task: " << avg_us_per_task << " μs" << std::endl;
        std::cout << "  (framework overhead: ~"
            << (avg_us_per_task - 15.0) << " μs, estimated real work: ~15μs)" << std::endl;
    }

    // ============================================================
    // 6. 满队列拒绝（验证 push 返回 false）
    // ============================================================
    void test_task_limit_rejection() {
        std::cout << "[Test 6] Task limit rejection..." << std::endl;
        po::threadpool pool;
        pool.start(kRejectThreads);

        std::vector<std::future<void>> futs;
        futs.reserve(kTaskMaximum);
        bool rejected = false;
        size_t accepted = 0;

        for (size_t i = 0; i < kTaskMaximum + kRejectExtra; ++i) {
            try {
                futs.push_back(pool.enqueue(std::function<void()>([]() {})));
                ++accepted;
            }
            catch (const std::runtime_error& e) {
                rejected = true;
                std::cout << "  Rejected after " << accepted << " tasks: " << e.what() << std::endl;
                break;
            }
        }
        for (auto& f : futs) f.get();
        assert(rejected);
        std::cout << "  Passed." << std::endl;
    }

    // ============================================================
    // 7. 高竞争（极少线程处理大量任务）
    // ============================================================
    void test_high_contention() {
        std::cout << "[Test 7] High contention..." << std::endl;
        po::threadpool pool;
        pool.start(kContentionThreads);
        std::atomic<int> cnt{ 0 };
        std::vector<std::future<void>> futs;
        futs.reserve(kContentionTasks);
        for (int i = 0; i < kContentionTasks; ++i)
            futs.push_back(pool.enqueue(
                std::function<void()>([&]() { cnt.fetch_add(1, std::memory_order_relaxed); })));
        for (auto& f : futs) f.get();
        assert(cnt == kContentionTasks);
        std::cout << "  Passed: " << cnt << " increments with "
            << kContentionThreads << " threads." << std::endl;
    }

    // ============================================================
    // 8. 多次吞吐量取平均
    // ============================================================
    void test_throughput_multiple_computational() {
        constexpr int runs = 5;
        constexpr size_t total = 20000;
        std::cout << "[Test 8b] Throughput " << runs << " runs (~"
            << kTaskCostUs << "μs/task)..." << std::endl;

        double total_tps = 0;

        for (int r = 0; r < runs; ++r) {
            po::threadpool pool;
            pool.start(kThroughputThreads);
            std::atomic<size_t> counter{ 0 };

            auto task = [&counter]() {
                int prime_check = 0;
                for (int n = 1000; n < 1100; ++n) {
                    bool is_prime = true;
                    for (int d = 2; d * d <= n; ++d) {
                        if (n % d == 0) { is_prime = false; break; }
                    }
                    if (is_prime) ++prime_check;
                }
                counter.fetch_add(prime_check, std::memory_order_relaxed);
                };

            auto t0 = high_resolution_clock::now();
            std::vector<std::future<void>> futs;
            futs.reserve(total);
            for (size_t i = 0; i < total; ++i)
                futs.push_back(pool.enqueue(std::function<void()>(task)));
            for (auto& f : futs) f.get();
            auto t1 = high_resolution_clock::now();

            auto ms = duration_cast<milliseconds>(t1 - t0).count();
            double kps = total / (ms / 1000.0) / 1000.0;
            total_tps += kps;
            std::cout << "  Run " << r + 1 << ": " << ms << " ms (" << kps << " K/s)" << std::endl;
        }
        std::cout << "  Average: " << total_tps / runs << " K tasks/sec" << std::endl;
    }

    // ============================================================
    // 9. 调度延迟测试（测量任务从入队到执行的时间）
    // ============================================================
    void test_scheduling_latency() {
        std::cout << "[Test 9] Scheduling latency..." << std::endl;
        po::threadpool pool;
        pool.start(kLatencyThreads);

        std::vector<long long> latencies;
        latencies.reserve(kLatencySamples);
        for (int i = 0; i < kLatencySamples; ++i) {
            auto t0 = high_resolution_clock::now();
            auto f = pool.enqueue(std::function<void()>([t0, &latencies]() {
                auto t1 = high_resolution_clock::now();
                latencies.push_back(duration_cast<nanoseconds>(t1 - t0).count());
                }));
            f.get(); // 等待完成以记录延迟
        }

        // 简单统计
        long long sum = 0, min_lat = LLONG_MAX, max_lat = 0;
        for (auto l : latencies) {
            sum += l;
            if (l < min_lat) min_lat = l;
            if (l > max_lat) max_lat = l;
        }
        double avg_ns = static_cast<double>(sum) / latencies.size();
        std::cout << "  Samples: " << latencies.size()
            << ", avg: " << avg_ns << " ns"
            << ", min: " << min_lat << " ns"
            << ", max: " << max_lat << " ns" << std::endl;
    }

    // ============================================================
    // 10. 混合负载测试（长短任务混合）
    // ============================================================
    void test_mixed_load_levels() {
        std::cout << "[Test 10b] Mixed load (short/long)..." << std::endl;
        po::threadpool pool;
        pool.start(8);

        constexpr int total = 2000;
        std::atomic<int> short_cnt{ 0 }, long_cnt{ 0 };

        auto short_task = [&]() {
            volatile int x = 0;
            for (int i = 0; i < 1000; ++i) x += i;  // ~5μs
            short_cnt.fetch_add(1);
            };
        auto long_task = [&]() {
            volatile int x = 0;
            for (int i = 0; i < 50000; ++i) x += i;  // ~200μs
            long_cnt.fetch_add(1);
            };

        std::vector<std::future<void>> futs;
        futs.reserve(total);
        for (int i = 0; i < total; ++i) {
            if (i % 3 == 0)  // 1/3 长任务，2/3 短任务
                futs.push_back(pool.enqueue(std::function<void()>(long_task)));
            else
                futs.push_back(pool.enqueue(std::function<void()>(short_task)));
        }
        for (auto& f : futs) f.get();

        std::cout << "  Short: " << short_cnt << ", long: " << long_cnt
            << " (total: " << short_cnt + long_cnt << ")" << std::endl;
    }

    // ============================================================
    // 11. end() 基本测试
    // ============================================================
    void test_end_basic() {
        std::cout << "[Test End 1] Basic end()..." << std::endl;
        po::threadpool pool;
        pool.start(kEndBasicThreads);

        std::vector<std::future<int>> futs;
        for (int i = 0; i < kEndBasicTasks; ++i)
            futs.push_back(pool.enqueue(
                std::function<int(int)>([](int x) { return x * 2; }), i));
        for (auto& f : futs) f.get();
        std::cout << "  All " << kEndBasicTasks << " tasks done, calling end..." << std::endl;

        bool ok = try_end_with_timeout(pool, kEndTimeoutSec);
        if (ok) {
            assert(!pool.is_running());
            std::cout << "  Passed: end() returned, pool stopped." << std::endl;
        }
        else {
            std::cout << "  FAILED: deadlock detected!" << std::endl;
        }
    }

    // ============================================================
    // 12. end() 压力测试（大量任务后立即 end）
    // ============================================================
    void test_end_stress() {
        std::cout << "[Test End 2] Stress end()..." << std::endl;
        po::threadpool pool;
        pool.start(kEndStressThreads);

        std::vector<std::future<void>> futs;
        futs.reserve(kEndStressTasks);
        for (size_t i = 0; i < kEndStressTasks; ++i)
            futs.push_back(pool.enqueue(std::function<void()>([]() {})));

        // 不等待任务完成，直接 end
        std::cout << "  Submitted " << kEndStressTasks << " tasks, calling end immediately..." << std::endl;
        bool ok = try_end_with_timeout(pool, kEndStressTimeout);
        if (ok) {
            std::cout << "  Passed: end() handled pending tasks." << std::endl;
        }
        else {
            std::cout << "  FAILED: deadlock under stress!" << std::endl;
        }
    }

    // ============================================================
    // 13. end() 后重启测试
    // ============================================================
    void test_end_restart() {
        std::cout << "[Test End 3] Restart after end()..." << std::endl;
        po::threadpool pool;
        pool.start(kEndRestartThreads);

        // 第一轮
        std::vector<std::future<int>> futs1;
        for (int i = 0; i < kEndRestartTasks1; ++i)
            futs1.push_back(pool.enqueue(
                std::function<int(int)>([](int x) { return x + 1; }), i));
        for (auto& f : futs1) f.get();
        bool ok1 = try_end_with_timeout(pool, kEndTimeoutSec);
        assert(ok1);
        assert(!pool.is_running());
        std::cout << "  First round ended." << std::endl;

        // 第二轮：重新 start
        pool.start(kEndRestartThreads);
        std::vector<std::future<int>> futs2;
        for (int i = 0; i < kEndRestartTasks2; ++i)
            futs2.push_back(pool.enqueue(
                std::function<int(int)>([](int x) { return x * 2; }), i));
        for (auto& f : futs2) f.get();
        bool ok2 = try_end_with_timeout(pool, kEndTimeoutSec);
        assert(ok2);
        std::cout << "  Restart and second round succeeded." << std::endl;
    }

    // ============================================================
    // 14. end() 空线程池（start 后立即 end）
    // ============================================================
    void test_end_empty() {
        std::cout << "[Test End 4] End immediately after start..." << std::endl;
        po::threadpool pool;
        pool.start(kEndEmptyThreads);
        bool ok = try_end_with_timeout(pool, kEndTimeoutSec);
        if (ok) {
            assert(!pool.is_running());
            std::cout << "  Passed: empty pool stopped cleanly." << std::endl;
        }
        else {
            std::cout << "  FAILED: deadlock on empty pool!" << std::endl;
        }
    }

    // ============================================================
    // 主测试入口（可在 main 中按需调用）
    // ============================================================
    void run_all_tests() {
        test_basic_submit();
        test_concurrent_correctness();
        test_atomic_increment();
        test_exception_isolation();
        test_throughput_computational();
        test_task_limit_rejection();
        test_high_contention();
        test_throughput_multiple_computational();
        test_scheduling_latency();
        test_mixed_load_levels();

        // end() 系列测试（注意：如果存在死锁，进程可能卡住）
        test_end_basic();
        test_end_stress();
        test_end_restart();
        test_end_empty();

        std::cout << "\n===== ALL TESTS PASSED =====" << std::endl;
    }

    // 如果只想跑性能测试或 end 测试，可单独调用相关函数

}