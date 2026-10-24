#pragma once
#include <atomic>
#include <algorithm>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <set>
#include <string>
#include <streambuf>
#include <cstdint>
namespace ra2engine::diag {
struct State {
    std::atomic<bool> enabled{false};
    std::mutex mutex;
    std::string dir;
    std::FILE* log=nullptr;
    std::FILE* assets=nullptr;
    std::size_t bytes=0, asset_bytes=0, limit=1024*1024;
    std::atomic<std::uint32_t> frame{0};
    std::set<std::string> seen;
};
// Process lifetime: stream buffers may still log during static destruction.
inline State& state(){static State* s=new State;return *s;}
inline void rotate(const char* name) {
    auto& s=state();std::error_code ec;
    const auto current=std::filesystem::u8path(s.dir+"/"+name+"-current.log");
    const auto previous=std::filesystem::u8path(s.dir+"/"+name+"-previous.log");
    std::filesystem::remove(previous,ec);ec.clear();
    std::filesystem::rename(current,previous,ec);
}
inline bool start(const std::string& dir,std::size_t limit=1024*1024) {
    auto& s=state();std::lock_guard<std::mutex> lock(s.mutex);
    s.enabled=false;if(s.log)std::fclose(s.log);if(s.assets)std::fclose(s.assets);
    s.log=s.assets=nullptr;s.dir=dir;s.limit=limit;s.bytes=0;s.asset_bytes=0;s.frame=0;s.seen.clear();
    std::error_code ec;std::filesystem::create_directories(std::filesystem::u8path(dir),ec);
    if(ec)return false;rotate("engine");rotate("assets");
    s.log=std::fopen((dir+"/engine-current.log").c_str(),"wb");
    s.assets=std::fopen((dir+"/assets-current.log").c_str(),"wb");
    if(s.log)std::setvbuf(s.log,nullptr,_IOLBF,4096);
    if(s.assets)std::setvbuf(s.assets,nullptr,_IOLBF,4096);
    s.enabled=s.log!=nullptr;return s.enabled;
}
inline void write(const char* text,std::size_t n) noexcept { try {
    auto& s=state();if(!s.enabled.load())return;
    std::lock_guard<std::mutex> lock(s.mutex);if(!s.log)return;
    if(s.bytes+n>s.limit){std::fclose(s.log);s.log=nullptr;rotate("engine");
        s.log=std::fopen((s.dir+"/engine-current.log").c_str(),"wb");
        if(s.log)std::setvbuf(s.log,nullptr,_IOLBF,4096);s.bytes=0;}
    if(!s.log)return;
    n=std::min(n,s.limit);std::fwrite(text,1,n,s.log);
    if(n && std::memchr(text,'\n',n))std::fflush(s.log);
    } catch (...) { /* Logging must never terminate the simulation. */ }
}
inline void note(const char* fmt,...) {
    if(!state().enabled.load())return;
    char body[3072];va_list args;va_start(args,fmt);std::vsnprintf(body,sizeof body,fmt,args);va_end(args);
    const auto ms=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    char line[3200];std::snprintf(line,sizeof line,"[%lldms frame=%u] %s\n",static_cast<long long>(ms),state().frame.load(),body);write(line,std::char_traits<char>::length(line));
}
inline std::uint64_t fingerprint(const void* data,std::size_t n) {
    auto p=static_cast<const unsigned char*>(data);std::uint64_t h=14695981039346656037ULL;
    for(std::size_t i=0;i<n;++i){h^=p[i];h*=1099511628211ULL;}return h;
}
inline void snapshot(const char* name,const std::string& text) noexcept { try {
    auto& s=state();if(!s.enabled.load())return;std::lock_guard<std::mutex> lock(s.mutex);
    // Fixed names only: callers never pass game data or user paths here.
    if(std::string(name)!="skirmish.txt")return;
    if(auto* f=std::fopen((s.dir+"/"+name).c_str(),"wb")){std::fwrite(text.data(),1,std::min<std::size_t>(text.size(),65536),f);std::fflush(f);std::fclose(f);}
    } catch (...) { /* Logging must never terminate the simulation. */ }
}
inline void asset(const char* kind,const std::string& name,const std::string& source,std::uint64_t size) noexcept { try {
    auto& s=state();if(!s.enabled.load())return;std::lock_guard<std::mutex> lock(s.mutex);
    if(!s.assets || s.seen.size()>=8192)return;
    const std::string key=std::string(kind)+" "+name.substr(0,400)+" <- "+source.substr(0,500);
    if(s.asset_bytes+key.size()+40>1024*1024)return;
    if(!s.seen.insert(key).second)return;
    std::fprintf(s.assets,"%s bytes=%llu\n",key.c_str(),static_cast<unsigned long long>(size));s.asset_bytes+=key.size()+40;
    } catch (...) { /* Logging must never terminate the simulation. */ }
}
inline void stop() {
    auto& s=state();std::lock_guard<std::mutex> lock(s.mutex);s.enabled=false;
    if(s.log)std::fclose(s.log);if(s.assets)std::fclose(s.assets);s.log=s.assets=nullptr;
}
class StreamBuffer : public std::streambuf {
    std::streamsize xsputn(const char* p,std::streamsize n) override {write(p,static_cast<std::size_t>(n));return n;}
    int overflow(int ch) override {if(ch!=traits_type::eof()){char c=static_cast<char>(ch);write(&c,1);}return traits_type::not_eof(ch);}
};
}
