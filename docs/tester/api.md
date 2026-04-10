# Tester API Reference

## TesterServer

The only public header: `#include <tester/TesterServer.h>`

### Config

```cpp
namespace tester {

struct Config {
    quint16 port = 1408;
};

} // namespace tester
```

### Class

```cpp
namespace tester {

class TesterServer : public QObject {
    Q_OBJECT
public:
    explicit TesterServer(QQuickWindow& window, Config config);
    explicit TesterServer(QQuickWindow& window);  // uses default Config
    ~TesterServer() override;

    auto start() -> bool;   // returns true if server started successfully
    auto stop() -> void;    // stops the server and disconnects all clients
};

} // namespace tester
```

### Usage

```cpp
#include <tester/TesterServer.h>

// Default port (1408)
tester::TesterServer server(*window);
server.start();

// Custom port
tester::TesterServer server(*window, {.port = 9223});
server.start();
```

### Lifetime

- `TesterServer` must outlive the `QQuickWindow` it's attached to
- Call `start()` after the QML engine has loaded and the window exists
- The server stops automatically when destroyed
- Multiple clients can connect simultaneously

### Thread Safety

All commands execute on the Qt main thread. The WebSocket server runs on the same event loop as your application. This is intentional — QML items can only be accessed from the main thread.

## CMake Integration

```cmake
# As subdirectory
add_subdirectory(tester)
target_link_libraries(myapp PRIVATE tester)

# As installed package
find_package(tester REQUIRED)
target_link_libraries(myapp PRIVATE tester::tester)
```

Tester is built as a static library by default.
