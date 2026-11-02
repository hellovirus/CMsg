// PyMsg Pro - C++ 版本 (飞鸽传书增强版)
// 编译命令: g++ -std=c++17 -pthread -o pymsg pymsg.cpp
// 依赖: nlohmann/json 头文件库 (https://github.com/nlohmann/json)
// 使用说明: 将 json.hpp 放入同目录或 -I 指定路径

#ifndef PYMSG_H
#define PYMSG_H

#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <set>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <sstream>
#include <algorithm>
#include <cstring>
#include <ctime>
#include <iomanip>
#include <random>
#include <fstream>
#include <filesystem>
#include <variant>

#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <windows.h>
    #pragma comment(lib, "ws2_32.lib")
    typedef int socklen_t;
    #define snprintf _snprintf
    #define localtime_r(a,b) localtime_s(b,a)
#else
    #include <sys/socket.h>
    #include <sys/stat.h>
    #include <arpa/inet.h>
    #include <netdb.h>
    #include <unistd.h>
    #include <errno.h>
    #include <net/if.h>
    #include <netinet/in.h>
    #include <sys/ioctl.h>
    #ifdef __linux__
        #include <linux/if_ether.h>
    #endif
    typedef int SOCKET;
    #define INVALID_SOCKET (-1)
    #define SOCKET_ERROR (-1)
    #define closesocket(s) close(s)
    #define WSAGetLastError() errno
    #define WSAEINVAL EINVAL
    #define WSAEWOULDBLOCK EWOULDBLOCK
    #define WSAEINTR EINTR
    #define WSAEISCONN EISCONN
    #define WSAENOTSOCK ENOTSOCK
    #define WSAEBADF EBADF
    #define WSAEACCES EACCES
    #define WSAEADDRINUSE EADDRINUSE
    #define WSAEADDRNOTAVAIL EADDRNOTAVAIL
    #define WSAEAFNOSUPPORT EAFNOSUPPORT
    #define WSAECONNREFUSED ECONNREFUSED
    #define WSAECONNRESET ECONNRESET
    #define WSAETIMEDOUT ETIMEDOUT
    #define WSAECONNABORTED ECONNABORTED
    #define WSAENETUNREACH ENETUNREACH
    #define WSAEHOSTUNREACH EHOSTUNREACH
    #define WSAEOPNOTSUPP EOPNOTSUPP
    #define WSAEPFNOSUPPORT EPFNOSUPPORT
    #define WSAEPROTONOSUPPORT EPROTONOSUPPORT
    #define WSAESOCKTNOSUPPORT ESOCKTNOSUPPORT
    #define WSAEPROTOTYPE EPROTOTYPE
    #define WSAENOBUFS ENOBUFS
    #define WSAEDESTADDRREQ EDESTADDRREQ
    #define WSAEMSGSIZE EMSGSIZE
    #define EWOULDBLOCK EWOULDBLOCK
    inline void WSACleanup() {}
    inline bool WSAStartup(WORD w, LPWSADATA lp) { return false; }
#endif

#include <nlohmann/json.hpp>
using json = nlohmann::json;

// ============================================================
// IPMsg 协议常量
// ============================================================
constexpr int IPMSG_VERSION = 1;
constexpr int IPMSG_DEFAULT_PORT = 2425;
constexpr int IPMSG_NOOPERATION = 0x00;
constexpr int IPMSG_BR_ENTRY = 0x01;
constexpr int IPMSG_BR_EXIT = 0x02;
constexpr int IPMSG_ANSENTRY = 0x03;
constexpr int IPMSG_BR_ABSENCE = 0x04;
constexpr int IPMSG_SENDMSG = 0x20;
constexpr int IPMSG_RECVMSG = 0x21;
constexpr int IPMSG_GETFILEDATA = 0x60;
constexpr int IPMSG_RELEASEFILES = 0x61;
constexpr int IPMSG_GETDIRFILES = 0x62;
constexpr int IPMSG_SENDCHECKOPT = 0x100;
constexpr int IPMSG_FILEATTACHOPT = 0x200000;
constexpr int IPMSG_ABSENCEOPT = 0x100;
constexpr int IPMSG_UTF8OPT = 0x800000;
constexpr int IPMSG_CAPUTF8OPT = 0x1000000;
constexpr int IPMSG_FILE_REGULAR = 0x01;
constexpr int IPMSG_FILE_DIR = 0x02;
constexpr int IPMSG_FILE_RETPARENT = 0x03;
constexpr int IPMSG_FILE_RONLYOPT = 0x100;
constexpr int IPMSG_CUSTOM_SHUTDOWN = 0x80;
constexpr int IPMSG_SHUTDOWN_CANCEL = 0x81;
constexpr int IPMSG_PING = 0x82;
constexpr int IPMSG_PONG = 0x83;
constexpr int IPMSG_FILE_RECV_OK = 0x84;
constexpr int IPMSG_MAC_QUERY = 0x85;
constexpr int IPMSG_MAC_REPLY = 0x86;
constexpr int IPMSG_TICKER_MSG = 0x90;
constexpr int IPMSG_BIGMSG = 0x91;
constexpr int IPMSG_MSG_ACK = 0x92;
constexpr int IPMSG_MSG_DONE = 0x93;
constexpr int IPMSG_TASK_VOID = 0x94;
constexpr int IPMSG_TASK_RECALL = 0x95;
constexpr int IPMSG_TASK_RECALL_REQ = 0x96;
constexpr int IPMSG_TASK_RECALL_ANS = 0x97;
constexpr int IPMSG_PYMSG_FLAG = 0x20000;

