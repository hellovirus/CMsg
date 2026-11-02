// PyMsg Pro - C++ 实现 (飞鸽传书增强版) - 网络层与核心引擎
// 编译命令: g++ -std=c++17 -pthread -o pymsg pymsg_main.cpp
// 依赖: nlohmann/json 头文件库

#include "pymsg.h"
#include <iostream>
#include <sstream>
#include <random>
#include <algorithm>
#include <thread>
#include <chrono>
#include <cstring>
#include <ctime>
#include <set>
#include <map>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
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
#endif

// ============================================================
// 网络工具函数实现
// ============================================================

#ifdef _WIN32
static bool wsinit_done = false;
static void ws_init() {
    if (!wsinit_done) { WSADATA wsa; WSAStartup(MAKEWORD(2,2), &wsa); wsinit_done = true; }
}
#else
static void ws_init() {}
#endif

static std::string get_local_ip() {
    ws_init();
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) return "127.0.0.1";
    sockaddr_in addr; memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET; addr.sin_port = htons(53);
    addr.sin_addr.s_addr = inet_addr("8.8.8.8");
    if (connect(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) >= 0) {
        sockaddr_in local; socklen_t len = sizeof(local);
        if (getsockname(sock, reinterpret_cast<sockaddr*>(&local), &len) >= 0) {
            closesocket(sock); return inet_ntoa(local.sin_addr);
        }
    }
    closesocket(sock);
    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) == 0) {
        hostent* he = gethostbyname(hostname);
        if (he && he->h_addr_list[0]) return std::string(inet_ntoa(*reinterpret_cast<in_addr*>(he->h_addr_list[0])));
    }
    return "127.0.0.1";
}

static std::string get_hostname() {
    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) == 0) return std::string(hostname);
    return "unknown";
}

static std::string get_username() {
#ifdef _WIN32
    char username[256]; DWORD size = sizeof(username);
    if (GetUserNameA(username, &size)) return std::string(username);
#else
    const char* user = getenv("USER");
    if (user) return std::string(user);
    struct passwd* pw = getpwuid(getuid());
    if (pw) return std::string(pw->pw_name);
#endif
    return "user";
}

static std::vector<std::string> get_subnet_broadcasts() {
    std::vector<std::string> bcasts;
    bcasts.push_back("255.255.255.255");
    std::string local_ip = get_local_ip();
    if (local_ip != "127.0.0.1" && local_ip != "") {
        size_t last_dot = local_ip.rfind('.');
        if (last_dot != std::string::npos) bcasts.push_back(local_ip.substr(0, last_dot) + ".255");
    }
    std::sort(bcasts.begin(), bcasts.end());
    bcasts.erase(std::unique(bcasts.begin(), bcasts.end()), bcasts.end());
    return bcasts;
}

static std::string get_local_mac(const std::string& target_ip = "") {
#ifdef _WIN32
    ULONG outbuf_size = 15000;
    PIP_ADAPTER_INFO adapter_info = (IP_ADAPTER_INFO*)malloc(outbuf_size);
    if (!adapter_info) return "";
    DWORD result = GetAdaptersInfo(adapter_info, &outbuf_size);
    if (result == ERROR_BUFFER_OVERFLOW) {
        free(adapter_info);
        adapter_info = (IP_ADAPTER_INFO*)malloc(outbuf_size);
        if (!adapter_info) return "";
        result = GetAdaptersInfo(adapter_info, &outbuf_size);
    }
    if (result == NO_ERROR) {
        for (IP_ADAPTER_INFO* adapter = adapter_info; adapter; adapter = adapter->Next) {
            if (adapter->AddressLength >= 6) {
                char mac[32];
                snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X",
                         adapter->Address[0], adapter->Address[1], adapter->Address[2],
                         adapter->Address[3], adapter->Address[4], adapter->Address[5]);
                free(adapter_info); return std::string(mac);
            }
        }
    }
    free(adapter_info);
#else
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) return "";
    ifreq ifr; memset(&ifr, 0, sizeof(ifr));
    ifr.ifr_addr.sa_family = AF_INET;
    const char* ifaces[] = {"eth0", "en0", "wlan0", "en1", nullptr};
    for (int i = 0; ifaces[i]; i++) {
        strncpy(ifr.ifr_name, ifaces[i], IFNAMSIZ-1);
        if (ioctl(sock, SIOCGIFHWADDR, &ifr) == 0) {
#ifdef __linux__
            struct sockaddr_dl* sdl = reinterpret_cast<struct sockaddr_dl*>(&ifr.ifr_addr);
            if (sdl->sdl_alen >= 6) {
                unsigned char* arp = reinterpret_cast<unsigned char*>(&sdl->sdl_data[0]);
                char mac[32];
                snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X",
                         arp[0], arp[1], arp[2], arp[3], arp[4], arp[5]);
                closesocket(sock); return std::string(mac);
            }
#endif
        }
    }
    closesocket(sock);
#endif
    return "";
}

static void send_wol(const std::string& mac, const std::string& bcast = "255.255.255.255", int port = 9) {
    SOCKET sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) return;
    int opt = 1; setsockopt(sock, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char*>(&opt), sizeof(opt));
    sockaddr_in addr; memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET; addr.sin_port = htons(port);
    addr.sin_addr.s_addr = inet_addr(bcast.c_str());
    std::vector<char> packet(6, '\xFF');
    std::string clean_mac = mac;
    clean_mac.erase(std::remove(clean_mac.begin(), clean_mac.end(), ':'), clean_mac.end());
    clean_mac.erase(std::remove(clean_mac.begin(), clean_mac.end(), '-'), clean_mac.end());
    for (int i = 0; i < 16 && i*2+1 < (int)clean_mac.size(); i++) {
        char h = clean_mac[i*2], l = clean_mac[i*2+1];
        char byte = (isdigit(h)?h-'0':toupper(h)-'A'+10)<<4 | (isdigit(l)?l-'0':toupper(l)-'A'+10);
        packet.push_back(byte);
    }
    sendto(sock, packet.data(), packet.size(), 0, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    closesocket(sock);
}

