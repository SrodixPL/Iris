#pragma once
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
namespace iris::ui {
class FileWorker {
    struct Job {int key;std::function<void()> run;};
    std::mutex mutex_;std::condition_variable wake_;std::deque<Job> jobs_;bool stop_{};std::thread thread_;
public:
    FileWorker();~FileWorker();
    bool push(std::function<void()> job,int coalesce_key=0);
};
std::filesystem::path executable_folder();
std::string read_file(const std::filesystem::path& path);
void atomic_write(const std::filesystem::path& path,const std::string& data);
}
