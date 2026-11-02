// PyMsg Pro - C++ 实现 - 主程序入口与控制台UI
// 编译命令: g++ -std=c++17 -pthread -o pymsg pymsg_main.cpp
// 依赖: nlohmann/json 头文件库

#include "pymsg.h"
#include "pymsg_engine.cpp"  // 包含引擎实现
#include <iostream>
#include <sstream>
#include <thread>
#include <chrono>
#include <csignal>
#include <iomanip>

// ============================================================
// 全局变量
// ============================================================
static std::unique_ptr<Engine> g_engine;
static std::unique_ptr<TransferQueue> g_tq;
static std::unique_ptr<Config> g_cfg;
static std::unique_ptr<FileLogger> g_flog;
static std::unique_ptr<GlobalChatHistory> g_hist;
static std::atomic<bool> g_running{true};

// ============================================================
// 信号处理
// ============================================================
static void signal_handler(int sig) {
    g_running = false;
    if (g_engine) g_engine->stop();
    exit(0);
}

// ============================================================
// 控制台UI
// ============================================================
void print_banner() {
    std::cout << "============================================" << std::endl;
    std::cout << "  PyMsg Pro - C++ Version (飞鸽传书增强版)" << std::endl;
    std::cout << "============================================" << std::endl;
    std::cout << std::endl;
}

void print_help() {
    std::cout << std::endl;
    std::cout << "命令列表:" << std::endl;
    std::cout << "  /help          - 显示此帮助" << std::endl;
    std::cout << "  /peers         - 显示在线对端列表" << std::endl;
    std::cout << "  /msg <ip> <msg>- 发送消息给指定IP" << std::endl;
    std::cout << "  /bcast <msg>   - 广播消息给所有对端" << std::endl;
    std::cout << "  /ticker <msg>  - 发送提醒消息(滚动条)" << std::endl;
    std::cout << "  /big <msg>     - 发送大屏消息(强制弹窗)" << std::endl;
    std::cout << "  /file <ip> <path> - 发送文件给指定IP" << std::endl;
    std::cout << "  /shutdown <ip> - 远程关机指令" << std::endl;
    std::cout << "  /wol <mac>     - 网络唤醒(WOL)" << std::endl;
    std::cout << "  /ping <ip>     -  ping对端" << std::endl;
    std::cout << "  /history       - 显示聊天历史" << std::endl;
    std::cout << "  /clear         - 清除聊天历史" << std::endl;
    std::cout << "  /archive [month] - 查询归档日志" << std::endl;
    std::cout << "  /transfer      - 显示传输列表" << std::endl;
    std::cout << "  /settings      - 显示设置" << std::endl;
    std::cout << "  /nick <name>   - 设置昵称" << std::endl;
    std::cout << "  /group <name>  - 设置分组" << std::endl;
    std::cout << "  /offline       - 标记下班" << std::endl;
    std::cout << "  /online        - 恢复在岗" << std::endl;
    std::cout << "  /exit          - 退出程序" << std::endl;
    std::cout << std::endl;
}

void print_peers() {
    if (!g_engine) return;
    std::cout << std::endl << "=== 在线对端 ===" << std::endl;
    int idx = 0;
    for (auto& [ip, peer] : g_engine->peers) {
        std::string status = peer.is_local ? "[本机]" : (peer.offduty ? "[下班]" : "[在线]");
        std::string name = peer.shown_name();
        if (name.empty()) name = peer.user;
        std::cout << "  " << idx++ << ". " << status << " " << name
                  << " (" << ip << ":" << peer.port << ")"
                  << (peer.mac.empty() ? "" : " MAC:" + peer.mac)
                  << std::endl;
    }
    std::cout << "  共 " << g_engine->peers.size() << " 台 (在岗: " << g_engine->on_duty_peer_count() << ")" << std::endl;
    std::cout << std::endl;
}

