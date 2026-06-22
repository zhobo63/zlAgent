#pragma once

#include <atomic>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

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
    RemoteLog();
    ~RemoteLog();

    /*
     * Send UDP to port 995
     * Binary format: [4-byte LogPacket header][msg + '\0']
     */
    void Log(const Color &color, const char *msg, ...);

    static RemoteLog& GetInstance() {
        std::call_once(initFlag_, []{
            gInstance = std::make_unique<RemoteLog>();
        });
        return *gInstance;
    }
    static void FreeInstance() {
        gInstance.reset();
    }

private:
    int _socket = -1;
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
