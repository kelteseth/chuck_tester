# testa

Python controller for **chuck** — AI-driven Qt/QML testing.

## Install

```bash
uv sync
```

## Usage

```bash
# Interactive REPL
uv run testa repl --port 1408

# Replay a recorded session
uv run testa replay session.jsonl

# Export JSONL to a Python test
uv run testa export session.jsonl --to-python test_flow.py
```

## Python API

```python
from testa import TestaClient

async with TestaClient(port=1408) as app:
    await app.click("saveBtn")
    await app.keys("nameField", text="hello")
    items = await app.find(interactive=True)
```
