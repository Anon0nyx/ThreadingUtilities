#include "ThreadingUtilities.h"
#include <iostream>
#include <utility> // For std::move
#include <string>
#include <future>
#include <queue>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <atomic>

class ThreadPool {
public:
  ThreadPool(size_t numThreads);
  ~ThreadPool();

  template<class F, class... Args>
  auto enqueue(F&& f, int priority, Args&&... args) -> std::future<typename std::result_of<F(Args...)>::type>;

private:
  struct Task {
    int priority;
    std::function<void()> func;

    bool operator<(const Task& other) const {
      return priority < other.priority;
    }
  };

  std::vector<std::thread> workers;
  std::priority_queue<Task> tasks;
  std::mutex queueMutex;
  std::condition_variable_any condition;
  std::atomic<bool> stop;
};

ThreadPool::ThreadPool(size_t numThreads) : stop(false) {
  std::cout << "Initializing Thread Pool with: " << std::to_string(numThreads) << " Worker Threads." << std::endl;
  for (size_t i = 0; i < numThreads; ++i) {
    workers.emplace_back([this] {
      for (;;) {
        Task task;

        {
          std::unique_lock<std::mutex> lock(this->queueMutex);
          this->condition.wait(lock, [this] { return this->stop || !this->tasks.empty(); });
          if (this->stop && this->tasks.empty()) return;
          task = std::move(this->tasks.top());
          this->tasks.pop();
        }

        try {
          task.func();
        }
        catch (const std::exception& e) {
          std::cerr << "Exception in thread pool task: " << e.what() << std::endl;
        }
        catch (...) {
          std::cerr << "Unknown exception in thread pool task" << std::endl;
        }
      }
      });
  }
}

ThreadPool::~ThreadPool() {
  {
    std::unique_lock<std::mutex> lock(queueMutex);
    stop = true;
  }
  condition.notify_all();
  for (std::thread& worker : workers) worker.join();
}

template<class F, class... Args>
auto ThreadPool::enqueue(F&& f, int priority, Args&&... args) -> std::future<typename std::result_of<F(Args...)>::type> {
  using return_type = typename std::result_of<F(Args...)>::type;

  auto task = std::make_shared<std::packaged_task<return_type()>>(
    std::bind(std::forward<F>(f), std::forward<Args>(args)...)
  );

  std::future<return_type> res = task->get_future();
  {
    std::unique_lock<std::mutex> lock(queueMutex);
    tasks.push({ priority, [task]() { (*task)(); } });
  }
  condition.notify_one();
  return res;
}

class Semaphore {
public:
  Semaphore(int count = 0);
  void acquire();
  void release();

private:
  std::mutex semaphoreMutex;
  std::condition_variable condition;
  int count;
};

Semaphore::Semaphore(int count) : count(count) {}

void Semaphore::acquire() {
  std::unique_lock<std::mutex> lock(semaphoreMutex);
  condition.wait(lock, [this] { return count > 0; });
  --count;
}

void Semaphore::release() {
  std::unique_lock<std::mutex> lock(semaphoreMutex);
  ++count;
  condition.notify_one();
}