static std::string safe_component(const std::string& name) {
    std::string r = name;
    for (auto& c : r) { if (c=='/'||c=='\\'||c=='\0') c = '_'; }
    return r;
}

// ============================================================
// 路径与文件工具
// ============================================================
static std::string get_data_dir() {
#ifdef _WIN32
    const char* appdata = getenv("APPDATA");
    if (appdata) return std::string(appdata) + "/PyMsgPro";
    return "PyMsgPro";
#else
    const char* home = getenv("HOME");
    if (home) return std::string(home) + "/.PyMsgPro";
    return "~/.PyMsgPro";
#endif
}

static std::string get_config_path() { return get_data_dir() + "/config.json"; }
static std::string get_archive_dir() { return get_data_dir() + "/archive"; }
static std::string get_history_path() { return get_data_dir() + "/global_chat.json"; }
static std::string get_ticker_path() { return get_data_dir() + "/ticker_pending.json"; }
static std::string get_big_queue_path() { return get_data_dir() + "/big_pending.json"; }
static std::string get_ledger_path() { return get_data_dir() + "/task_ledger.json"; }
static std::string get_order_cfg_path() { return get_data_dir() + "/order_panel.json"; }

static std::string make_entry_payload(const std::string& nick, const std::string& group,
                                       const std::string& user, const std::string& host) {
    return "N:" + nick + ":G:" + group + ":U:" + user + ":H:" + host;
}

static std::tuple<std::string, std::string, std::string> split_entry_payload(const std::string& payload) {
    std::string nick, group, ext;
    auto parts = split_str(payload, ':');
    for (size_t i = 0; i + 1 < parts.size(); i += 2) {
        if (parts[i] == "N") nick = parts[i+1];
        else if (parts[i] == "G") group = parts[i+1];
        else if (parts[i] == "U") { /* skip */ }
        else if (parts[i] == "H") { /* skip */ }
        else ext += parts[i] + ":" + (i+1 < parts.size() ? parts[i+1] : "");
    }
    return {nick, group, ext};
}

static std::map<std::string, std::string> parse_entry_ext(const std::string& ext) {
    std::map<std::string, std::string> info;
    auto parts = split_str(ext, ':');
    for (size_t i = 0; i + 1 < parts.size(); i += 2) {
        info[parts[i]] = parts[i+1];
    }
    return info;
}

// ============================================================
// 归档日志
// ============================================================
static std::vector<std::string> archive_buf;
static std::mutex archive_mtx;

static std::string archive_path_str() {
    auto now = std::time(nullptr);
    struct tm tm_buf; localtime_r(&now, &tm_buf);
    char buf[16]; snprintf(buf, sizeof(buf), "%04d-%02d", tm_buf.tm_year+1900, tm_buf.tm_mon+1);
    return get_archive_dir() + "/" + std::string(buf) + ".jsonl";
}

static void archive_append(const json& rec) {
    std::string line;
    try { line = rec.dump(); } catch(...) { return; }
    { std::lock_guard<std::mutex> lock(archive_mtx); archive_buf.push_back(line); }
    std::lock_guard<std::mutex> lock(archive_mtx);
    if (archive_buf.empty()) return;
    std::vector<std::string> lines = archive_buf; archive_buf.clear();
    try {
        std::filesystem::create_directories(get_archive_dir());
        std::ofstream f(archive_path_str(), std::ios::app);
        if (f.is_open()) {
            for (auto& ln : lines) { f << ln << "\n"; }
            f.flush();
#ifdef _WIN32
            HANDLE hf = reinterpret_cast<HANDLE>(::_get_osfhandle(f.fileno()));
            if (hf != INVALID_HANDLE_VALUE) FlushFileBuffers(hf);
#else
            fsync(f.fileno());
#endif
        }
    } catch(...) {}
}

static std::vector<json> archive_query(const std::string& month = "", const std::string& ip = "",
                                        const std::string& keyword = "", const std::string& kind = "",
                                        int limit = 2000) {
    std::vector<json> out;
    try {
        std::filesystem::directory_iterator dir(get_archive_dir());
        std::vector<std::string> files;
        for (auto& entry : dir) {
            if (entry.path().extension() == ".jsonl") files.push_back(entry.path().string());
        }
        std::sort(files.rbegin(), files.rend());
        std::string kw = keyword; std::transform(kw.begin(), kw.end(), kw.begin(), ::tolower);
        std::string ip_s = ip;
        for (auto& fpath : files) {
            if (!month.empty() && fpath.find(month) == std::string::npos) continue;
            std::ifstream f(fpath); if (!f.is_open()) continue;
            std::string line; std::vector<std::string> lines;
            while (std::getline(f, line)) if (!line.empty()) lines.push_back(line);
            for (auto it = lines.rbegin(); it != lines.rend() && (int)out.size() < limit; ++it) {
                try {
                    json rec = json::parse(*it);
                    if (!kind.empty() && rec.value("kind","") != kind) continue;
                    if (!ip_s.empty() && rec.value("ip","").find(ip_s) == std::string::npos) continue;
                    if (!kw.empty()) { std::string b = rec.dump(); std::transform(b.begin(),b.end(),b.begin(),::tolower); if(b.find(kw)==std::string::npos) continue; }
                    out.push_back(rec);
                } catch(...) { continue; }
            }
        }
    } catch(...) {}
    return out;
}

