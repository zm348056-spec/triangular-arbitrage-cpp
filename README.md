# triangular-arbitrage-cpp

[![C++](https://img.shields.io/badge/C%2B%2B-17-00599C?logo=cplusplus&logoColor=white)](https://isocpp.org/)
[![CMake](https://img.shields.io/badge/CMake-3.20-064F8C?logo=cmake&logoColor=white)](https://cmake.org/)
[![Linux](https://img.shields.io/badge/Linux-FCC624?logo=linux&logoColor=black)](https://www.linux.org/)
[![MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)

Low-latency C++17 triangular arbitrage scanner with non-blocking UDP price-feed
networking. Rates are maintained in a fixed 256-symbol table updated by an atomic
writer thread, while a scanner thread detects negative-weight cycles using
log-price transforms.

## How It Works

1. **Feed thread** — binds a non-blocking UDP socket, applies a large `SO_RCVBUF`,
   and parses a compact binary frame (`seq`, `count`, per-symbol bid/ask fixed-point).
2. **Rate table** — prices are published through per-symbol `std::atomic<uint64_t>`
   pairs, so the scanner reads a consistent snapshot without locks.
3. **Scanner thread** — converts quoted rates to `-ln(rate)` edge weights and
   enumerates all triangles over the active symbol set, flagging cycles whose
   implied return exceeds the fee-scaled threshold.
4. **Ticker** — measures scan latency with `std::chrono::steady_clock`, reporting
   average, min, and max per cycle.

## Build

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j

./build/triarb 127.0.0.1 9000
```

## Feed Frame Format

```
offset  size  field
0       4     magic (0x54415242 = "TARB")
4       4     sequence number (little-endian)
8       1     number of quotes in frame
9       n*12  quote: symbol(u8) + bid u32 fixed-point (1e6) + ask u32 fixed-point
```

Scaled prices use 1e6 fixed-point, matching typical exchange quote decimal places.

## Project Layout

```
├── CMakeLists.txt   # CMake 3.20+, C++17, -O3 with warnings
├── src/
│   └── main.cpp     # UDP feed thread, rate table, triangle scanner, perf ticker
└── README.md
```

## License

MIT