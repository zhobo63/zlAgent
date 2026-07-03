#include "pch.h"

#include "key_watcher.h"
#include "logger.h"

namespace agent {

#ifdef _WIN32
#include <conio.h>
#include <windows.h>

#define getch _getch
#define kbhit _kbhit

void init_keyboard() {}
void close_keyboard() {}

#else
#include <stdio.h>
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>

static struct termios oldt, newt;

static void init_keyboard() {
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);
}

static void close_keyboard() {
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
}

static int kbhit() {
    struct timeval tv;
    fd_set fds;
    tv.tv_sec = 0;
    tv.tv_usec = 0;
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);
    select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv);
    return FD_ISSET(STDIN_FILENO, &fds);
}

static int getch() {
    return getchar();
}

#endif

std::thread*       KeyWatcher::s_thread      = nullptr;
std::atomic<bool>  KeyWatcher::s_running      = false;
InterruptCallback  KeyWatcher::s_callback     = nullptr;

void KeyWatcher::on_key(InterruptCallback cb) {
    s_callback = std::move(cb);
}

void KeyWatcher::start() {
    if (s_running.load()) return; // already running
    s_running.store(true);

    init_keyboard();

    s_thread = new std::thread([] {
        while (s_running.load()) {
            int ch = 0;
            if (kbhit()) {
                ch = getch();
				//LOG_DEBUG("KeyWatcher", "Key pressed: " + std::to_string(ch));
                if (s_callback) s_callback(ch);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    });
}

void KeyWatcher::stop() {
    if (!s_running.load()) return;
    s_running.store(false);
    if (s_thread) {
        s_thread->join();
        delete s_thread;
        s_thread = nullptr;
    }
    close_keyboard();
}

} // namespace agent
