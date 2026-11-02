# PyMsg Pro - C++ 版本 (飞鸽传书增强版)

## 项目说明

这是 PyMsg Pro (飞鸽传书增强版) 的 C++ 移植版本。原程序为 Python 实现，约 9147 行代码，
实现了完整的局域网即时通讯功能。

### 功能特性

- **IPMsg 协议兼容**: 完整的 IPMsg 协议实现，包括 BR_ENTRY/BR_EXIT/ANSENTRY/BR_ABSENCE
- **UDP 广播发现**: 自动发现局域网内对端，支持子网广播和有限广播
- **TCP 文件传输**: 支持文件和文件夹传输，含进度显示
- **消息类型**:
  - 普通聊天消息 (IPMSG_SENDMSG)
  - 提醒消息/滚动条 (IPMSG_TICKER_MSG = 0x90)
  - 大屏消息/强制弹窗 (IPMSG_BIGMSG = 0x91)
  - 回执机制 (MSG_ACK = 0x92, MSG_DONE = 0x93)
- **任务管理**: 任务作废(0x94)、撤回(0x95)、撤回请求(0x96)、撤回应答(0x97)
- **MAC 查询/回复**: 自动获取对端 MAC 地址
- **远程关机**: 发送关机指令 (IPMSG_CUSTOM_SHUTDOWN)
- **网络唤醒 (WOL)**: 魔术包唤醒休眠主机
- **Ping/Pong**: 延迟检测
- **去重机制**: 广播去重 (DUP_MSG_TTL=60s)，防止多网卡导致重复接收
- **自环防护**: 防止自己发的包被自己收到
- **留痕归档**: JSONL 格式追加式日志，按月分文件
- **配置管理**: JSON 配置文件持久化
- **多平台**: Linux/macOS/Windows 兼容 (POSIX sockets / Winsock)

### 文件结构

| 文件 | 说明 |
|------|------|
| pymsg.h | 头文件：协议常量、数据结构、工具函数声明、Config/FileLogger/GlobalChatHistory 类 |
| pymsg_engine.cpp | 核心引擎：Engine 类 (UDP/TCP 网络循环、协议处理)、TransferQueue、归档函数 |
| pymsg_main.cpp | 主程序：控制台 UI、命令解析、回调设置、main() 入口 |
| README.md | 本文件 |

### 编译说明

#### 依赖

- C++17 编译器 (g++ 7+, clang++ 5+)
- nlohmann/json 头文件库 (https://github.com/nlohmann/json)
  - 下载 json.hpp 放入同目录，或安装系统包

#### Linux/macOS

```bash
# 安装 nlohmann/json (可选，也可手动下载)
# Ubuntu/Debian:
sudo apt install nlohmann-json3-dev

# 编译
g++ -std=c++17 -pthread -O2 -o pymsg pymsg_main.cpp

# 运行
./pymsg
```

#### Windows (MinGW/MSVC)

```bash
# MinGW
g++ -std=c++17 -pthread -O2 -o pymsg.exe pymsg_main.cpp -lws2_32

# Visual Studio
# 新建控制台项目，添加 pymsg_main.cpp 和 pymsg.h，设置 C++17
```

### 使用说明

程序启动后进入控制台交互模式，支持以下命令：

| 命令 | 说明 |
|------|------|
| /help | 显示帮助 |
| /peers | 显示在线对端列表 |
| /msg <ip> <msg> | 发送消息给指定IP |
| /bcast <msg> | 广播消息给所有对端 |
| /ticker <msg> | 发送提醒消息(屏幕滚动条) |
| /big <msg> | 发送大屏消息(强制弹窗，需指定IP) |
| /file <ip> <path> | 发送文件给指定IP |
| /shutdown <ip> | 远程关机指令 |
| /wol <mac> | 网络唤醒(WOL魔术包) |
| /ping <ip> | Ping对端(检测延迟) |
| /history | 显示聊天历史 |
| /clear | 清除聊天历史 |
| /archive [month] | 查询归档日志 |
| /transfer | 显示传输列表 |
| /settings | 显示设置 |
| /nick <name> | 设置昵称 |
| /group <name> | 设置分组 |
| /offline | 标记下班(静默模式) |
| /online | 恢复在岗 |
| /exit | 退出程序 |

直接输入文本(非命令)会自动广播发送。

### 与 Python 版本的差异

1. **GUI**: Python 版使用 tkinter 构建图形界面；C++ 版当前为控制台版本
   - 如需 GUI，可基于 Qt/wxWidgets 替换控制台 UI 部分
2. **拖放**: Python 版支持文件拖放；C++ 版需集成对应 GUI 框架的拖放 API
3. **系统托盘**: Python 版使用 pystray；C++ 版需使用平台原生 API
4. **通知**: Python 版有完整的系统通知；C++ 版当前仅控制台输出
5. **协议完全兼容**: 网络协议层 100% 兼容，C++ 版可与 Python 版互通

### 扩展建议

1. **添加 GUI**: 使用 Qt5/Qt6 替换控制台 UI，可实现完整的图形界面
2. **添加系统托盘**: 使用 QSystemTrayIcon 或平台原生 API
3. **添加文件拖放**: Qt 的 drag-and-drop API 非常成熟
4. **添加加密**: 集成 OpenSSL 实现端到端加密
5. **添加代理支持**: 支持 SOCKS/HTTP 代理转发

### 许可证

本项目基于原 Python 版本移植，遵循原项目许可证。

---

PyMsg Pro C++ Version - Built with std::thread, std::mutex, and POSIX/Winsock sockets.
