#include "core/menu/config_io.hpp"
#include "core/module.hpp"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <ctime>
#include <stdexcept>
namespace iris::ui {
bool valid_config_name(const std::string& name){
    if(name.empty()||name.size()>64||name.front()==' '||name.back()==' ')return false;
    for(unsigned char c:name)if(!std::isalnum(c)&&c!=' '&&c!='-'&&c!='_')return false;
    std::string upper=name;for(auto& c:upper)c=static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    if(upper=="CON"||upper=="PRN"||upper=="AUX"||upper=="NUL")return false;
    if(upper.size()==4&&(upper.starts_with("COM")||upper.starts_with("LPT"))&&upper[3]>='1'&&upper[3]<='9')return false;return true;
}
bool default_config(const std::string& name){return text_matches(name.c_str(), "default") && name.size()==7;}
namespace {
std::filesystem::path config_path(const std::filesystem::path& folder,const std::string& name){return folder/(default_config(name)?"default.cfg":name+".json");}
void ensure_default(const std::filesystem::path& folder,bool inventory_only=false){
    std::filesystem::create_directories(folder);
    if(!std::filesystem::exists(folder/"default.cfg")){
        MenuState state;state.cosmetics.enabled=true;
        if(std::filesystem::exists(folder/"Default.json"))state=decode_config(read_file(folder/"Default.json"));
        atomic_write(folder/"default.cfg",inventory_only?encode_inventory_config(state):encode_config(state));
    }
}
std::vector<ConfigEntry> listing(const std::filesystem::path& folder,bool inventory_only=false){
    std::vector<ConfigEntry> result;ensure_default(folder,inventory_only);
    for(const auto& entry:std::filesystem::directory_iterator(folder))if(entry.is_regular_file()&&(entry.path().extension()==".json" || entry.path().filename()=="default.cfg") && !(entry.path().extension()==".json" && default_config(entry.path().stem().string()))){
        auto time=std::chrono::time_point_cast<std::chrono::system_clock::duration>(entry.last_write_time()-std::chrono::file_clock::now()+std::chrono::system_clock::now());std::time_t raw=std::chrono::system_clock::to_time_t(time);std::tm local{};localtime_s(&local,&raw);char text[32];std::strftime(text,sizeof(text),"%Y-%m-%d %H:%M",&local);result.push_back({entry.path().stem().string(),text});
    }
    std::sort(result.begin(),result.end(),[](const auto& a,const auto& b){return a.name<b.name;});return result;
}
}
ConfigIO::ConfigIO(std::filesystem::path root,bool inventory_only):folder_(std::move(root)/(inventory_only?"inventory-configs":"configs")),inventory_only_(inventory_only){}
void ConfigIO::complete(ConfigResult result){std::lock_guard lock(mutex_);completed_.push_back(std::move(result));}
void ConfigIO::request(ConfigAction action,std::string name,MenuState state,std::string argument){
    if(!worker_.push([this,action,name=std::move(name),state=std::move(state),argument=std::move(argument)]()mutable{
        ConfigResult result;result.action=action;result.name=name;
        const auto encode=[&](const MenuState& value){return inventory_only_?encode_inventory_config(value):encode_config(value);};
        try{
            const bool restore_name=action==ConfigAction::Restore && name.rfind("__")!=std::string::npos && valid_config_name(name.substr(0,name.rfind("__"))) && name.substr(name.rfind("__")+2).find_first_not_of("0123456789")==std::string::npos;
            if(action!=ConfigAction::List&&action!=ConfigAction::ListDeleted&&!restore_name&&!valid_config_name(name))throw std::runtime_error("Use 1-64 letters, digits, spaces, hyphens or underscores for the name");
            if(folder_.parent_path()==module::data_folder())module::migrate_data();
            ensure_default(folder_,inventory_only_);
            if(default_config(name)){name="default";result.name=name;}
            if(default_config(name) && (action==ConfigAction::Delete || action==ConfigAction::Rename))throw std::runtime_error("default.cfg cannot be deleted or renamed");
            auto path=config_path(folder_,name);
            switch(action){
            case ConfigAction::List:break;
            case ConfigAction::Autosave:case ConfigAction::Save:atomic_write(path,encode(state));result.hash=inventory_only_?inventory_state_hash(state):state_hash(state);break;
            case ConfigAction::Create:if(std::filesystem::exists(path))throw std::runtime_error("A config with this name already exists");atomic_write(path,encode(state));result.hash=inventory_only_?inventory_state_hash(state):state_hash(state);break;
            case ConfigAction::Load:result.state=decode_config(read_file(path));result.hash=inventory_only_?inventory_state_hash(*result.state):state_hash(*result.state);break;
            case ConfigAction::Import:if(std::filesystem::exists(path))throw std::runtime_error("A config with this name already exists");result.state=decode_config(argument);atomic_write(path,encode(*result.state));result.hash=inventory_only_?inventory_state_hash(*result.state):state_hash(*result.state);break;
            case ConfigAction::Export:result.clipboard=encode(decode_config(read_file(path)));break;
            case ConfigAction::Delete:{
                const auto trash=folder_/".deleted";std::filesystem::create_directories(trash);
                const auto stamp=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
                std::filesystem::rename(path,trash/(name+"__"+std::to_string(stamp)+".json"));break;
            }
            case ConfigAction::ListDeleted:{
                const auto trash=folder_/".deleted";
                if(std::filesystem::exists(trash))for(const auto& entry:std::filesystem::directory_iterator(trash))if(entry.is_regular_file()&&entry.path().extension()==".json") {
                    const auto stem=entry.path().stem().string();const auto separator=stem.rfind("__");
                    if(separator==std::string::npos)continue;
                    try {const auto stamp=std::stoll(stem.substr(separator+2));const auto now=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();if(now-stamp<=7ll*24*60*60*1000)result.entries.push_back({stem,stem.substr(0,separator)});}catch(const std::exception&){}
                }
                complete(std::move(result));return;
            }
            case ConfigAction::Restore:{
                const auto separator=name.rfind("__");if(separator==std::string::npos)throw std::runtime_error("Invalid deleted profile");
                const auto original=name.substr(0,separator);if(!valid_config_name(original))throw std::runtime_error("Invalid profile name");
                const auto target=config_path(folder_,original);if(std::filesystem::exists(target))throw std::runtime_error("A profile with this name already exists");
                std::filesystem::rename(folder_/".deleted"/(name+".json"),target);result.name=original;break;
            }
            case ConfigAction::Rename:case ConfigAction::Duplicate:{if(!valid_config_name(argument))throw std::runtime_error("Invalid new name");auto target=config_path(folder_,argument);if(std::filesystem::exists(target))throw std::runtime_error("A config with this name already exists");if(action==ConfigAction::Rename)std::filesystem::rename(path,target);else atomic_write(target,encode(decode_config(read_file(path))));result.name=argument;break;}
            }
            result.entries=listing(folder_,inventory_only_);
        }catch(const std::exception& e){result.error=e.what();}
        complete(std::move(result));
    },action==ConfigAction::Autosave?1:0))complete({action,name,"Config queue is full; try again"});
}
std::optional<ConfigResult> ConfigIO::poll(){std::unique_lock lock(mutex_,std::try_to_lock);if(!lock||completed_.empty())return {};auto result=std::move(completed_.front());completed_.pop_front();return result;}
}
