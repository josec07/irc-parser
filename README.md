# IRCP (IRC Parser)

IRCP is a compiled C++ parser for live text sockets (Twitch IRC). 

By isolating the raw socket lifecycle into a dedicated C++ process, IRCP manages network fragmentation and pipes clean JSON Lines (`.jsonl`) directly to standard output for downstream consumption.

## Architecture

IRCP operates as a standard Unix pipe utility:

[ Raw TCP Socket ] -> [ IRCP (C++) ] -> [ stdout (JSON Lines) ] -> [ Downstream Application ]

## Quick Start

### Prerequisites
* CMake 3.14+
* C++17 Compiler (GCC/Clang)
* Internet connection (CMake will fetch Boost Asio and nlohmann_json if needed, or link system libs)

### Build
```bash
git clone https://github.com/yourusername/irc-parser.git
cd irc-parser
mkdir build && cd build
cmake ..
make
```

### Usage

Run the binary by passing a Twitch channel name or URL:

```bash
./irc-parser justin
```

### Example Output (JSONL)
```json
{"channel":"justin","message":"Hello world","username":"viewer123"}
{"channel":"justin","message":"pogchamp","username":"some_user"}
```