void print_history() {
    if (!g_hist) return;
    auto msgs = g_hist->get_all();
    std::cout << std::endl << "=== 聊天历史 (最近" << msgs.size() << "条) ===" << std::endl;
    int count = 0;
    for (auto it = msgs.rbegin(); it != msgs.rend() && count < 50; ++it) {
        std::string time = it->value("time", "");
        std::string kind = it->value("kind", "");
        std::string sender = it->value("sender", "");
        std::string ip = it->value("ip", "");
        std::string text = it->value("text", "");
        std::string dir = it->value("dir", "");
        std::cout << "  [" << time << "] " << dir << " " << sender << "(" << ip << "): " << text << std::endl;
        count++;
    }
    std::cout << std::endl;
}

void print_transfers() {
    if (!g_tq) return;
    auto tasks = g_tq->get_all();
    std::cout << std::endl << "=== 传输任务 ===" << std::endl;
    for (auto& t : tasks) {
        std::string status;
        switch(t.status) {
            case TransferTask::IDLE: status = "空闲"; break;
            case TransferTask::RUNNING: status = "传输中"; break;
            case TransferTask::DONE: status = "完成"; break;
            case TransferTask::FAILED: status = "失败"; break;
            case TransferTask::CANCELLED: status = "取消"; break;
        }
        std::cout << "  " << t.tid << " " << status << " " << t.ip << " " << t.fi.name
                  << " " << format_size(t.fi.size) << " " << t.progress_text() << std::endl;
    }
    std::cout << std::endl;
}

void print_settings() {
    if (!g_cfg) return;
    std::cout << std::endl << "=== 设置 ===" << std::endl;
    std::cout << "  用户名: " << g_cfg->get<std::string>("username", "") << std::endl;
    std::cout << "  主机名: " << g_cfg->get<std::string>("hostname", "") << std::endl;
    std::cout << "  昵称: " << g_cfg->get<std::string>("nickname", "") << std::endl;
    std::cout << "  分组: " << g_cfg->get<std::string>("group", "") << std::endl;
    std::cout << "  端口: " << g_cfg->get<int>("port", 0) << std::endl;
    std::cout << "  接受关机: " << (g_cfg->get<bool>("accept_shutdown", true) ? "是" : "否") << std::endl;
    std::cout << std::endl;
}

