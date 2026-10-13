#pragma once
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>

namespace iris {
// One running job, one replaceable pending snapshot, one immutable result.
// Callbacks never wait for a result. Cancellation also rejects an in-flight job.
template<class Input,class Output,class Compute> class LatestWorker {
    struct Job {Input input;std::uint64_t epoch;};
    std::mutex mutex_;
    std::condition_variable_any wake_;
    std::optional<Job> pending_;
    std::uint64_t epoch_{};
    std::atomic<std::shared_ptr<const Output>> result_;
    Compute compute_;
    std::jthread thread_;
    void run(std::stop_token stop){
        while(!stop.stop_requested()){
            std::optional<Job> job;
            {std::unique_lock lock(mutex_);if(!wake_.wait(lock,stop,[&]{return pending_.has_value();}))return;job=std::move(pending_);pending_.reset();}
            std::shared_ptr<const Output> output;
            try {output=std::make_shared<const Output>(compute_(std::move(job->input)));}catch(...){}
            {std::lock_guard lock(mutex_);if(job->epoch==epoch_)result_.store(std::move(output));}
        }
    }
public:
    explicit LatestWorker(Compute compute):compute_(std::move(compute)),thread_([this](std::stop_token stop){run(stop);}){}
    ~LatestWorker(){thread_.request_stop();wake_.notify_all();thread_.join();}
    LatestWorker(const LatestWorker&)=delete;
    bool submit(Input input){
        std::unique_lock lock(mutex_,std::try_to_lock);if(!lock)return false;
        pending_.emplace(Job{std::move(input),epoch_});wake_.notify_one();return true;
    }
    void cancel(){std::lock_guard lock(mutex_);++epoch_;pending_.reset();result_.store(nullptr);}
    std::thread::id thread_id() const noexcept {return thread_.get_id();}
    std::shared_ptr<const Output> result() const noexcept {return result_.load();}
};
}