constexpr const char* MSG_KIND_TICKER = "ticker";
constexpr const char* MSG_KIND_BIG = "big";
constexpr double DUP_MSG_TTL = 60.0;

inline int get_command(int cmd) { return cmd & 0xFF; }
inline int get_flags(int cmd) { return cmd & 0xFFFFFF00; }

// ============================================================
// 工具函数
// ============================================================
inline std::string get_timestamp() {
    auto now = std::chrono::system_clock::now();
    auto tt = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    struct tm tm_buf;
    localtime_r(&tt, &tm_buf);
    char buf[64];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d.%03lld",
             tm_buf.tm_year+1900, tm_buf.tm_mon+1, tm_buf.tm_mday,
             tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec, ms.count());
    return std::string(buf);
}

inline std::string get_time_str() {
    auto now = std::chrono::system_clock::now();
    auto tt = std::chrono::system_clock::to_time_t(now);
    struct tm tm_buf;
    localtime_r(&tt, &tm_buf);
    char buf[32];
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d", tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec);
    return std::string(buf);
}

inline std::string format_size(uint64_t size) {
    const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    int idx = 0; double s = static_cast<double>(size);
    while (s >= 1024.0 && idx < 4) { s /= 1024.0; idx++; }
    char buf[32];
    if (idx == 0) snprintf(buf, sizeof(buf), "%llu%s", (unsigned long long)size, units[idx]);
    else snprintf(buf, sizeof(buf), "%.1f%s", s, units[idx]);
    return std::string(buf);
}

inline std::string format_duration(uint64_t sec) {
    char buf[64];
    uint64_t h = sec/3600, m = (sec%3600)/60, s = sec%60;
    if (h > 0) snprintf(buf, sizeof(buf), "%llu时%02d分%02d秒", (unsigned long long)h, (int)m, (int)s);
    else if (m > 0) snprintf(buf, sizeof(buf), "%d分%02d秒", (int)m, (int)s);
    else snprintf(buf, sizeof(buf), "%llu秒", (unsigned long long)s);
    return std::string(buf);
}

inline std::vector<std::string> split_str(const std::string& s, char delim) {
    std::vector<std::string> parts; std::istringstream iss(s); std::string p;
    while (std::getline(iss, p, delim)) parts.push_back(p);
    return parts;
}

// ============================================================
// IPMsg 数据包
// ============================================================
struct IPMsgPacket {
    int version = 1; int pno = 0; std::string sender; std::string host; int cmd = 0; std::string add;
    IPMsgPacket() = default;
    IPMsgPacket(int v, int p, const std::string& sender, const std::string& host, int cmd, const std::string& add)
        : version(v), pno(p), sender(sender), host(host), cmd(cmd), add(add) {}

    std::vector<char> encode() const {
        std::vector<char> buf;
        auto write_str = [&](const std::string& s) {
            int16_t len = static_cast<int16_t>(s.size());
            buf.insert(buf.end(), reinterpret_cast<char*>(&len), reinterpret_cast<char*>(&len)+2);
            if (!s.empty()) buf.insert(buf.end(), s.begin(), s.end());
        };
        int32_t v=version, p=pno, c=cmd; int32_t al=static_cast<int32_t>(add.size());
        buf.insert(buf.end(), reinterpret_cast<char*>(&v), reinterpret_cast<char*>(&v)+4);
        buf.insert(buf.end(), reinterpret_cast<char*>(&p), reinterpret_cast<char*>(&p)+4);
        write_str(sender); write_str(host);
        buf.insert(buf.end(), reinterpret_cast<char*>(&c), reinterpret_cast<char*>(&c)+4);
        buf.insert(buf.end(), reinterpret_cast<char*>(&al), reinterpret_cast<char*>(&al)+4);
        if (!add.empty()) buf.insert(buf.end(), add.begin(), add.end());
        return buf;
    }

