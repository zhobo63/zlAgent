#include "remote_log.h"

#if defined(_WIN32) || defined(_WIN64)
    #include <winsock2.h>
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
#endif

#include <cstdarg>
#include <chrono>

// Color definitions
Color Color::RED     = {255, 0,   0,   255};
Color Color::GREEN   = {0,   255, 0,   255};
Color Color::YELLOW  = {255, 255, 0,   255};
Color Color::BLUE    = {0,   0,   255, 255};
Color Color::CYAN    = {0,   255, 255, 255};
Color Color::MAGENTA = {255, 0,   255, 255};
Color Color::ORANGE  = {255, 165, 0,   255};
Color Color::GRAY    = {128, 128, 128, 255};
Color Color::WHITE   = {255, 255, 255, 255};

// Windows socket init/cleanup helper
#if defined(_WIN32) || defined(_WIN64)
static bool g_winsockInitialized = false;

static void InitWinsock() {
    if (!g_winsockInitialized) {
        WSADATA wsaData;
        WSAStartup(MAKEWORD(2, 2), &wsaData);
        g_winsockInitialized = true;
    }
}

static void CleanupWinsock() {
    if (g_winsockInitialized) {
        WSACleanup();
        g_winsockInitialized = false;
    }
}
#endif

RemoteLog::RemoteLog() {
#if defined(_WIN32) || defined(_WIN64)
    InitWinsock();
#endif
    _running = true;
    _senderThread = std::thread(&RemoteLog::SendWorker, this);
}

RemoteLog::~RemoteLog() {
    _running = false;
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
    if (_socket >= 0) return; // already initialized

#if defined(_WIN32) || defined(_WIN64)
    _socket = (int)WSASocket(AF_INET, SOCK_DGRAM, IPPROTO_UDP, nullptr, 0, 0);
#else
    _socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
#endif

    if (_socket < 0) {
        return; // failed silently
    }

    // Enable broadcast
    int optval = 1;
    setsockopt(_socket, SOL_SOCKET, SO_BROADCAST, (const char*)&optval, sizeof(optval));

    // Set non-blocking
#if defined(_WIN32) || defined(_WIN64)
    u_long mode = 1;
    ioctlsocket(_socket, FIONBIO, &mode);
#else
    int flags = fcntl(_socket, F_GETFL, 0);
    fcntl(_socket, F_SETFL, flags | O_NONBLOCK);
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
    while (_running) {
        std::vector<std::string> localBuffer;

        {
            std::unique_lock<std::mutex> lock(_mtx);
            _cv.wait_for(lock, std::chrono::milliseconds(100), [this]() {
                return !_buffer.empty() || !_running;
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

            struct sockaddr_in addr = {};
            addr.sin_family = AF_INET;
            addr.sin_port   = htons(UDP_PORT);
            addr.sin_addr.s_addr = htonl(INADDR_BROADCAST);

#if defined(_WIN32) || defined(_WIN64)
            sendto((SOCKET)_socket, data.data(), (int)data.size(), 0,
                   (struct sockaddr*)&addr, sizeof(addr));
#else
            sendto(_socket, data.data(), data.size(), 0,
                   (struct sockaddr*)&addr, sizeof(addr));
#endif
        }
    }
}

void RemoteLog::Log(const Color &color, const char *msg, ...) {
    va_list args;
    va_start(args, msg);

    // Format message into buffer
    std::string formatted;
    formatted.resize(4096);
    int len = vsnprintf(&formatted[0], formatted.size(), msg, args);
    if (len < 0) {
        va_end(args);
        return;
    }
    if (static_cast<size_t>(len) >= formatted.size()) {
        formatted.resize(len + 1);
        vsnprintf(&formatted[0], formatted.size(), msg, args);
    } else {
        formatted.resize(len);
    }
    va_end(args);

    // Build binary packet: [LogPacket header][msg + '\0']
    std::string packet;
    packet.reserve(sizeof(LogPacket) + formatted.size() + 1);

    LogPacket hdr = {};
    hdr.r = color.r;
    hdr.g = color.g;
    hdr.b = color.b;
    hdr.a = color.a;
    packet.append(reinterpret_cast<char*>(&hdr), sizeof(hdr));
    packet.append(formatted);
    packet.push_back('\0');

    // Push to buffer (thread-safe, non-blocking)
    {
        std::lock_guard<std::mutex> lock(_mtx);
        if (_buffer.size() >= MAX_BUFFER_SIZE) {
            _buffer.erase(_buffer.begin()); // drop oldest
        }
        _buffer.push_back(std::move(packet));
    }
    _cv.notify_one();
}
