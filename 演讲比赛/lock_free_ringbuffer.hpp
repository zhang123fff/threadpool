#include"container.hpp"



namespace con {

	template<typename T>
	class lock_free_ringbuffer : public container<T> {
	private:
		const size_t capacity;
		static inline constexpr size_t default_cap = 10000;
		std::atomic<size_t> count;

		std::atomic<size_t> rear;
		std::atomic<size_t> tail;

	private:
		std::unique_ptr<T[]> buf;
	private:
		



	public:
		void push(const T& value)override {

		}
		void push(T&& value)override {

		}
		bool pop(T& value) override {

		}
		bool try_pop(T& value) override {

		}
		bool empty() const noexcept override {

		}
		size_t size() const noexcept override {

		}

	public:
		
		lock_free_ringbuffer() 
			: capacity(default_cap), rear(0), tail(0), count(0) {

		}






	};










}