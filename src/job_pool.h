#pragma once
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

/// Background workers for slow things (downloads, decoding). Jobs return a
/// callback that runs on the UI thread in RunCompleted(), so UI state and GPU
/// textures are only ever touched there.
/// Workers are detached on destruction: closing the app never waits on a
/// stalled download (the process exit ends them).
class JobPool {
public:
    using Done = std::function<void()>;          // runs on the UI thread
    using Job = std::function<Done()>;           // runs on a worker

    explicit JobPool(int threads = 6) : m_s(std::make_shared<State>())
    {
        for (int i = 0; i < threads; ++i) {
            std::thread([s = m_s] {
                for (;;) {
                    Job job;
                    {
                        std::unique_lock<std::mutex> lock(s->m);
                        s->cv.wait(lock, [&] { return s->stop || !s->queue.empty(); });
                        if (s->stop) return;
                        job = std::move(s->queue.front());
                        s->queue.pop_front();
                        ++s->running;
                    }
                    Done done = job();
                    std::lock_guard<std::mutex> lock(s->m);
                    --s->running;
                    if (done) s->completed.push_back(std::move(done));
                }
            }).detach();
        }
    }

    ~JobPool()
    {
        std::lock_guard<std::mutex> lock(m_s->m);
        m_s->stop = true;
        m_s->queue.clear();
        m_s->cv.notify_all();
    }

    JobPool(const JobPool&) = delete;
    JobPool& operator=(const JobPool&) = delete;

    void Submit(Job job)
    {
        std::lock_guard<std::mutex> lock(m_s->m);
        m_s->queue.push_back(std::move(job));
        m_s->cv.notify_one();
    }

    /// Call once per frame on the UI thread.
    void RunCompleted()
    {
        std::vector<Done> done;
        {
            std::lock_guard<std::mutex> lock(m_s->m);
            done.swap(m_s->completed);
        }
        for (auto& d : done) d();
    }

    bool Busy() const
    {
        std::lock_guard<std::mutex> lock(m_s->m);
        return !m_s->queue.empty() || m_s->running > 0 || !m_s->completed.empty();
    }

private:
    struct State {
        mutable std::mutex m;
        std::condition_variable cv;
        std::deque<Job> queue;
        std::vector<Done> completed;
        int running = 0;
        bool stop = false;
    };
    std::shared_ptr<State> m_s;
};
