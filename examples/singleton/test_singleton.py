"""Demo test for the singleton example app.

Demonstrates reading and waiting on properties of a QML singleton
that is NOT part of the QQuickItem tree, via the @URI/Type syntax.

Run the singleton_example binary first, then `pytest -v test_singleton.py`.
"""
import pytest
from chuck import ChuckClient


@pytest.mark.asyncio
async def test_read_singleton_properties():
    async with ChuckClient(port=1408) as app:
        # Wait on a content item, not the window itself — the tree walk starts at
        # QQuickWindow::contentItem(), so the window's own objectName isn't visible.
        await app.wait("messageLabel", property="visible", value=True, timeout=5000)

        props = await app.get("@SingletonExample/AppState", props="counter,message")
        assert props["counter"] == 0
        assert props["message"] == "Hello from a singleton"


@pytest.mark.asyncio
async def test_wait_on_singleton_property():
    async with ChuckClient(port=1408) as app:
        # Wait on a content item, not the window itself — the tree walk starts at
        # QQuickWindow::contentItem(), so the window's own objectName isn't visible.
        await app.wait("messageLabel", property="visible", value=True, timeout=5000)

        # Driving via the QML UI updates AppState.counter through the binding
        await app.click("incBtn")
        await app.wait(
            "@SingletonExample/AppState",
            property="counter",
            value=1,
            timeout=2000,
        )


@pytest.mark.asyncio
async def test_click_on_singleton_is_rejected():
    """Singletons are not QQuickItems — click must fail with NotInteractive (-32010)."""
    async with ChuckClient(port=1408) as app:
        # Wait on a content item, not the window itself — the tree walk starts at
        # QQuickWindow::contentItem(), so the window's own objectName isn't visible.
        await app.wait("messageLabel", property="visible", value=True, timeout=5000)

        with pytest.raises(Exception) as exc:
            await app.click("@SingletonExample/AppState")
        # The exact error class depends on chuck's mapping; the code should be -32010.
        assert "-32010" in str(exc.value) or "NotInteractive" in str(exc.value) \
            or "not a QQuickItem" in str(exc.value) or "non-interactive" in str(exc.value)
