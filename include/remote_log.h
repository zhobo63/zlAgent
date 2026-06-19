#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <mutex>
#include <condition_variable>
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
          if(!gInstance)
              gInstance = std::make_unique<RemoteLog>();
          return *gInstance;
      }
      static void FreeInstance() {
          gInstance.reset();
      }

private:
    int _socket = -1;
    std::vector<std::string> _buffer;
    std::mutex              _mtx;
    std::condition_variable _cv;
    bool                    _running = false;
    std::thread             _senderThread;

    static constexpr size_t MAX_BUFFER_SIZE = 1024;
    static constexpr int    UDP_PORT        = 995;
    static std::unique_ptr<RemoteLog> gInstance;

    void InitSocket();
    void CloseSocket();
    void SendWorker();
};

inline std::unique_ptr<RemoteLog> RemoteLog::gInstance;

#define LOG RemoteLog::GetInstance().Log