// ============================================================
// 消息回调
// ============================================================
void setup_callbacks() {
    if (!g_engine) return;

    g_engine->on_peer_add = [](const PeerInfo& peer) {
        if (g_flog) g_flog->info("Peer online: " + peer.shown_name() + " (" + peer.ip + ")");
        std::cout << "\\033[32m[上线] " << peer.shown_name() << " (" << peer.ip << ")\\033[0m" << std::endl;
    };

    g_engine->on_peer_rem = [](const std::string& ip) {
        if (g_flog) g_flog->info("Peer offline: " + ip);
        std::cout << "\\033[31m[下线] " << ip << "\\033[0m" << std::endl;
    };

    g_engine->on_recv_msg = [](const std::string& ip, const std::string& sender, const std::string& msg) {
        json rec;
        rec["time"] = get_timestamp();
        rec["kind"] = "msg";
        rec["dir"] = "recv";
        rec["sender"] = sender;
        rec["ip"] = ip;
        rec["text"] = msg;
        if (g_hist) g_hist->add(rec);
        std::cout << "\\033[36m[" << get_time_str() << "] " << sender << "(" << ip << "): " << msg << "\\033[0m" << std::endl;
    };

    g_engine->on_recv_files = [](const std::string& ip, const std::string& sender,
                                  const std::vector<FileInfo>& fis, int port, int share_pno) {
        if (g_flog) g_flog->info("Files received from " + ip + ": " + std::to_string(fis.size()) + " files");
        json rec;
        rec["time"] = get_timestamp();
        rec["kind"] = "file";
        rec["dir"] = "recv";
        rec["sender"] = sender;
        rec["ip"] = ip;
        rec["text"] = "收到文件: " + std::string(fis.size()==1?fis[0].name.c_str():fis[0].name.c_str());
        if (g_hist) g_hist->add(rec);
        std::cout << "\\033[33m[" << get_time_str() << "] 收到文件 from " << sender << "(" << ip << ")\\033[0m" << std::endl;
        // 自动回复文件接收确认
        if (g_engine) g_engine->send_recv(ip, port, share_pno);
    };

    g_engine->on_recv_ticker = [](const std::string& ip, const std::string& sender, int pno, const std::string& msg, int port) {
        if (g_flog) g_flog->info("Ticker from " + ip + ": " + msg);
        std::cout << "\\033[35m[提醒] " << sender << "(" << ip << "): " << msg << "\\033[0m" << std::endl;
    };

    g_engine->on_recv_big = [](const std::string& ip, const std::string& sender, int pno, const std::string& msg, int port) {
        if (g_flog) g_flog->info("Big message from " + ip + ": " + msg);
        std::cout << "\\033[31m[大屏] " << sender << "(" << ip << "): " << msg << "\\033[0m" << std::endl;
        // 自动确认
        if (g_engine) g_engine->send_msg_ack(ip, port, pno);
    };

    g_engine->on_msg_ack = [](const std::string& ip, int pno) {
        if (g_flog) g_flog->info("ACK received from " + ip + " for pno=" + std::to_string(pno));
    };

    g_engine->on_msg_done = [](const std::string& ip, int pno) {
        if (g_flog) g_flog->info("DONE received from " + ip + " for pno=" + std::to_string(pno));
    };

    g_engine->on_task_void = [](const std::string& ip, int pno, const std::string& reason) {
        if (g_flog) g_flog->info("Task voided from " + ip + ": " + reason);
        std::cout << "\\033[33m[作废] " << ip << " 作废任务 pno=" << pno << " 原因: " << reason << "\\033[0m" << std::endl;
    };

    g_engine->on_task_recall = [](const std::string& ip, int pno, const std::string& reason, int port) {
        if (g_flog) g_flog->info("Task recall from " + ip + ": " + reason);
        std::cout << "\\033[33m[撤回] " << ip << " 撤回任务 pno=" << pno << " 原因: " << reason << "\\033[0m" << std::endl;
    };

    g_engine->on_task_recall_req = [](const std::string& ip, int pno, const std::string& reason, int port) {
        if (g_flog) g_flog->info("Task recall req from " + ip + ": " + reason);
        // 自动同意撤回
        if (g_engine) g_engine->send_task_recall_ans(ip, port, pno, true, "同意");
    };

    g_engine->on_task_recall_ans = [](const std::string& ip, int pno, bool agree, const std::string& reason) {
        if (g_flog) g_flog->info("Task recall ans from " + ip + ": " + (agree?"agree":"reject") + " " + reason);
    };

    g_engine->on_shutdown = [](const std::string& sender, const std::string& ip) {
        if (g_flog) g_flog->warning("Shutdown command from " + sender + "(" + ip + ")");
        std::cout << "\\033[31m[关机指令] 收到来自 " << sender << "(" << ip << ") 的关机指令!\\033[0m" << std::endl;
        // 这里可以弹出确认窗口，控制台版本直接确认
        std::cout << "  输入 /shutcancel 取消关机，或等待自动关机" << std::endl;
    };

    g_engine->on_pong = [](const std::string& ip, int latency_ms) {
        std::cout << "\\033[32m[PONG] " << ip << " latency=" << latency_ms << "ms\\033[0m" << std::endl;
    };

    g_engine->on_port_busy = [](int port) {
        std::cout << "\\033[31m[错误] 端口 " << port << " 已被占用!\\033[0m" << std::endl;
        g_running = false;
    };
}

