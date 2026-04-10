# Launch todoapp before running:
#   uv run --directory testa pytest examples/todoapp/test_todoapp.py -v
import pytest
from testa import TestaClient

STACK = "stack"


async def wait_nav(app: TestaClient):
    """Wait for StackView transition animation to finish."""
    await app.wait(STACK, property="busy", value=False, timeout=3000)


@pytest.mark.asyncio
async def test_todoapp():
    async with TestaClient(port=1408) as app:
        await app.wait("listPage", property="visible", value=True, timeout=5000)

        # Verify app started — list page is visible
        props = await app.get("listPage", props="visible")
        assert props["visible"] is True

        # List page should be empty
        await app.get("listPage", props="objectName")

        # ── Add first todo ───────────────────────────────────────────────
        await app.click("addBtn")
        await wait_nav(app)
        await app.keys("titleField", text="Buy milk")
        await app.click("saveBtn")
        await wait_nav(app)
        await app.wait("todoItem_0", timeout=3000)
        props = await app.get("todoItem_0", props="objectName")
        assert props["objectName"] == "todoItem_0"

        # ── Add second todo ──────────────────────────────────────────────
        await app.click("addBtn")
        await wait_nav(app)
        await app.keys("titleField", text="Walk the dog")
        await app.click("saveBtn")
        await wait_nav(app)
        await app.wait("todoItem_1", timeout=3000)
        props = await app.get("todoItem_1", props="objectName")
        assert props["objectName"] == "todoItem_1"

        # ── Toggle todo ──────────────────────────────────────────────────
        await app.click("check_0")
        props = await app.get("check_0", props="checked")
        assert props["checked"] is True

        await app.click("check_0")
        props = await app.get("check_0", props="checked")
        assert props["checked"] is False

        # ── Edit todo ────────────────────────────────────────────────────
        await app.click("editBtn_0")
        await wait_nav(app)
        await app.keys("titleField", shortcut="Ctrl+A")
        await app.keys("titleField", text="Buy oat milk")
        await app.click("saveBtn")
        await wait_nav(app)
        props = await app.get("label_0", props="text")
        assert props["text"] == "Buy oat milk"

        # ── Cancel edit ──────────────────────────────────────────────────
        await app.click("editBtn_0")
        await wait_nav(app)
        await app.keys("titleField", shortcut="Ctrl+A")
        await app.keys("titleField", text="SHOULD NOT SAVE")
        await app.click("cancelBtn")
        await wait_nav(app)
        props = await app.get("label_0", props="text")
        assert props["text"] == "Buy oat milk"

        # ── Delete todo ──────────────────────────────────────────────
        await app.wait("deleteBtn_1", property="visible", value=True, timeout=3000)
        await app.click("deleteBtn_1")

        # ── Save / Load ─────────────────────────────────────────────────
        await app.keys(shortcut="Ctrl+S")
        await app.wait("deleteBtn_0", property="visible", value=True, timeout=3000)
        await app.click("deleteBtn_0")
        await app.keys(shortcut="Ctrl+L")
        await app.wait("todoItem_0", timeout=3000)
        props = await app.get("label_0", props="text")
        assert props["text"] == "Buy oat milk"

        await app.quit()
