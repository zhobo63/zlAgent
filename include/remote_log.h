#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#pragma pack(push, 1)
struct LogPacket {
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t a;
};
#pragma pack(pop)

class RemoteLog
{
public:
    explicit RemoteLog(int port = 995);
    ~RemoteLog();

    struct Color
    {
        uint8_t r;
        uint8_t g;
        uint8_t b;
        uint8_t a;

        static Color RED;
        static Color GREEN;
        static Color YELLOW;
        static Color BLUE;
        static Color CYAN;
        static Color MAGENTA;
        static Color ORANGE;
        static Color GRAY;
        static Color WHITE;
    };

    /*
     * Send UDP to the target machine (SetTarget, default 127.0.0.1) port 995
     * Binary format: [4-byte LogPacket header][msg + '\0']
     */
    void Log(const Color &color, const char *msg, ...);

    // Set the receiver machine's IPv4 address (default 127.0.0.1).
    void SetTarget(const std::string &ip);

    static RemoteLog& GetInstance() {
        std::call_once(initFlag_, []{
            gInstance = std::make_unique<RemoteLog>();
        });
        return *gInstance;
    }
    static void FreeInstance() {
        gInstance.reset();
    }

protected:
    void EnqueuePacket(std::string packet);

private:
    int _socket = -1;
    int _port = 995;
    std::string _target = "127.0.0.1";  // receiver machine's IPv4 address
    std::deque<std::string> _buffer;  // O(1) pop_front vs vector's O(n) erase(begin())
    std::mutex              _mtx;
    std::condition_variable _cv;
    std::atomic<bool>       _running{false};  // thread-safe flag, no data race
    std::thread             _senderThread;

    static constexpr size_t MAX_BUFFER_SIZE = 1024;
    static constexpr int    UDP_PORT        = 995;
    static constexpr size_t INIT_FORMAT_BUF = 4096; // initial format buffer size

    static std::unique_ptr<RemoteLog> gInstance;
    static std::once_flag initFlag_;

    void InitSocket();
    void CloseSocket();
    void SendWorker();
};

inline std::unique_ptr<RemoteLog> RemoteLog::gInstance;
inline std::once_flag             RemoteLog::initFlag_;

#define LOG RemoteLog::GetInstance().Log

class Inspector : public RemoteLog {
public:
    Inspector() : RemoteLog(996) {}

    void Log(const RemoteLog::Color &color, const char *key, const char *msg, ...);

    static Inspector &GetInstance() {
        std::call_once(init_flag_, [] {
            instance_ = std::make_unique<Inspector>();
        });
        return *instance_;
    }

    static void FreeInstance() {
        instance_.reset();
    }

private:
    inline static std::unique_ptr<Inspector> instance_;
    inline static std::once_flag init_flag_;
};

#define INSPECTOR Inspector::GetInstance().Log

#ifdef REMOTE_LOG_IMPLEMENT

#if defined(_WIN32) || defined(_WIN64)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <winsock2.h>
#include <windows.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#endif

#include <cstdarg>
#include <chrono>

// ── Color definitions ────────────────────────────────────────────────

RemoteLog::Color RemoteLog::Color::RED = { 255,   0,   0,   255 };
RemoteLog::Color RemoteLog::Color::GREEN = { 0, 255,   0,   255 };
RemoteLog::Color RemoteLog::Color::YELLOW = { 255, 255,   0, 255 };
RemoteLog::Color RemoteLog::Color::BLUE = { 0,   0, 255, 255 };
RemoteLog::Color RemoteLog::Color::CYAN = { 0, 255, 255, 255 };
RemoteLog::Color RemoteLog::Color::MAGENTA = { 255,   0, 255, 255 };
RemoteLog::Color RemoteLog::Color::ORANGE = { 255, 165,   0,   255 };
RemoteLog::Color RemoteLog::Color::GRAY = { 128, 128, 128, 255 };
RemoteLog::Color RemoteLog::Color::WHITE = { 255, 255, 255, 255 }; 

// ── Windows socket init/cleanup (Meyers' Singleton — thread-safe) ────

#if defined(_WIN32) || defined(_WIN64)
static void InitWinsock() {
    // C++11 guarantees: function-local static is initialized exactly once,
    // even under concurrent access. No need for a separate bool flag.
    static bool inited = [] {
        WSADATA wsaData;
        WSAStartup(MAKEWORD(2, 2), &wsaData);
        return true;
        }();
    (void)inited;
}

static void CleanupWinsock() {
    // Note: WSACleanup is safe to call even if Winsock was not initialized.
    // If multiple RemoteLog instances exist, the last one to be destroyed
    // will clean up. This is acceptable for a logging utility.
    WSACleanup();
}
#endif

// ── RemoteLog implementation ─────────────────────────────────────────

