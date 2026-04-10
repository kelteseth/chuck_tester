"""Quick demo: drive the minimal example app via chuck."""

import asyncio
from chuck import ChuckClient


async def demo():
    async with ChuckClient(port=1408) as app:
        print("Connected to chuck!")

        # Inspect the tree
        items = await app.find(interactive=True)
        print(f"\nInteractive items ({len(items)}):")
        for it in items:
            name = it.get("objectName", "?")
            typ = it.get("type", "?")
            print(f"  {name} ({typ})")

        # Type a name
        await app.keys("nameField", text="World")
        print('\nTyped "World" into nameField')

        # Check if text was received
        nf_props = await app.get("nameField", props="text")
        print(f"nameField text after typing: '{nf_props.get('text')}'")

        # Click greet
        result = await app.click("greetBtn")
        print(f"Clicked greetBtn at {result.get('point')}")

        # Read the label
        props = await app.get("titleLabel", props="text")
        print(f"titleLabel text: {props.get('text')}")

        # Take a screenshot
        ss = await app.screenshot(save_to="demo_screenshot.png")
        print(f"\nScreenshot: {ss['width']}x{ss['height']} saved to demo_screenshot.png")

        # Reset
        await app.click("resetBtn")
        props = await app.get("titleLabel", props="text")
        print(f"After reset: {props.get('text')}")

        print("\nDone!")


asyncio.run(demo())
