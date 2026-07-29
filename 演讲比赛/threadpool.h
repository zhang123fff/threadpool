#pragma once
#include<iostream>
#include<semaphore>
#include<future>
#include<memory>
#include<mutex>
#include<condition_variable>
#include<vector>
#include<queue>
#include<thread>
#include<atomic>
#include"container.hpp"
#include"blocking_queue.hpp"
//该线程池class threadpool已经通过测试，测试性能中等（任务容器是全局锁 + 框架开销大）
/*结果如下     ->[Test 1] Basic submit...
  Passed.
[Test 2] Concurrent tasks correctness...
  Passed: 2000 tasks.
[Test 3] Atomic increment...
  Passed: counter = 50000
[Test 4] Exception isolation...
  Passed.
[Test 5b] Throughput (~50μs/task)...
  Tasks: 50000, time: 72 ms
  Throughput: 694.444 K tasks/sec
  Avg time/task: 1.44 μs
  (framework overhead: ~-13.56 μs, estimated real work: ~15μs)
[Test 6] Task limit rejection...
  Passed.
[Test 7] High contention...
  Passed: 200000 increments with 2 threads.
[Test 8b] Throughput 5 runs (~50μs/task)...
  Run 1: 34 ms (588.235 K/s)
  Run 2: 33 ms (606.061 K/s)
  Run 3: 32 ms (625 K/s)
  Run 4: 30 ms (666.667 K/s)
  Run 5: 28 ms (714.286 K/s)
  Average: 640.05 K tasks/sec
[Test 9] Scheduling latency...
  Samples: 1000, avg: 4892.1 ns, min: 800 ns, max: 28600 ns
[Test 10b] Mixed load (short/long)...
  Short: 1333, long: 667 (total: 2000)
[Test End 1] Basic end()...
  All 100 tasks done, calling end...
  Passed: end() returned, pool stopped.
[Test End 2] Stress end()...
  Submitted 50000 tasks, calling end immediately...
  Passed: end() handled pending tasks.
[Test End 3] Restart after end()...
  First round ended.
  Restart and second round succeeded.
[Test End 4] End immediately after start...
  Passed: empty pool stopped cleanly.

===== ALL TESTS PASSED =====*/


namespace po {
	
	enum class pool_command{instant};
	class threadpool {
	private:

		static inline constexpr size_t task_maximum = 5000000;
		static inline constexpr size_t thread_maximum = 5000;
		
		using pool_task = std::function<void()>;
		using pool_task_container = con::container<pool_task>;
		using thread_uniptr = std::unique_ptr<std::thread>;

	private:
		//在push和pop会自增自减
		std::atomic<size_t> task_num;
		//在线程program函数会自增自减
		std::atomic<size_t> thread_num;

	private:

		std::vector<thread_uniptr> threads;
		
		//容器大小和maximum一致或者能扩展到maximum
		std::unique_ptr<pool_task_container> task_container;
	private:

		//running为核心标志，绝对不能出错
		std::atomic<bool> running;
		std::counting_semaphore<task_maximum> sem;

	private:

		//下列函数为底层封装函数 start
		//注意construct不涉及条件检查
		//construct默认基于当前数量额外创建
		void threads_contruct(size_t th_num);
		void set_flag(bool fl)noexcept;
		void semaphore_increase();
		void semaphore_decrease()noexcept;
		void task_num_increase()noexcept;
		void task_num_decrease()noexcept;
		void thread_num_increase()noexcept;
		void thread_num_decrease()noexcept;
		void set_thread_num(size_t num)noexcept;
		void add_thread_num(size_t num)noexcept;
		size_t get_thread_num()const noexcept;
		size_t get_task_num()const noexcept;

		//tasknum的原子cas控制,实现原子获取加自增
		bool compare_and_exchange_task_num_add()noexcept;

		//end
		//容器push和pop操作封装
		bool push(pool_task&& task);
		bool pop(pool_task& task);

		void thread_executing_program1(std::atomic<size_t>& th_num);
		//preperation无条件检测
		void start_preperation(size_t th_num);
		void exit_preperation();

	public:

		bool is_running()const noexcept;
		void start(size_t th_num);
		void end();
		//任务入队
		template<typename...Args, typename Ret>
		auto enqueue(const std::function<Ret(Args...)>& func, Args...args);

	public:

		threadpool(pool_command com);
		//初始化
		threadpool()noexcept;
		// 禁止拷贝
		threadpool(const threadpool&) = delete;
		threadpool& operator=(const threadpool&) = delete;

		// 禁止移动
		threadpool(threadpool&&) = delete;
		threadpool& operator=(threadpool&&) = delete;

		~threadpool();



	};

	template<typename...Args, typename Ret>
	auto threadpool::enqueue(const std::function<Ret(Args...)>& func, Args...args) {
		//检查
		if(!is_running())throw std::runtime_error("enqueue 失败: 线程池已经终止");

		//能通过检查的，都当成是结束时容器残留任务
		auto task_ptr = std::make_shared<std::packaged_task<Ret()>>
			(std::bind(func, std::forward<Args>(args)...));
		std::future<Ret> fut = task_ptr->get_future();

		

		if (!push([task_ptr]() {(*task_ptr)(); }))
			throw std::runtime_error("enqueue : push失败");
		return fut;
	}
}