RemoteLog::RemoteLog(int port) : _port(port) {
#if defined(_WIN32) || defined(_WIN64)
    InitWinsock();
#endif
    _running.store(true, std::memory_order_release);
    _senderThread = std::thread(&RemoteLog::SendWorker, this);
}

RemoteLog::~RemoteLog() {
    // Signal the worker to stop. The atomic store ensures SendWorker sees it.
    _running.store(false, std::memory_order_release);
    _cv.notify_all();

    if (_senderThread.joinable()) {
        _senderThread.join();
    }

    CloseSocket();

#if defined(_WIN32) || defined(_WIN64)
    CleanupWinsock();
#endif
}

void RemoteLog::InitSocket() {
    // Double-check pattern: first check without lock (fast path), then with lock.
    if (_socket >= 0) return;

#if defined(_WIN32) || defined(_WIN64)
    _socket = static_cast<int>(WSASocket(AF_INET, SOCK_DGRAM, IPPROTO_UDP, nullptr, 0, 0));
#else
    _socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
#endif

    if (_socket < 0) {
        return; // failed silently — same as before for backward compat
    }

    // Enable broadcast
    int optval = 1;
    if (setsockopt(_socket, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char*>(&optval), sizeof(optval)) != 0) {
        CloseSocket();
        return;
    }

    // Set non-blocking
#if defined(_WIN32) || defined(_WIN64)
    u_long mode = 1;
    if (ioctlsocket(_socket, FIONBIO, &mode) != 0) {
        CloseSocket();
        return;
    }
#else
    int flags = fcntl(_socket, F_GETFL, 0);
    if (flags < 0 || fcntl(_socket, F_SETFL, flags | O_NONBLOCK) < 0) {
        CloseSocket();
        return;
    }
#endif
}

void RemoteLog::CloseSocket() {
    if (_socket >= 0) {
#if defined(_WIN32) || defined(_WIN64)
        closesocket(_socket);
#else
        close(_socket);
#endif
        _socket = -1;
    }
}

void RemoteLog::SendWorker() {
    // Pre-allocate the address structure outside the loop.
    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(_port));

    std::deque<std::string> localBuffer;
    while (_running.load(std::memory_order_acquire)) {

        std::string target;
        {
            std::unique_lock<std::mutex> lock(_mtx);
            _cv.wait_for(lock, std::chrono::milliseconds(100), [this]() {
                return !_buffer.empty() || !_running.load(std::memory_order_acquire);
                });

            if (_buffer.empty()) continue;
            localBuffer.swap(_buffer);
            target = _target;
        }

        if (inet_pton(AF_INET, target.c_str(), &addr.sin_addr) != 1) {
            localBuffer.clear();
            continue;
        }

        // Init socket on first send attempt (lazy init)
        if (_socket < 0) {
            InitSocket();
        }

        for (const auto& data : localBuffer) {
            if (_socket < 0) break;

#if defined(_WIN32) || defined(_WIN64)
            sendto(static_cast<SOCKET>(_socket), data.data(), static_cast<int>(data.size()), 0,
                reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));
#else
            sendto(_socket, data.data(), data.size(), 0,
                reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));
#endif
        }
        localBuffer.clear();
    }

    // Drain remaining messages after _running is false.
    {
        std::unique_lock<std::mutex> lock(_mtx);
        if (!_buffer.empty()) {
            localBuffer.swap(_buffer);
        }
        if (inet_pton(AF_INET, _target.c_str(), &addr.sin_addr) != 1) {
            localBuffer.clear();
        }
    }

    for (const auto& data : localBuffer) {
        if (_socket < 0) break;

#if defined(_WIN32) || defined(_WIN64)
        sendto(static_cast<SOCKET>(_socket), data.data(), static_cast<int>(data.size()), 0,
            reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));
#else
        sendto(_socket, data.data(), data.size(), 0,
            reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));
#endif
    }
}

void RemoteLog::Log(const Color& color, const char* msg, ...) {
    va_list args;
    va_start(args, msg);

    // ── Two-pass formatting: first get length, then format directly into packet ──

    int len = vsnprintf(nullptr, 0, msg, args);
    va_end(args);

    if (len < 0) {
        return; // encoding error
    }

    std::string packet;
    packet.reserve(sizeof(LogPacket) + static_cast<size_t>(len) + 1);

    // Write header
    LogPacket hdr = {};
    hdr.r = color.r;
    hdr.g = color.g;
    hdr.b = color.b;
    hdr.a = color.a;
    packet.append(reinterpret_cast<char*>(&hdr), sizeof(hdr));

    // Format message directly into the packet (no intermediate string)
    size_t msg_start = packet.size();
    packet.resize(msg_start + static_cast<size_t>(len) + 1);
    va_start(args, msg);
    vsnprintf(&packet[msg_start], static_cast<size_t>(len) + 1, msg, args);
    va_end(args);

    EnqueuePacket(std::move(packet));
}

