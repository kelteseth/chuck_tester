# Chuck API Reference

## ChuckServer

The only public header: `#include <chuck/ChuckServer.h>`

### Config

```cpp
namespace chuck {

struct Config {
    quint16 port = 1408;
};

} // namespace chuck
```

### Class

```cpp
namespace chuck {

class ChuckServer : public QObject {
    Q_OBJECT
public:
    explicit ChuckServer(QQuickWindow& window, Config config);
    explicit ChuckServer(QQuickWindow& window);  // uses default Config
    ~ChuckServer() override;

    auto start() -> bool;   // returns true if server started successfully
    auto stop() -> void;    // stops the server and disconnects all clients
};

} // namespace chuck
```

### Usage

```cpp
#include <chuck/ChuckServer.h>

// Default port (1408)
chuck::ChuckServer server(*window);
server.start();

// Custom port
chuck::ChuckServer server(*window, {.port = 9223});
server.start();
```

### Lifetime

- `ChuckServer` must outlive the `QQuickWindow` it's attached to
- Call `start()` after the QML engine has loaded and the window exists
- The server stops automatically when destroyed
- Multiple clients can connect simultaneously

### Thread Safety

All commands execute on the Qt main thread. The WebSocket server runs on the same event loop as your application. This is intentional — QML items can only be accessed from the main thread.

## CMake Integration

```cmake
# As subdirectory
add_subdirectory(chuck)
target_link_libraries(myapp PRIVATE chuck)

# As installed package
find_package(chuck REQUIRED)
target_link_libraries(myapp PRIVATE chuck::chuck)
```

Chuck is built as a static library by default.
