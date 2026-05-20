"""Demo test for the popup example app.

Demonstrates addressing Popup/Drawer/Dialog by objectName. These derive from
QQuickPopup → QObject (not QQuickItem), so the tree walk skips them — tester
reaches them via a fallback QObject-children scan of the window.

Run via: uv run examples/popup/test_suite.py
"""
import pytest
from chuck import ChuckClient


@pytest.mark.asyncio
async def test_drawer_open_close():
    async with ChuckClient(port=1408) as app:
        await app.wait("openDrawerBtn", property="visible", value=True, timeout=5000)

        # Drawer starts closed
        props = await app.get("navDrawer", props="opened,position")
        assert props["opened"] is False
        assert props["position"] == 0.0

        # Open via the button, wait for the property change
        await app.click("openDrawerBtn")
        await app.wait("navDrawer", property="opened", value=True, timeout=2000)
        await app.wait("navDrawer", property="position", value=1.0, timeout=2000)

        # Content inside the drawer is now visible (popupItem in overlay tree)
        props = await app.get("drawerLabel", props="text")
        assert props["text"] == "Drawer content"

        # Close the drawer so subsequent tests start with a clean modal state.
        # Drawer's default closePolicy includes CloseOnEscape. Wait for the
        # full close — opened flips early (when the close anim starts), but the
        # popup item stays visible/modal during the slide-out.
        await app.keys(key="escape")
        await app.wait("navDrawer", property="opened", value=False, timeout=2000)
        await app.wait("navDrawer", property="position", value=0.0, timeout=2000)
        await app.wait("navDrawer", property="visible", value=False, timeout=2000)


@pytest.mark.asyncio
async def test_dialog_visibility():
    async with ChuckClient(port=1408) as app:
        await app.wait("openDialogBtn", property="visible", value=True, timeout=5000)

        # Dialog is declared but not yet shown
        props = await app.get("confirmDialog", props="opened,title")
        assert props["opened"] is False
        assert props["title"] == "Confirm"

        await app.click("openDialogBtn")
        await app.wait("confirmDialog", property="opened", value=True, timeout=2000)

        # Inner content reachable via the overlay tree once shown
        await app.wait("confirmDialogMessage", property="visible", value=True, timeout=2000)
        props = await app.get("confirmDialogMessage", props="text")
        assert props["text"] == "Proceed?"


@pytest.mark.asyncio
async def test_click_on_popup_is_rejected():
    """Popups aren't QQuickItems — click must return NotInteractive (-32010)."""
    async with ChuckClient(port=1408) as app:
        await app.wait("openDrawerBtn", property="visible", value=True, timeout=5000)

        with pytest.raises(Exception) as exc:
            await app.click("navDrawer")
        msg = str(exc.value)
        assert "-32010" in msg or "non-interactive" in msg or "QQuickItem" in msg