void RemoteLog::SetTarget(const std::string &ip) {
    std::lock_guard<std::mutex> lock(_mtx);
    _target = ip;
}

void RemoteLog::EnqueuePacket(std::string packet) {
    {
        std::lock_guard<std::mutex> lock(_mtx);
        if (_buffer.size() >= MAX_BUFFER_SIZE) {
            _buffer.pop_front();
        }
        _buffer.push_back(std::move(packet));
    }
    _cv.notify_one();
}

void Inspector::Log(const RemoteLog::Color &color, const char *key, const char *msg, ...) {
    if (key == nullptr || msg == nullptr) {
        return;
    }

    va_list args;
    va_start(args, msg);
    const int length = vsnprintf(nullptr, 0, msg, args);
    va_end(args);
    if (length < 0) {
        return;
    }

    LogPacket header = {color.r, color.g, color.b, color.a};
    std::string packet(reinterpret_cast<const char *>(&header), sizeof(header));
    packet.append(key);
    packet.push_back('\0');

    const size_t message_offset = packet.size();
    packet.resize(message_offset + static_cast<size_t>(length) + 1);
    va_start(args, msg);
    vsnprintf(&packet[message_offset], static_cast<size_t>(length) + 1, msg, args);
    va_end(args);

    EnqueuePacket(std::move(packet));
}

#endif

#ifdef REMOTE_LOG_SERVER_IMPLEMENT

#if defined(_WIN32) || defined(_WIN64)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

struct RemoteLogServer {
    struct Log {
        std::string time;
        std::string ip;
        RemoteLog::Color color;
        std::string text;
    };

    struct Inspector {
        std::string ip;
        RemoteLog::Color color;
        std::string key;
        std::string text;
    };

    int port = 995;
    int inspector_port = 996;

    using on_log = std::function<void(const Log& log)>;
    using on_inspector = std::function<void(const Inspector& inspector)>;

    explicit RemoteLogServer(on_log callback, on_inspector inspector_callback = {})
        : callback_(std::move(callback)), inspector_callback_(std::move(inspector_callback)) {
#if defined(_WIN32) || defined(_WIN64)
        WSADATA wsa_data;
        if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
            return;
        }
        winsock_initialized_ = true;
#endif

        socket_ = bind_socket(port);
        inspector_socket_ = bind_socket(inspector_port);
        if (socket_ == invalid_socket() || inspector_socket_ == invalid_socket()) {
            close_socket(socket_);
            close_socket(inspector_socket_);
            return;
        }

        running_ = true;
        receiver_ = std::thread(&RemoteLogServer::receive, this, socket_);
        inspector_receiver_ = std::thread(&RemoteLogServer::receive_inspector, this, inspector_socket_);
    }

    ~RemoteLogServer() {
        running_ = false;
        close_socket(socket_);
        close_socket(inspector_socket_);
        if (receiver_.joinable()) {
            receiver_.join();
        }
        if (inspector_receiver_.joinable()) {
            inspector_receiver_.join();
        }
#if defined(_WIN32) || defined(_WIN64)
        if (winsock_initialized_) {
            WSACleanup();
        }
#endif
    }

    RemoteLogServer(const RemoteLogServer&) = delete;
    RemoteLogServer& operator=(const RemoteLogServer&) = delete;

private:
#if defined(_WIN32) || defined(_WIN64)
    using socket_type = SOCKET;
    static constexpr socket_type invalid_socket() { return INVALID_SOCKET; }
#else
    using socket_type = int;
    static constexpr socket_type invalid_socket() { return -1; }
#endif

    socket_type socket_ = invalid_socket();
    socket_type inspector_socket_ = invalid_socket();
    std::atomic<bool> running_ = false;
    std::thread receiver_;
    std::thread inspector_receiver_;
    on_log callback_;
    on_inspector inspector_callback_;
#if defined(_WIN32) || defined(_WIN64)
    bool winsock_initialized_ = false;