// ============================================================
// 命令处理
// ============================================================
void process_command(const std::string& cmd_line) {
    std::istringstream iss(cmd_line);
    std::string cmd; iss >> cmd;

    if (cmd == "/help" || cmd == "help") {
        print_help();
    } else if (cmd == "/peers") {
        print_peers();
    } else if (cmd == "/history") {
        print_history();
    } else if (cmd == "/transfer") {
        print_transfers();
    } else if (cmd == "/settings") {
        print_settings();
    } else if (cmd == "/clear") {
        if (g_hist) g_hist->clear();
        std::cout << "聊天历史已清除" << std::endl;
    } else if (cmd == "/exit" || cmd == "quit" || cmd == "q") {
        g_running = false;
    } else if (cmd == "/offline") {
        if (g_engine) g_engine->go_offline();
    } else if (cmd == "/online") {
        if (g_engine) g_engine->go_online();
    } else if (cmd == "/nick") {
        std::string nick; std::getline(iss, nick);
        if (!nick.empty()) {
            if (g_cfg) g_cfg->set("nickname", nick);
            if (g_engine) {
                g_engine->nickname = nick;
                g_engine->nick = nick;
                g_engine->send_entry();
            }
            std::cout << "昵称已设置为: " << nick << std::endl;
        }
    } else if (cmd == "/group") {
        std::string grp; std::getline(iss, grp);
        if (!grp.empty()) {
            if (g_cfg) g_cfg->set("group", grp);
            if (g_engine) g_engine->group = grp;
            std::cout << "分组已设置为: " << grp << std::endl;
        }
    } else if (cmd == "/bcast") {
        std::string msg; std::getline(iss, msg);
        if (!msg.empty() && g_engine) {
            g_engine->broadcast_msg(msg);
            json rec; rec["time"]=get_timestamp(); rec["kind"]="msg"; rec["dir"]="send";
            rec["sender"]=g_engine->user; rec["ip"]=g_engine->ip; rec["text"]=msg;
            if (g_hist) g_hist->add(rec);
            std::cout << "广播已发送: " << msg << std::endl;
        }
    } else if (cmd == "/ticker") {
        std::string msg; std::getline(iss, msg);
        if (!msg.empty() && g_engine) {
            int pno = g_engine->broadcast_ticker_msg(msg);
            json rec; rec["time"]=get_timestamp(); rec["kind"]="ticker"; rec["dir"]="send";
            rec["sender"]=g_engine->user; rec["ip"]=g_engine->ip; rec["text"]=msg;
            if (g_hist) g_hist->add(rec);
            std::cout << "提醒消息已发送 (pno=" << pno << "): " << msg << std::endl;
        }
    } else if (cmd == "/big") {
        std::string msg; std::getline(iss, msg);
        if (!msg.empty() && g_engine) {
            // 大屏消息需要指定单台
            std::string ip; iss >> ip;
            if (!ip.empty()) {
                // 获取端口
                int port = 2425;
                auto it = g_engine->peers.find(ip);
                if (it != g_engine->peers.end()) port = it->second.port;
                int pno = g_engine->send_big_msg(ip, port, msg);
                json rec; rec["time"]=get_timestamp(); rec["kind"]="big"; rec["dir"]="send";
                rec["sender"]=g_engine->user; rec["ip"]=g_engine->ip; rec["text"]=msg;
                if (g_hist) g_hist->add(rec);
                std::cout << "大屏消息已发送: " << msg << std::endl;
            } else {
                std::cout << "用法: /big <ip> <msg>" << std::endl;
            }
        }
    } else if (cmd == "/msg") {
        std::string ip, msg; iss >> ip; std::getline(iss, msg);
        if (!ip.empty() && !msg.empty() && g_engine) {
            int port = 2425;
            auto it = g_engine->peers.find(ip);
            if (it != g_engine->peers.end()) port = it->second.port;
            int pno = g_engine->send_msg(ip, port, msg);
            json rec; rec["time"]=get_timestamp(); rec["kind"]="msg"; rec["dir"]="send";
            rec["sender"]=g_engine->user; rec["ip"]=g_engine->ip; rec["text"]=msg;
            if (g_hist) g_hist->add(rec);
            std::cout << "消息已发送 (pno=" << pno << "): " << msg << std::endl;
        } else {
            std::cout << "用法: /msg <ip> <msg>" << std::endl;
        }
    } else if (cmd == "/file") {
        std::string ip, path; iss >> ip >> path;
        if (!ip.empty() && !path.empty() && g_engine) {
            int port = 2425;
            auto it = g_engine->peers.find(ip);
            if (it != g_engine->peers.end()) port = it->second.port;
            std::vector<std::string> paths = {path};
            auto fis = g_engine->send_files(ip, port, "文件传输", paths);
            if (!fis.empty()) {
                json rec; rec["time"]=get_timestamp(); rec["kind"]="file"; rec["dir"]="send";
                rec["sender"]=g_engine->user; rec["ip"]=g_engine->ip;
                rec["text"]="发送文件: "+fis[0].name;
                if (g_hist) g_hist->add(rec);
                std::cout << "文件已发送: " << fis[0].name << std::endl;
            } else {
                std::cout << "文件发送失败，请检查路径" << std::endl;
            }
        } else {
            std::cout << "用法: /file <ip> <path>" << std::endl;
        }
    } else if (cmd == "/shutdown") {
        std::string ip; iss >> ip;
        if (!ip.empty() && g_engine) {
            int port = 2425;
            auto it = g_engine->peers.find(ip);
            if (it != g_engine->peers.end()) port = it->second.port;
            g_engine->send_shutdown(ip, port);
            std::cout << "关机指令已发送: " << ip << std::endl;
        } else {
            std::cout << "用法: /shutdown <ip>" << std::endl;
        }
    } else if (cmd == "/wol") {
        std::string mac; iss >> mac;
        if (!mac.empty()) {
            send_wol(mac);
            std::cout << "WOL魔术包已发送到 " << mac << std::endl;
        } else {
            std::cout << "用法: /wol <mac>" << std::endl;
        }
    } else if (cmd == "/ping") {
        std::string ip; iss >> ip;
        if (!ip.empty() && g_engine) {
            int port = 2425;
            auto it = g_engine->peers.find(ip);
            if (it != g_engine->peers.end()) port = it->second.port;
            g_engine->send_ping(ip, port);
            std::cout << "PING 已发送到: " << ip << std::endl;
        } else {
            std::cout << "用法: /ping <ip>" << std::endl;
        }
    } else if (cmd == "/archive") {
        std::string month; iss >> month;
        auto results = archive_query(month);
        std::cout << std::endl << "=== 归档日志 (" << results.size() << "条) ===" << std::endl;
        for (auto& r : results) {
            std::cout << "  " << r.value("time","") << " " << r.value("kind","") << " "
                      << r.value("sender","") << "(" << r.value("ip","") << "): "
                      << r.value("text","") << std::endl;
        }
        std::cout << std::endl;
    } else if (cmd == "") {
        // 空命令，忽略
    } else {
        // 当作普通消息广播
        if (g_engine) {
            g_engine->broadcast_msg(cmd_line);
            json rec; rec["time"]=get_timestamp(); rec["kind"]="msg"; rec["dir"]="send";
            rec["sender"]=g_engine->user; rec["ip"]=g_engine->ip; rec["text"]=cmd_line;
            if (g_hist) g_hist->add(rec);
        }
    }
}

