#include "core/menu/artwork_download.hpp"
#include "core/menu/artwork_release.hpp"
#include "core/menu/file_worker.hpp"
#include "core/log.hpp"
#include "util/https_download.hpp"
#include "util/file_hash.hpp"
#include "ext/json/json.hpp"
#include <array>
#include <fstream>
#include <set>

namespace iris::ui::artwork {
namespace {
struct Staging {
    std::filesystem::path path;
    explicit Staging(const std::filesystem::path& parent){
        std::filesystem::create_directories(parent);
        for(unsigned attempt=0;attempt<8;++attempt){
            path=parent/(".iris-artwork-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64())+"-"+std::to_string(attempt));
            if(std::filesystem::create_directory(path))return;
        }
        throw std::runtime_error("Cannot create private artwork staging directory.");
    }
    ~Staging(){std::error_code ec;std::filesystem::remove_all(path,ec);}
};
void read(std::istream& input,void* data,std::size_t size){
    if(!input.read(static_cast<char*>(data),static_cast<std::streamsize>(size)))throw std::runtime_error("Artwork pack is truncated.");
}
std::uint32_t integer(std::istream& input,unsigned size){
    std::array<unsigned char,4> bytes{};read(input,bytes.data(),size);
    std::uint32_t value{};for(unsigned i=0;i<size;++i)value|=std::uint32_t{bytes[i]}<<(8*i);return value;
}
}
bool valid_filename(std::string_view name){
    if(name=="manifest.json")return true;
    if(name.size()>80||!name.ends_with(".png"))return false;
    name.remove_suffix(4);const auto separator=name.find('_');
    if(separator==std::string_view::npos||separator==0||separator+1==name.size())return false;
    auto definition=name.substr(0,separator),paint=name.substr(separator+1);
    if(definition.starts_with('-'))definition.remove_prefix(1);
    const auto digits=[](std::string_view part){return !part.empty()&&part.find_first_not_of("0123456789")==std::string_view::npos;};
    return digits(definition)&&digits(paint);
}
bool complete(const std::filesystem::path& root){
    try {
        const auto manifest=nlohmann::json::parse(read_file(root/"manifest.json"));
        if(manifest.value("source","")!="installed-cs2-vpk"||manifest.value("version",0)!=1||!manifest.contains("images")||!manifest["images"].is_object()||manifest["images"].empty()||manifest["images"].size()>10000)return false;
        for(const auto& entry:manifest["images"].items()){
            const auto name=entry.key()+".png";if(!valid_filename(name))return false;
            std::error_code ec;const auto size=std::filesystem::file_size(root/name,ec);
            if(ec||size<8||size>4*1024*1024)return false;
        }
        return true;
    }catch(...){return false;}
}
void install(const std::filesystem::path& pack,const std::filesystem::path& destination,
             std::string_view expected_hash,const std::atomic_bool& cancelled){
    if(cancelled)throw std::runtime_error("Artwork installation cancelled.");
    if(sdk::hash_file(pack)!=expected_hash)throw std::runtime_error("Artwork SHA256 does not match this DLL release.");
    Staging staging(destination.parent_path());std::ifstream input(pack,std::ios::binary);
    std::array<char,8> magic{};read(input,magic.data(),magic.size());
    if(std::string_view(magic.data(),magic.size())!="IRISART1")throw std::runtime_error("Artwork pack format differs from this release.");
    const auto count=integer(input,4);if(!count||count>10000)throw std::runtime_error("Artwork pack file count is invalid.");
    std::set<std::string> names;std::array<char,65536> buffer{};
    for(unsigned file=0;file<count;++file){
        if(cancelled)throw std::runtime_error("Artwork installation cancelled.");
        const auto length=integer(input,2),size=integer(input,4);
        if(!length||length>80||size<8||size>4*1024*1024)throw std::runtime_error("Artwork pack entry is invalid.");
        std::string name(length,'\0');read(input,name.data(),length);
        if(!valid_filename(name)||!names.insert(name).second)throw std::runtime_error("Artwork pack filename is invalid or duplicated.");
        std::ofstream output(staging.path/name,std::ios::binary);
        for(std::uint32_t remaining=size;remaining;){
            if(cancelled)throw std::runtime_error("Artwork installation cancelled.");
            const auto chunk=std::min<std::size_t>(remaining,buffer.size());read(input,buffer.data(),chunk);
            output.write(buffer.data(),static_cast<std::streamsize>(chunk));remaining-=static_cast<std::uint32_t>(chunk);
        }
        output.flush();if(!output)throw std::runtime_error("Cannot stage artwork file.");
    }
    if(input.peek()!=std::char_traits<char>::eof()||!complete(staging.path))throw std::runtime_error("Artwork pack manifest or payload is incomplete.");
    if(cancelled)throw std::runtime_error("Artwork installation cancelled.");
    std::filesystem::create_directories(destination);
    // Every PNG is a validated leaf. Publish the manifest last, so a partial
    // install is detected and retried without touching profiles or bindings.
    for(const auto& name:names)if(name!="manifest.json"){
        if(cancelled)throw std::runtime_error("Artwork installation cancelled.");
        if(!MoveFileExW((staging.path/name).c_str(),(destination/name).c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("Cannot publish artwork file.");
    }
    if(!MoveFileExW((staging.path/"manifest.json").c_str(),(destination/"manifest.json").c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("Cannot publish artwork manifest.");
}
std::filesystem::path fetch(const std::filesystem::path& data_folder,const std::atomic_bool& cancelled){
    Staging download(data_folder/"assets");const auto pack=download.path/"artwork.bin";
    overlay_log("Inventory artwork missing or incomplete; downloading the pinned release pack on its worker.");
    http::download(L"github.com",download_path,pack,download_size,cancelled);
    const auto destination=data_folder/"assets/inventory-game";
    install(pack,destination,sha256,cancelled);
    overlay_log("Inventory artwork installed and verified; icons are ready.");
    return destination;
}
}
