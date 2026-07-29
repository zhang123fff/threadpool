#include"threadpool.h"


namespace po {
	threadpool::threadpool()noexcept 
		:task_num(0), thread_num(0), running(false), sem(0){}

	threadpool::threadpool(pool_command com):threadpool() {
		switch (com)
		{
		case pool_command::instant:

			break;
		default:
			break;
		}
	}

	threadpool::~threadpool() {
		if (is_running()) {
			exit_preperation();
		}
	}

	bool threadpool::is_running()const noexcept {
		return running.load(std::memory_order_acquire);
	}

	//threadnum自动加减
	void threadpool::thread_executing_program1(std::atomic<size_t>& th_num) {
		thread_num_increase();
		pool_task ta;
		while (is_running() || !task_container->empty()) {
			//这里可能有问题，pop可能抛出异常！！！！！！！！！
			//pop(ta);
			try {
				pop(ta);
				ta();
			}
			catch (std::exception& ex) {
				std::cerr << std::string(
					"线程thread_executing_program1执行异常,任务执行失败") + ex.what() << std::endl;
			}
			catch (...) {
				std::cerr << std::string(
					"线程thread_executing_program1出现非标准异常，任务执行失败") << std::endl;
			}
		}
		thread_num_decrease();
	}

	void threadpool::start_preperation(size_t th_num) {
		//无状态检测
		//开始准备

		task_container = std::make_unique<con::blocking_queue<pool_task>>();
		set_flag(true);
		threads_contruct(th_num);
	}

	void threadpool::exit_preperation() {
		//无条件检测
		//关闭,保留容器和vector容器空间，以便后续启动
		set_flag(false);
		size_t th_num = get_thread_num();

		//通过不断轮询，避免会出现容器为空，但线程阻塞的情况，
		//当容器为空时，就会放入一个任务，执行完后该线程会满足条件推出，之后每次放一个
		while (get_thread_num() != 0) {
			//if (get_task_num() != 0)continue;
			//只有在执行end终止线程池时才需要sleepfor（10毫秒）
			// 注意时间设置直接影响了退出时间
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
			if (th_num > get_thread_num()) {
				th_num = get_thread_num();
				std::this_thread::yield();
			}
			else push([]() {});
		}

		//象征性的进行join
		for (auto& i : threads) {
			if (i->joinable())i->join();
		}
		threads.clear();
		
	}

	void threadpool::start(size_t th_num) {
		if (th_num > thread_maximum || th_num == 0) {
			throw std::invalid_argument("start : th_num参数错误");
		}
		if (is_running()) {
			throw std::logic_error("start重复执行");
		}
		start_preperation(th_num);
	}

	void threadpool::end() {
		if (!is_running()) {
			throw std::logic_error("end : 线程池已经结束，重复操作");
		}
		exit_preperation();
	}

	void threadpool::set_flag(bool fl)noexcept {
		running.store(fl, std::memory_order_release);
	}

	void threadpool::semaphore_increase() {
		sem.release();
	}

	void threadpool::semaphore_decrease()noexcept {
		sem.acquire();
	}

	void threadpool::task_num_increase()noexcept {
		task_num.fetch_add(1, std::memory_order_release);
	}

	void threadpool::task_num_decrease()noexcept {
		task_num.fetch_sub(1, std::memory_order_release);
	}

	void threadpool::thread_num_increase()noexcept {
		thread_num.fetch_add(1, std::memory_order_release);
	}

	void threadpool::thread_num_decrease()noexcept {
		thread_num.fetch_sub(1, std::memory_order_release);
	}

	void threadpool::set_thread_num(size_t num)noexcept {
		thread_num.store(num, std::memory_order_release);
	}

	void threadpool::add_thread_num(size_t num)noexcept {
		thread_num.fetch_add(num, std::memory_order_acq_rel);
	}

	size_t threadpool::get_thread_num()const noexcept {
		return thread_num.load(std::memory_order_acquire);
	}

	size_t threadpool::get_task_num()const noexcept {
		return task_num.load(std::memory_order_acquire);
	}

	bool threadpool::compare_and_exchange_task_num_add()noexcept {
		size_t cur = get_task_num();
		do
		{
			if (cur >= task_maximum) {
				return false;
			}
		} while (!task_num.compare_exchange_weak(cur, cur + 1,
			std::memory_order_acq_rel,
			std::memory_order_relaxed));
		return true;
	}

	void threadpool::threads_contruct(size_t th_num) {
		for (size_t i = 0; i < th_num; i++) {
			threads.emplace_back(std::make_unique<std::thread>([this]() {
				this->thread_executing_program1(thread_num);
				}));
		}
	}


	//注意push和pop中有获取/释放信号量，获取task_num使其增加/减少，
	// 取出/放入容器三个并发控制步骤，注意其顺序
	bool threadpool::push(pool_task&& task) {
		if (!compare_and_exchange_task_num_add())return false;
		
		try {
			//要求容器->push抛出异常，必须要回滚
			task_container->push(std::move(task));
		}
		catch (...) {
			task_num_decrease();
			throw;
		}
		
		//由于上面的原子控制，sem_increase不会超过max
		semaphore_increase();
		return true;
	}

	bool threadpool::pop(pool_task& task) {
		bool tmp = false;
		semaphore_decrease();

		//尽量不抛出异常
		try {
			tmp = task_container->pop(task);
		}
		catch (...) {
			//由于上面已经减少了，该操作不会抛出异常
			semaphore_increase();
			throw;
		}
		task_num_decrease();
		
		
		return tmp;
	}
}