    static IPMsgPacket decode(const std::vector<char>& data) {
        IPMsgPacket pkt;
        if (data.size() < 12) return pkt;
        int o = 0;
        memcpy(&pkt.version, data.data()+o, 4); o+=4;
        memcpy(&pkt.pno, data.data()+o, 4); o+=4;
        int16_t slen;
        memcpy(&slen, data.data()+o, 2); o+=2;
        if (slen>0 && (size_t)slen <= data.size()-o) { pkt.sender = std::string(data.begin()+o, data.begin()+o+slen); o+=slen; }
        memcpy(&slen, data.data()+o, 2); o+=2;
        if (slen>0 && (size_t)slen <= data.size()-o) { pkt.host = std::string(data.begin()+o, data.begin()+o+slen); o+=slen; }
        memcpy(&pkt.cmd, data.data()+o, 4); o+=4;
        int32_t al; memcpy(&al, data.data()+o, 4); o+=4;
        if (al>0 && (size_t)al <= data.size()-o) { pkt.add = std::string(data.begin()+o, data.begin()+o+al); o+=al; }
        return pkt;
    }
};

// ============================================================
// 对端信息
// ============================================================
struct PeerInfo {
    std::string ip, user, host, nick, group, mac, alias;
    int port = 2425, status_flags = 0, ctype = 0;
    bool is_local = false, offduty = false;
    PeerInfo() = default;
    PeerInfo(const std::string& ip, const std::string& user, const std::string& host, int port)
        : ip(ip), user(user), host(host), port(port) {}
    std::string shown_name() const {
        if (!alias.empty()) return alias;
        if (!nick.empty()) return nick;
        return user;
    }
    std::string display_name() const {
        return (shown_name().empty() ? user : shown_name()) + "(" + ip + ")";
    }
};

// ============================================================
// 文件信息
// ============================================================
struct FileInfo {
    int no = 0; std::string name; uint64_t size = 0; time_t mtime = 0; int type = 1; std::string content; int share_pno = 0;
    bool is_dir() const { return type == IPMSG_FILE_DIR; }
    bool is_regular() const { return type == IPMSG_FILE_REGULAR; }
    std::string encode() const {
        std::ostringstream o; o << no << ":" << name << ":" << size << ":" << mtime << ":" << type << ":" << content;
        return o.str();
    }
    static std::vector<FileInfo> parse(const std::string& add, int share_pno = 0) {
        std::vector<FileInfo> files;
        if (add.empty()) return files;
        std::istringstream iss(add); std::string token;
        while (std::getline(iss, token, '\0')) {
            if (token.empty()) continue;
            FileInfo fi; fi.share_pno = share_pno;
            auto fields = split_str(token, '\a');
            if (fields.size() >= 5) {
                try {
                    fi.no = std::stoi(fields[0]); fi.name = fields[1]; fi.size = std::stoull(fields[2]);
                    fi.mtime = static_cast<time_t>(std::stoull(fields[3])); fi.type = std::stoi(fields[4]);
                    if (fields.size() > 5) fi.content = fields[5];
                } catch (...) { continue; }
                files.push_back(fi);
            }
        }
        return files;
    }
};

// ============================================================
// 共享会话 / 传输任务 / 任务台账
// ============================================================
struct ShareEntry {
    int share_pno, file_id, attr; uint64_t size; time_t mtime;
    std::string path, name; bool is_dir;
    ShareEntry() : share_pno(0), file_id(0), attr(0), size(0), mtime(0), is_dir(false) {}
    ShareEntry(int p, int fid, const std::string& pth, const std::string& n, bool d, uint64_t sz, time_t mt, int a)
        : share_pno(p), file_id(fid), path(pth), name(n), is_dir(d), size(sz), mtime(mt), attr(a) {}
};

struct ShareSession {
    int pno = 0; time_t created = 0;
    std::map<int, ShareEntry> entries;
    std::set<std::string> hosts;
    ShareSession() = default;
    ShareSession(int p, const std::map<int, ShareEntry>& e, const std::vector<std::string>& h)
        : pno(p), entries(e), created(std::time(nullptr)) { for(auto& h2:h) hosts.insert(h2); }
};