// ============================================================
// 主函数
// ============================================================
int main(int argc, char* argv[]) {
    ws_init();
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
#ifdef _WIN32
    std::signal(SIGBREAK, signal_handler);
#endif

    print_banner();

    // 初始化组件
    g_cfg = std::make_unique<Config>();
    g_flog = std::make_unique<FileLogger>();
    g_hist = std::make_unique<GlobalChatHistory>();
    g_tq = std::make_unique<TransferQueue>();

    // 创建引擎
    g_engine = std::make_unique<Engine>(*g_cfg, *g_flog);
    g_engine->tq = g_tq.get();

    // 设置回调
    setup_callbacks();

    // 启动引擎
    std::cout << "正在启动网络引擎..." << std::endl;
    if (!g_engine->start()) {
        std::cout << "引擎启动失败!" << std::endl;
        return 1;
    }

    std::cout << "引擎启动成功!" << std::endl;
    std::cout << "用户名: " << g_engine->user << std::endl;
    std::cout << "主机名: " << g_engine->host << std::endl;
    std::cout << "IP地址: " << g_engine->ip << std::endl;
    std::cout << "端口: " << g_engine->port << std::endl;
    std::cout << "MAC: " << g_engine->mac << std::endl;
    std::cout << std::endl;
    print_help();

    // 主循环
    std::string line;
    while (g_running) {
        std::cout << "> ";
        if (!std::getline(std::cin, line)) break;
        process_command(line);
    }

    // 清理
    std::cout << "正在关闭..." << std::endl;
    if (g_engine) g_engine->stop();
    g_flog->info("程序已退出");

    return 0;
}