static std::vector<std::string> archive_months() {
    std::vector<std::string> months;
    try {
        std::filesystem::directory_iterator dir(get_archive_dir());
        for (auto& entry : dir) {
            if (entry.path().extension() == ".jsonl") {
                std::string fname = entry.path().filename().string();
                if (fname.size() >= 8) months.push_back(fname.substr(0,7));
            }
        }
    } catch(...) {}
    std::sort(months.rbegin(), months.rend()); return months;
}

// ============================================================
// 传输队列
// ============================================================
class TransferQueue {
    std::map<std::string, TransferTask> tasks; std::mutex mtx;
public:
    void add_recv(const std::string& ip, int port, const FileInfo& fi, const std::string& path, int share_pno) {
        std::lock_guard<std::mutex> lock(mtx);
        TransferTask t;
        t.tid = std::to_string(std::hash<std::string>{}(fi.name+std::to_string(time(nullptr)))%100000);
        t.ip=ip; t.port=port; t.fi=fi; t.path=path; t.dir="receive"; t.share_pno=share_pno;
        t.status=TransferTask::RUNNING; t.start_time=std::time(nullptr);
        tasks[t.tid] = t;
    }
    void add_send(const std::string& ip, int port, const FileInfo& fi) {
        std::lock_guard<std::mutex> lock(mtx);
        TransferTask t;
        t.tid = std::to_string(std::hash<std::string>{}(fi.name+std::to_string(time(nullptr)))%100000);
        t.ip=ip; t.port=port; t.fi=fi; t.dir="send"; t.status=TransferTask::IDLE;
        t.start_time=std::time(nullptr); tasks[t.tid] = t;
    }
    TransferTask* get_send(int share_pno, int file_id=0) {
        std::lock_guard<std::mutex> lock(mtx);
        for (auto& kv : tasks)
            if (kv.second.share_pno==share_pno && kv.second.fi.no==file_id && kv.second.dir=="send") return &kv.second;
        return nullptr;
    }
    bool has_active() {
        std::lock_guard<std::mutex> lock(mtx);
        for (auto& kv : tasks) if (kv.second.status==TransferTask::RUNNING) return true;
        return false;
    }
    void cancel_all() {
        std::lock_guard<std::mutex> lock(mtx);
        for (auto& kv : tasks) { kv.second.cancelled=true; kv.second.status=TransferTask::CANCELLED; }
    }
    void upd_send(TransferTask* t, uint64_t sent, uint64_t total) {
        if (!t) return; t->sent=sent; t->total=total;
        if (total>0) t->progress=sent*100.0/total;
    }
    void mark_send_done(TransferTask* t, bool confirmed=true) {
        if (!t) return; t->status=confirmed?TransferTask::DONE:TransferTask::FAILED; t->done_time=std::time(nullptr);
    }
    void confirm_send(int share_pno, int file_id=0) {
        std::lock_guard<std::mutex> lock(mtx);
        for (auto& kv : tasks)
            if (kv.second.share_pno==share_pno && kv.second.fi.no==file_id) { kv.second.status=TransferTask::DONE; kv.second.done_time=std::time(nullptr); }
    }
    void fail_send(TransferTask* t, const std::string& err) {
        if (!t) return; t->status=TransferTask::FAILED; t->error=err; t->done_time=std::time(nullptr);
    }
    void cleanup_all() { std::lock_guard<std::mutex> lock(mtx); tasks.clear(); }
    std::vector<TransferTask> get_all() { std::lock_guard<std::mutex> lock(mtx); std::vector<TransferTask> out; for(auto& kv:tasks) out.push_back(kv.second); return out; }
};

// ============================================================
// 引擎类 - 核心网络引擎
// ============================================================
class Engine {
public:
    Config& cfg; FileLogger& flog;
    std::function<void(const PeerInfo&)> on_peer_add;
    std::function<void(const std::string&)> on_peer_rem;
    std::function<void(const std::string&, const std::string&, const std::string&)> on_recv_msg;
    std::function<void(const std::string&, const std::string&, const std::vector<FileInfo>&, int, int)> on_recv_files;
    std::function<void(const std::string&, const std::string&, int, const std::string&, int)> on_recv_ticker;
    std::function<void(const std::string&, const std::string&, int, const std::string&, int)> on_recv_big;
    std::function<void(const std::string&, int)> on_msg_ack;
    std::function<void(const std::string&, int)> on_msg_done;
    std::function<void(const std::string&, int, const std::string&)> on_task_void;
    std::function<void(const std::string&, int, const std::string&, int)> on_task_recall;
    std::function<void(const std::string&, int, const std::string&, int)> on_task_recall_req;
    std::function<void(const std::string&, int, bool, const std::string&)> on_task_recall_ans;
    std::function<void(const std::string&, const std::string&)> on_shutdown;
    std::function<void(const std::string&, int)> on_pong;
    std::function<void(int)> on_port_busy;

    std::string user, host, nick, group, ip, mac;
    int port; std::vector<std::string> bcasts;
    SOCKET udp_sock, tcp_sock;
    std::atomic<bool> running{false}, offline{false};
    int pno_counter = 0; std::mutex pno_mtx;
    std::map<std::string, PeerInfo> peers;
    std::map<std::string, time_t> ans_sent;
    std::map<int, ShareSession> shares; std::mutex shares_mtx;
    int fcnt = 1;
    std::string nickname; std::atomic<bool> offduty{false}; std::string offduty_msg;
    std::map<std::string, double> dup_msgs; double dup_clean_at = 0;

    TransferQueue* tq = nullptr;

