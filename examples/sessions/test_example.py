# Auto-generated from example_session.jsonl
import pytest
from testa import TestaClient


@pytest.mark.asyncio
async def test_example_session():
    async with TestaClient(port=1408) as app:
        await app.wait('mainWindow', property='visible', value=True, timeout=5000)
        await app.screenshot(save_to="screenshots/initial_state.png")
        await app.click('nameField')
        await app.keys('nameField', text='World')
        await app.click('greetBtn')
        await app.get('titleLabel', props='text')
        await app.screenshot(save_to="screenshots/greeted.png")
        await app.click('resetBtn')
        await app.screenshot(save_to="screenshots/reset.png")
