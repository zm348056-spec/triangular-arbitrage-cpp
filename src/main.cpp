#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <thread>

namespace {

constexpr uint32_t kFrameMagic = 0x54415242;
constexpr double kScale = 1'000'000.0;
constexpr std::size_t kSymbols = 8;
constexpr int kRateBuf = 64 * 1024;
constexpr double kMinReturn = 0.0005;

struct RateMatrix {
    std::atomic<double> bid[kSymbols][kSymbols];
    std::atomic<bool> active[kSymbols];

    RateMatrix() {
        for (auto& row : bid) {
            for (auto& cell : row) {
                cell.store(0.0, std::memory_order_relaxed);
            }
        }
        for (auto& slot : active) {
            slot.store(false, std::memory_order_relaxed);
        }
    }

    void update(uint8_t base, uint8_t quote, uint32_t bid_q, uint32_t ask_q) {
        double b = static_cast<double>(bid_q) / kScale;
        double a = static_cast<double>(ask_q) / kScale;
        if (b > 0.0) bid[base][quote].store(b, std::memory_order_release);
        if (a > 0.0) bid[quote][base].store(1.0 / a, std::memory_order_release);
        active[base].store(true, std::memory_order_release);
        active[quote].store(true, std::memory_order_release);
    }
};

int open_feed_socket(const char* host, uint16_t port) {
    int fd = ::socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0);
    if (fd < 0) return -1;
    int rcvbuf = kRateBuf;
    ::setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (::inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
        ::close(fd);
        return -1;
    }
    if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        ::close(fd);
        return -1;
    }
    return fd;
}

void feed_loop(int fd, RateMatrix& rates) {
    unsigned char buf[2048];
    pollfd pfd{fd, POLLIN, 0};
    for (;;) {
        if (::poll(&pfd, 1, 100) <= 0) continue;
        ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
        if (n < 9) continue;
        uint32_t magic;
        std::memcpy(&magic, buf, 4);
        if (magic != kFrameMagic) continue;
        std::size_t count = buf[8];
        if (count * 9 + 9 > static_cast<std::size_t>(n)) continue;
        std::size_t off = 9;
        for (std::size_t i = 0; i < count; ++i) {
            uint8_t sym = buf[off++];
            uint32_t bid, ask;
            std::memcpy(&bid, buf + off, 4);
            off += 4;
            std::memcpy(&ask, buf + off, 4);
            off += 4;
            if (sym < kSymbols) {
                rates.update(sym, static_cast<uint8_t>((sym + 1) % kSymbols), bid, ask);
            }
        }
    }
}

void scanner_loop(RateMatrix& rates, const std::atomic<bool>* running) {
    using Clock = std::chrono::steady_clock;
    auto best = Clock::duration::max();
    auto worst = Clock::duration::zero();
    auto total = Clock::duration::zero();
    std::size_t scans = 0;
    while (running->load(std::memory_order_acquire)) {
        auto start = Clock::now();
        for (std::size_t a = 0; a < kSymbols; ++a) {
            if (!rates.active[a].load(std::memory_order_acquire)) continue;
            for (std::size_t b = a + 1; b < kSymbols; ++b) {
                if (!rates.active[b].load(std::memory_order_acquire)) continue;
                for (std::size_t c = b + 1; c < kSymbols; ++c) {
                    if (!rates.active[c].load(std::memory_order_acquire)) continue;
                    double factor = rates.bid[a][b].load(std::memory_order_acquire) *
                                    rates.bid[b][c].load(std::memory_order_acquire) *
                                    rates.bid[c][a].load(std::memory_order_acquire);
                    if (factor > 1.0 + kMinReturn) {
                        std::printf("TRIANGLE %zu->%zu->%zu->%zu factor=%.6f\n", a, b, c, a, factor);
                    }
                }
            }
        }
        auto elapsed = Clock::now() - start;
        total += elapsed;
        best = std::min(best, elapsed);
        worst = std::max(worst, elapsed);
        ++scans;
        if (scans % 512 == 0) {
            double avg_us = std::chrono::duration<double, std::micro>(total).count() / scans;
            std::printf("scan=%zu avg=%.1fus min=%.1fus max=%.1fus\n", scans, avg_us,
                        std::chrono::duration<double, std::micro>(best).count(),
                        std::chrono::duration<double, std::micro>(worst).count());
            std::fflush(stdout);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

}  // namespace

int main(int argc, char** argv) {
    const char* host = argc > 1 ? argv[1] : "127.0.0.1";
    uint16_t port = argc > 2 ? static_cast<uint16_t>(std::atoi(argv[2])) : 9000;

    RateMatrix rates;
    int fd = open_feed_socket(host, port);
    if (fd < 0) {
        std::fprintf(stderr, "failed to bind feed socket %s:%u\n", host, port);
        return 1;
    }

    std::atomic<bool> running{true};
    std::thread feed(feed_loop, fd, std::ref(rates));
    std::thread scanner(scanner_loop, std::ref(rates), &running);

    std::printf("triarb %s listening on %s:%u (enter to stop)\n", TRIARB_VERSION, host, port);
    std::cin.get();

    running.store(false, std::memory_order_release);
    feed.join();
    scanner.join();
    ::close(fd);
    return 0;
}
