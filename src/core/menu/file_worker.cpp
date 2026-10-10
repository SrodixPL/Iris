#include "core/menu/file_worker.hpp"
#include "core/module.hpp"
#include <Windows.h>
#include <fstream>
#include <stdexcept>
namespace iris::ui {
FileWorker::FileWorker():thread_([this]{
    for(;;){std::function<void()> job;{
        std::unique_lock lock(mutex_);wake_.wait(lock,[this]{return stop_||!jobs_.empty();});
        if(jobs_.empty()&&stop_)return;job=std::move(jobs_.front().run);jobs_.pop_front();
    }job();}
}){}
FileWorker::~FileWorker(){{std::lock_guard lock(mutex_);stop_=true;}wake_.notify_one();if(thread_.joinable())thread_.join();}
bool FileWorker::push(std::function<void()> job,int key){
    std::lock_guard lock(mutex_);if(stop_)return false;
    if(key)for(auto& pending:jobs_)if(pending.key==key){pending.run=std::move(job);return true;}
    if(jobs_.size()>=32)return false;jobs_.push_back({key,std::move(job)});wake_.notify_one();return true;
}
std::filesystem::path executable_folder(){
    return module::folder();
}
std::string read_file(const std::filesystem::path& path){
    std::ifstream stream(path,std::ios::binary);if(!stream)throw std::runtime_error("Cannot read file: "+path.filename().string());
    stream.seekg(0,std::ios::end);auto size=stream.tellg();if(size<0||size>4*1024*1024)throw std::runtime_error("File exceeds the 4 MB limit");
    std::string data(static_cast<std::size_t>(size),'\0');stream.seekg(0);stream.read(data.data(),size);if(!stream)throw std::runtime_error("Cannot finish reading file");return data;
}
void atomic_write(const std::filesystem::path& path,const std::string& data){
    std::filesystem::create_directories(path.parent_path());const auto temp=std::filesystem::path(path.wstring()+L".tmp");
    {std::ofstream stream(temp,std::ios::binary|std::ios::trunc);if(!stream)throw std::runtime_error("Cannot create temporary file");stream.write(data.data(),data.size());stream.flush();if(!stream)throw std::runtime_error("Cannot write temporary file");}
    if(!MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){std::filesystem::remove(temp);throw std::runtime_error("Cannot replace destination file");}
}
}