    Engine(Config& cfg, FileLogger& flog) : cfg(cfg), flog(flog), udp_sock(INVALID_SOCKET), tcp_sock(INVALID_SOCKET) {
        user = cfg.get<std::string>("username", get_username());
        host = cfg.get<std::string>("hostname", get_hostname());
        nickname = cfg.get<std::string>("nickname", "");
        nick = nickname.empty() ? user : nickname;
        group = cfg.get<std::string>("group", "");
        port = cfg.get<int>("port", 9999);
        ip = get_local_ip();
        bcasts = get_subnet_broadcasts();
        mac = get_local_mac(ip);
        auto macs = cfg.get<std::map<std::string, std::string>>("mac_addresses", {});
        if (!mac.empty()) { macs[ip] = mac; cfg.set("mac_addresses", macs); }
        flog.info("Engine init: " + user + "@" + host + " IP=" + ip + " Port=" + std::to_string(port));
    }

    ~Engine() { stop(); }

    int next_pno() { std::lock_guard<std::mutex> l(pno_mtx); pno_counter = (pno_counter+1)&0xFFFFFFFF; return pno_counter; }

    bool _is_self(const std::string& addr, int port=0) {
        try { if ((addr==this->ip || addr=="127.0.0.1") && (port==0 || port==this->port)) return true; } catch(...) {}
        return false;
    }

    bool send_pkt(const IPMsgPacket& pkt, const std::string& addr, int port=0) {
        if (offline.load()) return false;
        if (_is_self(addr, port)) return false;
        try {
            std::vector<char> data = pkt.encode();
            int p = port ? port : this->port;
            sockaddr_in addr_in; memset(&addr_in, 0, sizeof(addr_in));
            addr_in.sin_family = AF_INET; addr_in.sin_port = htons(p);
            addr_in.sin_addr.s_addr = inet_addr(addr.c_str());
            if (addr_in.sin_addr.s_addr == INADDR_NONE) return false;
            sendto(udp_sock, data.data(), (int)data.size(), 0, reinterpret_cast<sockaddr*>(&addr_in), sizeof(addr_in));
            return true;
        } catch(...) { return false; }
    }

    bool start() {
        running = true; offline = false;
        udp_sock = socket(AF_INET, SOCK_DGRAM, 0);
        if (udp_sock < 0) { running=false; flog.error("UDP socket create failed"); return false; }
        int opt = 1; setsockopt(udp_sock, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char*>(&opt), sizeof(opt));
        sockaddr_in udp_addr; memset(&udp_addr, 0, sizeof(udp_addr));
        udp_addr.sin_family = AF_INET; udp_addr.sin_port = htons(port); udp_addr.sin_addr.s_addr = INADDR_ANY;
        if (bind(udp_sock, reinterpret_cast<sockaddr*>(&udp_addr), sizeof(udp_addr)) < 0) {
            closesocket(udp_sock); udp_sock = INVALID_SOCKET; running=false;
            flog.error("Port "+std::to_string(port)+" bind failed");
            if (on_port_busy) on_port_busy(port); return false;
        }
#ifdef _WIN32
        u_long nonblock = 1; ioctlsocket(udp_sock, FIONBIO, &nonblock);
#else
        fcntl(udp_sock, F_SETFL, O_NONBLOCK);
#endif

        tcp_sock = socket(AF_INET, SOCK_STREAM, 0);
        if (tcp_sock < 0) { flog.error("TCP socket create failed"); return false; }
        int reuse = 1; setsockopt(tcp_sock, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));
        sockaddr_in tcp_addr; memset(&tcp_addr, 0, sizeof(tcp_addr));
        tcp_addr.sin_family = AF_INET; tcp_addr.sin_port = htons(port); tcp_addr.sin_addr.s_addr = INADDR_ANY;
        if (bind(tcp_sock, reinterpret_cast<sockaddr*>(&tcp_addr), sizeof(tcp_addr)) < 0) {
            closesocket(tcp_sock); tcp_sock = INVALID_SOCKET;
            flog.error("TCP port "+std::to_string(port)+" bind failed"); return false;
        }
#ifdef _WIN32
        nonblock = 1; ioctlsocket(tcp_sock, FIONBIO, &nonblock);
#else
        fcntl(tcp_sock, F_SETFL, O_NONBLOCK);
#endif
        listen(tcp_sock, 20);

        PeerInfo local(ip, user, host, port); local.nick=nick; local.group=group; local.mac=mac; local.is_local=true;
        peers[ip] = local;
        if (on_peer_add) on_peer_add(local);
        send_entry();
        flog.info("Engine started: " + ip + ":" + std::to_string(port));

        // 启动UDP监听线程
        std::thread([this]() { _udp_loop(); }).detach();
        // 启动TCP监听线程
        std::thread([this]() { _tcp_loop(); }).detach();
        // 启动共享清理线程
        std::thread([this]() { _share_cleanup_loop(); }).detach();
        // 启动入口广播线程
        std::thread([this]() { _entry_loop(); }).detach();

