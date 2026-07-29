#pragma once
// container.hpp
#ifndef CONTAINER_HPP
#define CONTAINER_HPP

#include <queue>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <cstddef>
#include<boost/lockfree/queue.hpp>
#include <iostream>
#include <chrono>
#include <atomic>
#include <cassert>
#include <vector>
#include <thread>
#include <future>
#include <cmath>


namespace con {

    // ============================================================
    // 基类：定义并发容器的抽象接口
    // ============================================================
    template<typename T>
    class container {
    public:
        virtual ~container() = default;

        // 禁止拷贝和移动（基类通常不可复制）
        container(const container&) = delete;
        container& operator=(const container&) = delete;
        container(container&&) = delete;
        container& operator=(container&&) = delete;

        // ---- 纯虚接口 ----
        virtual void push(const T& value) = 0;
        virtual void push(T&& value) = 0;
        virtual bool pop(T& value) = 0;          // 阻塞直到有元素
        virtual bool try_pop(T& value) = 0;      // 非阻塞尝试
        virtual bool empty() const = 0;
        virtual size_t size() const = 0;

    protected:
        container() = default;
    };

}

#endif // CONTAINER_HPP