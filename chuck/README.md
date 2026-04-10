# chuck

Python controller for **chuck_tester** — AI-driven Qt/QML testing.

## Install

```bash
uv sync
```

## Usage

```bash
# Interactive REPL
uv run chuck repl --port 1408

# Replay a recorded session
uv run chuck replay session.jsonl

# Export JSONL to a Python test
uv run chuck export session.jsonl --to-python test_flow.py
```

## Python API

```python
from chuck import ChuckClient

async with ChuckClient(port=1408) as app:
    await app.click("saveBtn")
    await app.keys("nameField", text="hello")
    items = await app.find(interactive=True)
```