#endif

    socket_type bind_socket(int port) {
        socket_type socket = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (socket == invalid_socket()) {
            return invalid_socket();
        }

        int reuse_address = 1;
        setsockopt(socket, SOL_SOCKET, SO_REUSEADDR,
            reinterpret_cast<const char*>(&reuse_address), sizeof(reuse_address));

        sockaddr_in address = {};
        address.sin_family = AF_INET;
        address.sin_port = htons(static_cast<uint16_t>(port));
        address.sin_addr.s_addr = htonl(INADDR_ANY);
        if (bind(socket, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
            close_socket(socket);
            return invalid_socket();
        }
        return socket;
    }

    void close_socket(socket_type &socket) {
        if (socket == invalid_socket()) {
            return;
        }
#if defined(_WIN32) || defined(_WIN64)
        shutdown(socket, SD_BOTH);
        closesocket(socket);
#else
        shutdown(socket, SHUT_RDWR);
        close(socket);
#endif
        socket = invalid_socket();
    }

    static std::string timestamp() {
        const auto now = std::chrono::system_clock::now();
        const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()) % 1000;
        const std::time_t time = std::chrono::system_clock::to_time_t(now);
        std::tm local_time = {};
#if defined(_WIN32) || defined(_WIN64)
        localtime_s(&local_time, &time);
#else
        localtime_r(&time, &local_time);
#endif
        char value[24] = {};
        std::snprintf(value, sizeof(value), "%04d-%02d-%02d %02d:%02d:%02d.%03d",
            local_time.tm_year + 1900, local_time.tm_mon + 1, local_time.tm_mday,
            local_time.tm_hour, local_time.tm_min, local_time.tm_sec,
            static_cast<int>(milliseconds.count()));
        return value;
    }

    void receive(socket_type socket) {
        std::array<char, 65536> buffer;
        while (running_) {
            sockaddr_in sender = {};
#if defined(_WIN32) || defined(_WIN64)
            int sender_size = sizeof(sender);
            const int bytes_received = recvfrom(socket, buffer.data(), static_cast<int>(buffer.size()), 0,
                reinterpret_cast<sockaddr*>(&sender), &sender_size);
#else
            socklen_t sender_size = sizeof(sender);
            const ssize_t bytes_received = recvfrom(socket, buffer.data(), buffer.size(), 0,
                reinterpret_cast<sockaddr*>(&sender), &sender_size);
#endif
            if (bytes_received < 0) {
                if (!running_) {
                    break;
                }
                continue;
            }

            constexpr size_t header_size = 4;
            const size_t packet_size = static_cast<size_t>(bytes_received);
            if (packet_size <= header_size) {
                continue;
            }

            const char* message = buffer.data() + header_size;
            const size_t message_size = packet_size - header_size;
            const void* terminator = std::memchr(message, '\0', message_size);
            if (terminator == nullptr) {
                continue;
            }

            char ip[INET_ADDRSTRLEN] = {};
            if (inet_ntop(AF_INET, &sender.sin_addr, ip, sizeof(ip)) == nullptr) {
                continue;
            }

            const auto* color = reinterpret_cast<const uint8_t*>(buffer.data());
            Log log{
                timestamp(),
                ip,
                {color[0], color[1], color[2], color[3]},
                std::string(message, static_cast<const char*>(terminator)),
            };
            if (callback_) {
                callback_(log);
            }
            memset(buffer.data(), 0, 8);
        }
    }

    void receive_inspector(socket_type socket) {
        std::array<char, 65536> buffer;
        while (running_) {
            sockaddr_in sender = {};
#if defined(_WIN32) || defined(_WIN64)
            int sender_size = sizeof(sender);
            const int bytes_received = recvfrom(socket, buffer.data(), static_cast<int>(buffer.size()), 0,
                reinterpret_cast<sockaddr*>(&sender), &sender_size);
#else
            socklen_t sender_size = sizeof(sender);
            const ssize_t bytes_received = recvfrom(socket, buffer.data(), buffer.size(), 0,
                reinterpret_cast<sockaddr*>(&sender), &sender_size);
#endif
            if (bytes_received < 0) {
                if (!running_) {
                    break;
                }
                continue;
            }

            constexpr size_t header_size = 4;
            const size_t packet_size = static_cast<size_t>(bytes_received);
            if (packet_size <= header_size + 1) {
                continue;
            }

            const char* key = buffer.data() + header_size;
            const size_t content_size = packet_size - header_size;
            const auto* key_terminator = static_cast<const char*>(std::memchr(key, '\0', content_size));
            if (key_terminator == nullptr) {
                continue;
            }

            const char* message = key_terminator + 1;
            const size_t message_size = packet_size - header_size -
                static_cast<size_t>(message - key);
            const auto* message_terminator = static_cast<const char*>(std::memchr(message, '\0', message_size));
            if (message_terminator == nullptr) {
                continue;
            }

            char ip[INET_ADDRSTRLEN] = {};
            if (inet_ntop(AF_INET, &sender.sin_addr, ip, sizeof(ip)) == nullptr) {
                continue;
            }

            const auto* color = reinterpret_cast<const uint8_t*>(buffer.data());
            Inspector inspector{
                ip,
                {color[0], color[1], color[2], color[3]},
                std::string(key, key_terminator),
                std::string(message, message_terminator),
            };
            if (inspector_callback_) {
                inspector_callback_(inspector);
            }
            memset(buffer.data(), 0, 8);
        }
    }
};

#endif
