# chuck_tester

<p align="center">
  <img src="docs/assets/logo.svg" alt="chuck_tester" width="600">
</p>

UI testing for Qt/QML apps. Tester is a C++ library you link into your app. Chuck is the Python side that talks to it.

## Quick start

Add tester to your app:

```cpp
#include <tester/TesterServer.h>

// after your QML engine loads:
auto *window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
tester::TesterServer server(*window);
server.start();
```

Link it in CMake:

```cmake
find_package(Qt6 REQUIRED COMPONENTS Quick Qml WebSockets Test)
target_link_libraries(myapp PRIVATE Qt6::Quick Qt6::Qml tester)
```

Write a test:

```python
import pytest
from chuck import ChuckClient

@pytest.mark.asyncio
async def test_my_app():
    async with ChuckClient(port=1408) as app:
        await app.wait("mainWindow", property="visible", value=True, timeout=5000)
        await app.click("myButton")
        props = await app.get("myLabel", props="text")
        assert props["text"] == "Hello"
        await app.quit()
```

Run it (app must be running first):

```bash
uv run pytest examples/todoapp/test_todoapp.py -v
```

## What chuck can do

- `click` / `keys` / `drag` / `swipe` — interact with QML items by objectName
- `get` — read any property
- `wait` — wait for items to appear or properties to change
- `find` / `tree` — search and inspect the QML tree
- `screenshot` — capture PNGs, compare against baselines (SSIM, pHash, exact)
- `quit` — close the app

## Building

Requires Qt 6, CMake, C++23.

```bash
cmake --preset <your-preset>
cmake --build --preset <your-build-preset>
```

## Documentation

```bash
uv run --group docs mkdocs serve
```

## Examples

- `examples/minimal/` — hello world
- `examples/todoapp/` — full app with StackView navigation, Material Design, and a test
- `examples/text_editor/` — barebone skeleton for AI-driven development (see its README for the prompt)

## License

AGPL-3.0-only OR LicenseRef-EliasSteurerTachiom