struct TransferTask {
    static constexpr int IDLE=0, RUNNING=1, DONE=2, FAILED=3, CANCELLED=4;
    std::string tid, ip; int port=2425; FileInfo fi; std::string path, dir; int share_pno=0;
    int status=IDLE; uint64_t sent=0, total=0; double progress=0;
    time_t start_time=0, done_time=0; std::string error; std::atomic<bool> cancelled{false};
};

struct TaskRecord {
    std::string iid, role, pno, sender, receiver, message, state, kind;
    time_t created=0, confirmed_at=0, executed_at=0, voided_at=0, recalled_at=0, frozen_at=0;
    int remind_count=0, remind_interval=180; std::string executor;
};

// ============================================================
// 配置管理器
// ============================================================
class Config {
    std::string config_path; json data; std::mutex mtx;
public:
    Config() : config_path(get_data_dir()+"/config.json") { load(); }
    void set_path(const std::string& p) { config_path = p; }
    void load() {
        std::lock_guard<std::mutex> lock(mtx);
        try { std::ifstream f(config_path); if(f.is_open()) f >> data; } catch(...) { data = json::object(); }
    }
    void save() {
        std::lock_guard<std::mutex> lock(mtx);
        try { std::filesystem::create_directories(std::filesystem::path(config_path).parent_path()); std::ofstream f(config_path); if(f.is_open()) f << data.dump(2); } catch(...) {}
    }
    template<typename T> T get(const std::string& k, const T& d) {
        std::lock_guard<std::mutex> lock(mtx);
        auto it = data.find(k);
        if (it != data.end()) { try { return it->get<T>(); } catch(...) { return d; } }
        return d;
    }
    template<typename T> void set(const std::string& k, const T& v) {
        std::lock_guard<std::mutex> lock(mtx); data[k] = v; save();
    }
    json& operator[](const std::string& k) { return data[k]; }
};

// ============================================================
// 文件日志
// ============================================================
class FileLogger {
    std::ofstream log_file; std::mutex mtx;
public:
    FileLogger() { log_file.open("pymsg.log", std::ios::app); }
    ~FileLogger() { if(log_file.is_open()) log_file.close(); }
    void info(const std::string& m) { lock_log(); if(log_file.is_open()) log_file << "["<<get_timestamp()<<"] [INFO] "<<m<<"\n"; unlock_log(); }
    void debug(const std::string& m) { lock_log(); if(log_file.is_open()) log_file << "["<<get_timestamp()<<"] [DEBUG] "<<m<<"\n"; unlock_log(); }
    void warning(const std::string& m) { lock_log(); if(log_file.is_open()) log_file << "["<<get_timestamp()<<"] [WARN] "<<m<<"\n"; unlock_log(); }
    void error(const std::string& m) { lock_log(); if(log_file.is_open()) log_file << "["<<get_timestamp()<<"] [ERROR] "<<m<<"\n"; unlock_log(); }
private:
    void lock_log() { mtx.lock(); }
    void unlock_log() { mtx.unlock(); }
};

// ============================================================
// 全局聊天历史
// ============================================================
class GlobalChatHistory {
    std::vector<json> messages; std::mutex mtx; std::string history_path;
public:
    GlobalChatHistory() : history_path(get_data_dir()+"/global_chat.json") { load(); }
    void set_path(const std::string& p) { history_path = p; }
    void load() {
        std::lock_guard<std::mutex> lock(mtx);
        try { std::ifstream f(history_path); if(f.is_open()) f >> messages; } catch(...) { messages.clear(); }
    }
    void save() {
        std::lock_guard<std::mutex> lock(mtx);
        try { std::ofstream f(history_path); if(f.is_open()) f << json(messages).dump(1); } catch(...) {}
    }
    int add(const json& rec) {
        std::lock_guard<std::mutex> lock(mtx);
        messages.push_back(rec);
        if(messages.size()>10000) messages.erase(messages.begin(), messages.begin()+2000);
        save(); return (int)messages.size()-1;
    }
    std::vector<json> get_all() { std::lock_guard<std::mutex> lock(mtx); return messages; }
    void clear() { std::lock_guard<std::mutex> lock(mtx); messages.clear(); save(); }
    void clear_notifications() {
        std::lock_guard<std::mutex> lock(mtx);
        std::vector<json> r;
        for(auto& m:messages) { std::string k=m.value("kind",""); if(k!="sys"&&k!="notice") r.push_back(m); }
        messages = r; save();
    }
};

#endif // PYMSG_H