        return true;
    }

    void stop() {
        running = false;
        send_exit();
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        if (udp_sock != INVALID_SOCKET) { closesocket(udp_sock); udp_sock = INVALID_SOCKET; }
        if (tcp_sock != INVALID_SOCKET) { closesocket(tcp_sock); tcp_sock = INVALID_SOCKET; }
    }

    void go_offline() { offline=true; send_exit(); flog.info("Offline"); }
    void go_online() { offline=false; send_entry(); flog.info("Online"); }

    void send_entry() {
        std::string payload = make_entry_payload(nick, group, user, host);
        IPMsgPacket pkt(IPMSG_VERSION, next_pno(), user, host,
                       IPMSG_BR_ENTRY|IPMSG_CAPUTF8OPT|IPMSG_FILEATTACHOPT|IPMSG_PYMSG_FLAG, payload);
        for (auto& b : bcasts) send_pkt(pkt, b);
    }

    void send_entry_to(const std::string& ip, int port) {
        std::string payload = make_entry_payload(nick, group, user, host);
        IPMsgPacket pkt(IPMSG_VERSION, next_pno(), user, host,
                       IPMSG_BR_ENTRY|IPMSG_CAPUTF8OPT|IPMSG_FILEATTACHOPT|IPMSG_PYMSG_FLAG, payload);
        send_pkt(pkt, ip, port);
    }

    void send_exit() {
        IPMsgPacket pkt(IPMSG_VERSION, next_pno(), user, host, IPMSG_BR_EXIT, "");
        for (auto& b : bcasts) send_pkt(pkt, b);
    }

    void send_absence(const std::string& ip="", int port=0) {
        std::string payload = make_entry_payload(nick, group, user, host);
        IPMsgPacket pkt(IPMSG_VERSION, next_pno(), user, host,
                       IPMSG_BR_ABSENCE|IPMSG_ABSENCEOPT|IPMSG_CAPUTF8OPT|IPMSG_FILEATTACHOPT|IPMSG_PYMSG_FLAG, payload);
        if (!ip.empty()) send_pkt(pkt, ip, port);
        else for (auto& b : bcasts) send_pkt(pkt, b);
    }

    void send_ans(const std::string& ip, int port) {
        std::string payload = make_entry_payload(nick, group, user, host);
        send_pkt(IPMsgPacket(IPMSG_VERSION, next_pno(), user, host,
                            IPMSG_ANSENTRY|IPMSG_CAPUTF8OPT|IPMSG_UTF8OPT|IPMSG_FILEATTACHOPT|IPMSG_PYMSG_FLAG, payload), ip, port);
    }

    int send_msg(const std::string& ip, int port, const std::string& msg) {
        IPMsgPacket pkt(IPMSG_VERSION, next_pno(), user, host,
                       IPMSG_SENDMSG|IPMSG_SENDCHECKOPT|IPMSG_CAPUTF8OPT|IPMSG_UTF8OPT, msg);
        send_pkt(pkt, ip, port); return pkt.pno;
    }

    void broadcast_msg(const std::string& msg) {
        IPMsgPacket pkt(IPMSG_VERSION, next_pno(), user, host, IPMSG_SENDMSG|IPMSG_CAPUTF8OPT|IPMSG_UTF8OPT, msg);
        for (auto& b : bcasts) send_pkt(pkt, b);
        _mark_own_broadcast(pkt.pno);
    }

    void send_recv(const std::string& ip, int port, int pno) {
        send_pkt(IPMsgPacket(IPMSG_VERSION, next_pno(), user, host, IPMSG_RECVMSG, std::to_string(pno)), ip, port);
    }

    void send_release_files(const std::string& ip, int port, int share_pno) {
        send_pkt(IPMsgPacket(IPMSG_VERSION, next_pno(), user, host, IPMSG_RELEASEFILES|IPMSG_UTF8OPT, std::to_string(share_pno)), ip, port);
    }

    void send_mac_query(const std::string& ip, int port) {
        send_pkt(IPMsgPacket(IPMSG_VERSION, next_pno(), user, host, IPMSG_MAC_QUERY|IPMSG_CAPUTF8OPT, ""), ip, port);
    }

    void send_mac_reply(const std::string& ip, int port) {
        send_pkt(IPMsgPacket(IPMSG_VERSION, next_pno(), user, host, IPMSG_MAC_REPLY|IPMSG_CAPUTF8OPT, mac), ip, port);
    }

    std::vector<FileInfo> send_files(const std::string& ip, int port, const std::string& msg, const std::vector<std::string>& paths) {
        std::vector<FileInfo> fis; std::map<int, ShareEntry> entries; int pno = next_pno();
        for (auto& raw_path : paths) {
            std::string path; try { path = std::filesystem::canonical(raw_path).string(); } catch(...) { continue; }
            if (!std::filesystem::exists(path)) continue;
            std::string name = safe_component(std::filesystem::basename(path));
            auto st = std::filesystem::status(path);
            bool is_dir = std::filesystem::is_directory(st);
            int attr = is_dir ? IPMSG_FILE_DIR : IPMSG_FILE_REGULAR;
            uint64_t size = is_dir ? 0 : std::filesystem::file_size(path);
            int no; { std::lock_guard<std::mutex> l(pno_mtx); no = fcnt++; }
            entries[no] = ShareEntry(pno, no, path, name, is_dir, size,
                std::filesystem::last_write_time(path).time_since_epoch().count(), attr);
            fis.push_back(FileInfo{no, name, size, 0, attr, "", pno});
        }
        if (fis.empty()) return fis;
        { std::lock_guard<std::mutex> l(shares_mtx); shares[pno] = ShareSession(pno, entries, std::vector<std::string>{ip}); }
        std::string file_list;
        for (auto& f : fis) file_list += f.encode() + "\a";
        file_list += "\0";
        int cmd = IPMSG_SENDMSG|IPMSG_FILEATTACHOPT|IPMSG_SENDCHECKOPT|IPMSG_CAPUTF8OPT|IPMSG_UTF8OPT;
        send_pkt(IPMsgPacket(IPMSG_VERSION, pno, user, host, cmd, msg+"\0"+file_list), ip, port);
        flog.info("Files published: pno="+std::to_string(pno));
        return fis;
    }

    void send_shutdown(const std::string& ip, int port) {
        send_pkt(IPMsgPacket(IPMSG_VERSION, next_pno(), user, host, IPMSG_CUSTOM_SHUTDOWN|IPMSG_CAPUTF8OPT, "SHUTDOWN:30:"+user), ip, port);
    }

    void send_ping(const std::string& ip, int port) {
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
        send_pkt(IPMsgPacket(IPMSG_VERSION, next_pno(), user, host, IPMSG_PING|IPMSG_CAPUTF8OPT, std::to_string(ms)), ip, port);
    }

    void send_pong(const std::string& ip, int port, const std::string& ts) {
        send_pkt(IPMsgPacket(IPMSG_VERSION, next_pno(), user, host, IPMSG_PONG|IPMSG_CAPUTF8OPT, ts), ip, port);
    }

    int _send_tracked(int cmd, const std::string& ip, int port, const std::string& msg) {
        int pno = next_pno();
        send_pkt(IPMsgPacket(IPMSG_VERSION, pno, user, host,
                            cmd|IPMSG_CAPUTF8OPT|IPMSG_UTF8OPT|IPMSG_PYMSG_FLAG,
                            std::to_string(pno)+":"+msg), ip, port);
        return pno;
    }

    int send_ticker_msg(const std::string& ip, int port, const std::string& msg) { return _send_tracked(IPMSG_TICKER_MSG, ip, port, msg); }

    int send_ticker_msg_multi(const std::vector<std::pair<std::string,int>>& targets, const std::string& msg) {
        int pno = next_pno();
        IPMsgPacket pkt(IPMSG_VERSION, pno, user, host, IPMSG_TICKER_MSG|IPMSG_CAPUTF8OPT|IPMSG_UTF8OPT|IPMSG_PYMSG_FLAG, std::to_string(pno)+":"+msg);
        for (auto& [ip, port] : targets) send_pkt(pkt, ip, port);
        return pno;
    }

    void _mark_own_broadcast(int pno) {
        try { dup_msgs[ip+":"+std::to_string(pno)] = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); } catch(...) {}
    }

    int broadcast_tracked(int cmd, const std::string& msg) {
        int pno = next_pno();
        IPMsgPacket pkt(IPMSG_VERSION, pno, user, host, cmd|IPMSG_CAPUTF8OPT|IPMSG_UTF8OPT|IPMSG_PYMSG_FLAG, std::to_string(pno)+":"+msg);
        for (auto& b : bcasts) send_pkt(pkt, b);
        _mark_own_broadcast(pno); return pno;
    }

    int broadcast_ticker_msg(const std::string& msg) { return broadcast_tracked(IPMSG_TICKER_MSG, msg); }

    int on_duty_peer_count() {
        int c = 0; for (auto& [k,v] : peers) if (!v.is_local && !v.offduty) c++; return c;
    }

    int send_big_msg(const std::string& ip, int port, const std::string& msg) { return _send_tracked(IPMSG_BIGMSG, ip, port, msg); }

    void send_msg_ack(const std::string& ip, int port, int orig_pno) {
        send_pkt(IPMsgPacket(IPMSG_VERSION, next_pno(), user, host, IPMSG_MSG_ACK|IPMSG_CAPUTF8OPT|IPMSG_PYMSG_FLAG, std::to_string(orig_pno)), ip, port);
    }

    void send_msg_done(const std::string& ip, int port, int orig_pno) {
        send_pkt(IPMsgPacket(IPMSG_VERSION, next_pno(), user, host, IPMSG_MSG_DONE|IPMSG_CAPUTF8OPT|IPMSG_PYMSG_FLAG, std::to_string(orig_pno)), ip, port);
    }

    void send_task_void(const std::string& ip, int port, int orig_pno, const std::string& reason="") {
        std::string add = std::to_string(orig_pno)+":"+reason;
        send_pkt(IPMsgPacket(IPMSG_VERSION, next_pno(), user, host, IPMSG_TASK_VOID|IPMSG_CAPUTF8OPT|IPMSG_PYMSG_FLAG, add), ip, port);
    }

    void send_task_recall(const std::string& ip, int port, int orig_pno, const std::string& reason="") {
        std::string add = std::to_string(orig_pno)+":"+reason;
        send_pkt(IPMsgPacket(IPMSG_VERSION, next_pno(), user, host, IPMSG_TASK_RECALL|IPMSG_CAPUTF8OPT|IPMSG_PYMSG_FLAG, add), ip, port);
    }

    void send_task_recall_req(const std::string& ip, int port, int orig_pno, const std::string& reason="") {
        std::string add = std::to_string(orig_pno)+":"+reason;
        send_pkt(IPMsgPacket(IPMSG_VERSION, next_pno(), user, host, IPMSG_TASK_RECALL_REQ|IPMSG_CAPUTF8OPT|IPMSG_PYMSG_FLAG, add), ip, port);
    }

    void send_task_recall_ans(const std::string& ip, int port, int orig_pno, bool agree, const std::string& reason="") {
        std::string add = std::to_string(orig_pno)+":"+(agree?"1":"0")+":"+reason;
        send_pkt(IPMsgPacket(IPMSG_VERSION, next_pno(), user, host, IPMSG_TASK_RECALL_ANS|IPMSG_CAPUTF8OPT|IPMSG_PYMSG_FLAG, add), ip, port);
    }

    static std::pair<int,std::string> _split_pno_reason(const std::string& add) {
        if (add.find(':')==std::string::npos) { try { return {std::stoi(add),""}; } catch(...) { return {0,""}; } }
        auto pos = add.find(':'); std::string pno_s = add.substr(0,pos); std::string reason = add.substr(pos+1);
        try { return {std::stoi(pno_s), reason}; } catch(...) { return {0, reason}; }
    }

