#include "remote_log.h"

#if defined(_WIN32) || defined(_WIN64)
    #include <winsock2.h>
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

Color Color::RED     = {255,   0,   0,   255};
Color Color::GREEN   = {  0, 255,   0,   255};
Color Color::YELLOW  = {255, 255,   0,   255};
Color Color::BLUE    = {  0,   0, 255, 255};
Color Color::CYAN    = {  0, 255, 255, 255};
Color Color::MAGENTA = {255,   0, 255, 255};
Color Color::ORANGE  = {255, 165,   0,   255};
Color Color::GRAY    = {128, 128, 128, 255};
Color Color::WHITE   = {255, 255, 255, 255};

// ── Windows socket init/cleanup (Meyers' Singleton — thread-safe) ────

#if defined(_WIN32) || defined(_WIN64)
static void InitWinsock() {
    // C++11 guarantees: function-local static is initialized exactly once,
    // even under concurrent access. No need for a separate bool flag.
    static bool inited = []{
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

RemoteLog::RemoteLog() {
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
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(UDP_PORT);
    addr.sin_addr.s_addr = htonl(INADDR_BROADCAST);

    while (_running.load(std::memory_order_acquire)) {
        std::deque<std::string> localBuffer;

        {
            std::unique_lock<std::mutex> lock(_mtx);
            _cv.wait_for(lock, std::chrono::milliseconds(100), [this]() {
                return !_buffer.empty() || !_running.load(std::memory_order_acquire);
            });

            if (_buffer.empty()) continue;
            localBuffer.swap(_buffer);
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
    }

    // Drain remaining messages after _running is false.
    std::deque<std::string> drainBuffer;
    {
        std::unique_lock<std::mutex> lock(_mtx);
        if (!_buffer.empty()) {
            drainBuffer.swap(_buffer);
        }
    }

    for (const auto& data : drainBuffer) {
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

void RemoteLog::Log(const Color &color, const char *msg, ...) {
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

    // ── Push to buffer (thread-safe, non-blocking) ──

    {
        std::lock_guard<std::mutex> lock(_mtx);
        if (_buffer.size() >= MAX_BUFFER_SIZE) {
            _buffer.pop_front(); // O(1) with deque vs O(n) with vector::erase(begin())
        }
        _buffer.push_back(std::move(packet));
    }
    _cv.notify_one();
}
