# Getting Started

## Requirements

- **Qt 6.5+** with modules: Quick, Qml, WebSockets, Test
- **CMake 3.18+** and a C++23 compiler
- **Python 3.12+** with [uv](https://docs.astral.sh/uv/)

## 1. Add Chuck to Your App

### CMake

```cmake
# Option A: subdirectory (if chuck is in your repo)
add_subdirectory(path/to/chuck)
target_link_libraries(myapp PRIVATE chuck)

# Option B: installed package
find_package(chuck REQUIRED)
target_link_libraries(myapp PRIVATE chuck::chuck)
```

### C++ — Three Lines

```cpp
#include <chuck/ChuckServer.h>

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QQmlApplicationEngine engine;
    engine.loadFromModule("MyApp", "Main");

    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());

    // Add these three lines:
    chuck::ChuckServer server(*window);  // (1)!
    server.start();                       // (2)!

    return app.exec();                    // (3)!
}
```

1. Create a server attached to your window
2. Start listening on port 1408 (default)
3. The server runs on the main thread alongside the Qt event loop

### Custom Port

```cpp
chuck::ChuckServer server(*window, {.port = 9223});
```

## 2. Add objectNames to Your QML

Chuck finds items by `objectName`. Add them to anything you want to test:

```qml
Page {
    objectName: "listPage"

    Button {
        objectName: "addBtn"
        text: "Add"
        onClicked: stack.push(editPageComponent)
    }

    ListView {
        delegate: ItemDelegate {
            id: del
            objectName: "item_" + index      // (1)!
            required property int index

            Label {
                objectName: "label_" + del.index  // (2)!
                text: model.title
            }
        }
    }
}
```

1. Delegates get indexed names
2. Children reference the delegate's index via its `id`

## 3. Install Testa

```bash
# Create a pyproject.toml or add testa to your dependencies
uv add testa --editable path/to/testa

# Or install directly
uv pip install path/to/testa
```

## 4. Write Your First Test

```python title="test_myapp.py"
import pytest
from testa import TestaClient


@pytest.mark.asyncio
async def test_hello():
    async with TestaClient(port=1408) as app:
        # Wait for the app to load
        await app.wait("listPage", property="visible", value=True, timeout=5000)

        # Click a button
        await app.click("addBtn")

        # Type into a field
        await app.keys("nameField", text="Hello world")

        # Read back and verify
        props = await app.get("nameField", props="text")
        assert props["text"] == "Hello world"

        # Clean up
        await app.quit()
```

## 5. Run It

Start your app first, then run the test:

```bash
# Terminal 1: start the app
./build/myapp

# Terminal 2: run the test
uv run pytest test_myapp.py -v
```

Or use a test suite wrapper that handles both:

```python title="test_suite.py"
"""Start app, run tests, clean up."""
import subprocess, sys, time
from pathlib import Path

def main():
    exe = Path("build/myapp")
    proc = subprocess.Popen([str(exe)])
    time.sleep(2)

    try:
        rc = subprocess.call(
            [sys.executable, "-m", "pytest", "test_myapp.py", "-v"]
        )
    finally:
        proc.terminate()
        proc.wait(timeout=10)

    sys.exit(rc)

if __name__ == "__main__":
    main()
```

## Next Steps

- [Chuck Commands](chuck/commands.md) — full list of what chuck can do
- [Testa API](testa/api.md) — all Python methods
- [Synchronization Patterns](guide/synchronization.md) — handling animations and async operations
- [Todo App Example](examples/todoapp.md) — a complete worked example
