"""Tests for the one-window update flow, with fake downloads and a fake Gorgi (run: python test_updates.py)."""
import asyncio
import os
import sys
import tempfile

os.environ["HOME"] = tempfile.mkdtemp()  # config.json goes to a scratch folder
os.environ["APPDATA"] = os.environ["HOME"]
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gorgi_helper as gh  # noqa: E402
import updater  # noqa: E402

NEW = "9.9"
installed, stopped = [], []


class FakeWindow:
    def __init__(self, on_word):
        self.states, self.on_word, self.open = [], on_word, True

    def show(self, state):
        self.states.append(state)

    def close(self):
        self.open = False

    @property
    def last(self):
        return self.states[-1]


async def fetch_latest(_client):
    return updater.Release(NEW, "https://x/helper.exe", "https://x/fw.bin")


async def download(_client, _url, progress=None):
    for f in (0.0, 0.5, 1.0):
        progress and progress(f)
    return b"data"


def make_helper(gorgi_version="4.3.2", connected=True):
    h = gh.Helper(media=None, config={"helper_key": "k"})
    h.make_update_window = FakeWindow
    h.request_stop = lambda: stopped.append(1)
    if connected:
        h.ws, h.gorgi_version, h.gorgi_paired, h.gorgi_address = object(), gorgi_version, True, "1.2.3.4"

    async def send(_msg):
        pass

    h.send = send
    return h


async def send_firmware(h):
    async def fake(_client, _address, _key, _data, progress=None):
        progress and progress(1.0)

        async def reboot():  # Gorgi goes away and comes back with the new version
            h.ws, h.gorgi_version = None, None
            await asyncio.sleep(1.5)
            h.ws, h.gorgi_version = object(), NEW
        asyncio.get_running_loop().create_task(reboot())
        return True, "ok"
    return fake


async def main():
    updater.fetch_latest = fetch_latest
    updater.download = download
    updater.save_installer = lambda data, v: f"setup-{v}.exe"
    updater.run_installer = lambda path: installed.append(path)
    gh.save_config = lambda config: None
    sys.frozen = True  # like the installed app: the helper can update itself

    # 1) both need updating: "Update all" does the helper, which hands over to the new helper
    h = make_helper()
    h.open_updates()
    await asyncio.sleep(0.1)
    w = h.update_window
    assert w.last["button"] == "start" and [r["state"] for r in w.last["rows"]] == ["todo", "todo"], w.last
    assert w.last["rows"][0]["new"] == NEW and w.last["rows"][1]["now"] == "4.3.2"
    w.on_word("start")
    await asyncio.sleep(3.2)
    assert installed == [f"setup-{NEW}.exe"] and stopped == [1]
    assert h.config["update_flow"] == {"version": NEW} and w.last["rows"][0]["state"] == "busy"
    assert any("Downloading Gorgi Helper" in s["text"] for s in w.states)

    # 2) the new helper starts: reopens the window and does Gorgi, done only when Gorgi is back
    gh.VERSION = NEW
    h2 = make_helper()
    h2.send_firmware = None
    updater.send_firmware = await send_firmware(h2)
    await h2.continue_after_restart(h2.config.pop("update_flow", None) or {"version": NEW})
    w2 = h2.update_window
    assert any("restarting with version" in s["text"] for s in w2.states)
    assert w2.last["button"] == "close" and "All done" in w2.last["text"], w2.last
    assert [r["state"] for r in w2.last["rows"]] == ["ok", "ok"] and w2.last["rows"][1]["now"] == NEW
    assert not h2.updating

    # 3) only Gorgi needs it (helper already new), from Gorgi's own Update button
    h3 = make_helper()
    updater.send_firmware = await send_firmware(h3)
    await h3.update_all()
    await asyncio.sleep(4)
    assert "All done" in h3.update_window.last["text"], h3.update_window.last
    assert len(installed) == 1  # the helper wasn't reinstalled

    # 4) Gorgi off: says so, Try again; everything up to date: says so
    h4 = make_helper(connected=False)
    h4.open_updates()
    await asyncio.sleep(0.1)
    assert "isn't connected" in h4.update_window.last["text"] and h4.update_window.last["rows"][1]["state"] == "offline"
    h5 = make_helper(gorgi_version=NEW)
    h5.open_updates()
    await asyncio.sleep(0.1)
    assert h5.update_window.last["text"] == "Everything is up to date." and h5.update_window.last["button"] == "close"

    # 5) a failed Gorgi upload: Try again offered
    h6 = make_helper()
    h6.latest = updater.Release(NEW, "https://x/helper.exe", "https://x/fw.bin")

    async def broken(*_a, **_k):
        return False, "Gorgi said no"
    updater.send_firmware = broken
    await h6._update_gorgi_step()
    assert h6.update_window is None  # the window wasn't open: nothing shown, nothing broken
    h6.open_window_only()
    await h6._update_gorgi_step()
    assert h6.update_window.last["button"] == "retry" and "Gorgi said no" in h6.update_window.last["text"]
    assert h6.update_window.last["rows"][1]["state"] == "fail" and not h6.updating
    print("ALL OK")


asyncio.run(main())
