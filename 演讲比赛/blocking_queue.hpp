#include"container.hpp"



namespace con {

    // ============================================================
    // 派生类：基于 std::queue + mutex + condition_variable 的阻塞队列
    // ============================================================
    template<typename T>
    class blocking_queue : public container<T> {
    public:
        blocking_queue() = default;
        ~blocking_queue() override = default;

        // ---- 实现基类接口 ----

        void push(const T& value) override {
            {
                std::lock_guard<std::mutex> lock(mtx_);
                queue_.push(value);
            }
            cv_.notify_one();   // 唤醒一个等待的消费者
        }

        void push(T&& value) override {
            {
                std::lock_guard<std::mutex> lock(mtx_);
                queue_.push(std::move(value));
            }
            cv_.notify_one();
        }

        // 阻塞直到有元素可取
        bool pop(T& value) override {
            std::unique_lock<std::mutex> lock(mtx_);
            cv_.wait(lock, [this] { return !queue_.empty(); });
            value = std::move(queue_.front());
            queue_.pop();
            return true;
        }

        // 非阻塞尝试出队
        bool try_pop(T& value) override {
            std::lock_guard<std::mutex> lock(mtx_);
            if (queue_.empty()) {
                return false;
            }
            value = std::move(queue_.front());
            queue_.pop();
            return true;
        }

        bool empty() const override {
            std::lock_guard<std::mutex> lock(mtx_);
            return queue_.empty();
        }

        size_t size() const override {
            std::lock_guard<std::mutex> lock(mtx_);
            return queue_.size();
        }

        // ---- 额外扩展功能 ----
        // 带超时的出队（等待 timeout 时长）
        template<typename Rep, typename Period>
        bool pop_wait(T& value, const std::chrono::duration<Rep, Period>& timeout) {
            std::unique_lock<std::mutex> lock(mtx_);
            if (!cv_.wait_for(lock, timeout, [this] { return !queue_.empty(); })) {
                return false;   // 超时
            }
            value = std::move(queue_.front());
            queue_.pop();
            return true;
        }

        // 批量取出所有元素
        template<typename OutputIt>
        size_t pop_all(OutputIt out) {
            std::lock_guard<std::mutex> lock(mtx_);
            size_t count = 0;
            while (!queue_.empty()) {
                *out++ = std::move(queue_.front());
                queue_.pop();
                ++count;
            }
            return count;
        }

    private:
        mutable std::mutex mtx_;               // mutable 允许 const 成员函数加锁
        std::queue<T> queue_;
        std::condition_variable cv_;
    };

}