private:
    void _udp_loop() {
        while (running) {
            char buf[65535]; sockaddr_in from; socklen_t from_len = sizeof(from);
            int n = recvfrom(udp_sock, buf, sizeof(buf), 0, reinterpret_cast<sockaddr*>(&from), &from_len);
            if (n > 0) {
                std::vector<char> data(buf, buf+n);
                IPMsgPacket pkt = IPMsgPacket::decode(data);
                if (pkt.version > 0) {
                    std::string peer_ip = inet_ntoa(from.sin_addr);
                    int peer_port = ntohs(from.sin_port);
                    std::thread([this, pkt, peer_ip, peer_port]() { _handle(pkt, peer_ip, peer_port); }).detach();
                }
            } else { std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
        }
    }

    void _tcp_loop() {
        while (running) {
            sockaddr_in client; socklen_t client_len = sizeof(client);
            SOCKET client_sock = accept(tcp_sock, reinterpret_cast<sockaddr*>(&client), &client_len);
            if (client_sock >= 0) {
                std::string peer_ip = inet_ntoa(client.sin_addr);
                std::thread([this, client_sock, peer_ip]() { _handle_tcp(client_sock, peer_ip); }).detach();
            } else { std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
        }
    }

    void _share_cleanup_loop() {
        while (running) {
            std::this_thread::sleep_for(std::chrono::seconds(60));
            double now = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
            std::lock_guard<std::mutex> l(shares_mtx);
            for (auto it = shares.begin(); it != shares.end();) {
                if (now - it->second.created > 3600) it = shares.erase(it); else ++it;
            }
        }
    }

    void _entry_loop() {
        while (running) {
            std::this_thread::sleep_for(std::chrono::seconds(300));
            if (running && !offline.load()) send_entry();
        }
    }

    PeerInfo _peer_from_entry(const IPMsgPacket& p, const std::string& ip, int port) {
        auto [nick_body, group_body, ext] = split_entry_payload(p.add);
        auto info = parse_entry_ext(ext);
        std::string u = info.count("UN") ? info.at("UN") : p.sender;
        std::string h = info.count("HN") ? info.at("HN") : p.host;
        std::string n = info.count("NN") ? info.at("NN") : (nick_body.empty() ? u : nick_body);
        std::string g = info.count("GN") ? info.at("GN") : group_body;
        PeerInfo peer(ip, u, h, port);
        peer.nick = n; peer.group = g; peer.status_flags = get_flags(p.cmd);
        peer.offduty = (get_command(p.cmd)==IPMSG_BR_ABSENCE) || (get_flags(p.cmd)&IPMSG_ABSENCEOPT);
        peer.ctype = (get_flags(p.cmd)&IPMSG_PYMSG_FLAG) ? 1 : 0;
        auto macs = cfg.get<std::map<std::string,std::string>>("mac_addresses", {});
        peer.mac = macs.count(ip) ? macs.at(ip) : "";
        peers[ip] = peer;
        if (peer.mac.empty()) send_mac_query(ip, port);
        if (on_peer_add) on_peer_add(peer);
        return peer;
    }

    bool _is_dup_msg(const std::string& ip, int pno) {
        std::string key = ip+":"+std::to_string(pno);
        double now = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
        if (now - dup_clean_at > 30) {
            for (auto it = dup_msgs.begin(); it != dup_msgs.end();) {
                if (now - it->second > DUP_MSG_TTL) it = dup_msgs.erase(it); else ++it;
            }
            dup_clean_at = now;
        }
        auto it = dup_msgs.find(key);
        if (it != dup_msgs.end()) return true;
        dup_msgs[key] = now; return false;
    }

    void _handle(const IPMsgPacket& p, const std::string& ip, int port) {
        if (offline.load()) return;
        if (ip == this->ip) return;
        int cmd = get_command(p.cmd);
        if ((cmd==IPMSG_SENDMSG||cmd==IPMSG_TICKER_MSG||cmd==IPMSG_BIGMSG) && _is_dup_msg(ip, p.pno)) return;

        switch (cmd) {
        case IPMSG_BR_ENTRY: {
            _peer_from_entry(p, ip, port);
            double now = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
            auto it = ans_sent.find(ip);
            if (it==ans_sent.end() || now-it->second > 1.0) { ans_sent[ip]=now; send_ans(ip, port); }
            break;
        }
        case IPMSG_BR_EXIT: {
            ans_sent.erase(ip);
            if (peers.count(ip)) { auto p = peers[ip]; peers.erase(ip); if (on_peer_rem) on_peer_rem(ip); }
            break;
        }
        case IPMSG_ANSENTRY: _peer_from_entry(p, ip, port); break;
        case IPMSG_BR_ABSENCE: _peer_from_entry(p, ip, port); break;
        case IPMSG_MAC_QUERY: send_mac_reply(ip, port); break;
        case IPMSG_MAC_REPLY: {
            std::string m = p.add; std::replace(m.begin(),m.end(),'-',':');
            if (!m.empty() && m.size()==17) {
                auto macs = cfg.get<std::map<std::string,std::string>>("mac_addresses",{});
                macs[ip] = m; cfg.set("mac_addresses", macs);
                if (peers.count(ip)) peers[ip].mac = m;
                flog.info("MAC "+ip+"="+m);
            }
            break;
        }
        case IPMSG_SENDMSG: {
            if (!peers.count(ip)) {
                PeerInfo peer(ip, p.sender, p.host, port); peers[ip]=peer;
                if (on_peer_add) on_peer_add(peer); send_mac_query(ip, port);
            }
            if (get_flags(p.cmd)&IPMSG_SENDCHECKOPT) send_recv(ip, port, p.pno);
            if (get_flags(p.cmd)&IPMSG_FILEATTACHOPT) {
                auto parts = split_str(p.add, '\0');
                std::string msg = parts[0];
                std::vector<FileInfo> fis = FileInfo::parse(parts.size()>1?parts[1]:"", p.pno);
                if (on_recv_msg) on_recv_msg(ip, p.sender, msg);
                if (!fis.empty() && on_recv_files) on_recv_files(ip, p.sender, fis, port, p.pno);
            } else {
                if (!p.add.empty() && on_recv_msg) on_recv_msg(ip, p.sender, p.add);
            }
            break;
        }
        case IPMSG_RECVMSG: flog.info("Msg delivered (#"+p.add+") -> "+ip); break;
        case IPMSG_RELEASEFILES: {
            try {
                int sp = std::stoi(p.add);
                std::lock_guard<std::mutex> l(shares_mtx);
                auto it = shares.find(sp);
                if (it!=shares.end()) { it->second.hosts.erase(ip); if(it->second.hosts.empty()) shares.erase(it); }
            } catch(...) {}
            break;
        }
        case IPMSG_CUSTOM_SHUTDOWN:
            flog.info("Shutdown cmd from "+p.sender+"("+ip+")");
            if (cfg.get<bool>("accept_shutdown",true) && on_shutdown) on_shutdown(p.sender, ip);
            break;
        case IPMSG_PING: send_pong(ip, port, p.add); break;
        case IPMSG_PONG: if (on_pong) on_pong(ip, std::stoi(p.add)); break;
        case IPMSG_FILE_RECV_OK: {
            auto parts = split_str(p.add, ':');
            if (parts.size()>=2) { try { tq->confirm_send(std::stoi(parts[0],nullptr,16), std::stoi(parts[1],nullptr,16)); } catch(...) {} }
            break;
        }
        case IPMSG_TICKER_MSG: {
            auto parts = split_str(p.add, ':'); int orig = p.pno;
            if (!parts.empty()) { try { orig = std::stoi(parts[0],nullptr,16); } catch(...) {} }
            std::string text = parts.size()>1 ? parts[1] : p.add;
            if (on_recv_ticker) on_recv_ticker(ip, p.sender, orig, text, port);
            break;
        }
        case IPMSG_BIGMSG: {
            auto parts = split_str(p.add, ':'); int orig = p.pno;
            if (!parts.empty()) { try { orig = std::stoi(parts[0],nullptr,16); } catch(...) {} }
            std::string text = parts.size()>1 ? parts[1] : p.add;
            if (on_recv_big) on_recv_big(ip, p.sender, orig, text, port);
            break;
        }
        case IPMSG_MSG_ACK: {
            int orig = 0; try { orig = std::stoi(p.add); } catch(...) {}
            if (orig && on_msg_ack) on_msg_ack(ip, orig);
            break;
        }
        case IPMSG_MSG_DONE: {
            int orig = 0; try { orig = std::stoi(p.add); } catch(...) {}
            if (orig && on_msg_done) on_msg_done(ip, orig);
            break;
        }
        case IPMSG_TASK_VOID: {
            auto [pno, reason] = _split_pno_reason(p.add);
            if (pno && on_task_void) on_task_void(ip, pno, reason);
            break;
        }
        case IPMSG_TASK_RECALL: {
            auto [pno, reason] = _split_pno_reason(p.add);
            if (pno && on_task_recall) on_task_recall(ip, pno, reason, port);
            break;
        }
        case IPMSG_TASK_RECALL_REQ: {
            auto [pno, reason] = _split_pno_reason(p.add);
            if (pno && on_task_recall_req) on_task_recall_req(ip, pno, reason, port);
            break;
        }
        case IPMSG_TASK_RECALL_ANS: {
            std::string raw = p.add; bool agree=true; std::string reason; int pno=0;
            auto parts = split_str(raw, ':');
            if (!parts.empty()) { try { pno=std::stoi(parts[0],nullptr,16); } catch(...) {} }
            if (parts.size()>1) { agree=parts[1]!="0"; if(parts.size()>2) reason=parts[2]; }
            if (pno && on_task_recall_ans) on_task_recall_ans(ip, pno, agree, reason);
            break;
        }
        }
    }

    void _handle_tcp(SOCKET s, const std::string& peer_ip) {
        char buf[65535];
        int n = recv(s, buf, sizeof(buf), 0);
        if (n > 0) {
            std::vector<char> data(buf, buf+n);
            IPMsgPacket pkt = IPMsgPacket::decode(data);
            int cmd = get_command(pkt.cmd);
            if (cmd==IPMSG_GETFILEDATA || cmd==IPMSG_GETDIRFILES) {
                auto parts = split_str(pkt.add, ':');
                if (parts.size()>=2) {
                    try {
                        int sp = std::stoi(parts[0],nullptr,16);
                        int fid = std::stoi(parts[1],nullptr,16);
                        int offset = parts.size()>2 ? std::stoi(parts[2],nullptr,16) : 0;
                        std::lock_guard<std::mutex> l(shares_mtx);
                        auto it = shares.find(sp);
                        if (it!=shares.end() && it->second.hosts.count(peer_ip)) {
                            auto eit = it->second.entries.find(fid);
                            if (eit!=it->second.entries.end() && !eit->second.is_dir) {
                                std::ifstream f(eit->second.path, std::ios::binary);
                                if (f.is_open()) {
                                    if (offset>0) f.seekg(offset);
                                    char b[1024*1024];
                                    while (f.good()) { f.read(b,sizeof(b)); std::streamsize br=f.gcount(); if(br>0) send(s,b,(int)br,0); }
                                }
                            }
                        }
                    } catch(...) {}
                }
            }
        }
        closesocket(s);
    }
};

