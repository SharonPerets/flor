"""Notices Teams calls, and mutes / unmutes Teams from Gorgi's button.

A call = Teams is using the microphone right now. Windows keeps a list of which
apps use the microphone (the privacy "recently used" list in the registry);
an app that's using it now has LastUsedTimeStop = 0.

Mute: Teams' own shortcut, Ctrl+Shift+M, sent to the Teams window (which is
brought to the front for a moment, then the window you were in comes back).
"""

import logging
import sys
import time

log = logging.getLogger("gorgi.call")

IS_WINDOWS = sys.platform == "win32"
MIC_KEY = r"Software\Microsoft\Windows\CurrentVersion\CapabilityAccessManager\ConsentStore\microphone"
CALL_APPS = ("teams",)  # parts of the app's name


def apps_using_mic():
    """Names of the apps using the microphone right now (Windows only)."""
    if not IS_WINDOWS:
        return []
    import winreg

    found = []

    def scan(key, prefix=""):
        i = 0
        while True:
            try:
                name = winreg.EnumKey(key, i)
            except OSError:
                break
            i += 1
            if name == "NonPackaged":
                try:
                    with winreg.OpenKey(key, name) as sub:
                        scan(sub, "")
                except OSError:
                    pass
                continue
            try:
                with winreg.OpenKey(key, name) as app:
                    stop, _ = winreg.QueryValueEx(app, "LastUsedTimeStop")
                    start, _ = winreg.QueryValueEx(app, "LastUsedTimeStart")
                if stop == 0 and start:
                    found.append(prefix + name)
            except OSError:
                pass

    for root in (winreg.HKEY_CURRENT_USER,):
        try:
            with winreg.OpenKey(root, MIC_KEY) as key:
                scan(key)
        except OSError:
            pass
    return found


def in_call(apps, words=CALL_APPS):
    return any(w in a.lower() for a in apps for w in words)


# ---------------------------------------------------------------- mute


def teams_mute():
    """Presses Ctrl+Shift+M in Teams. Returns True if a Teams window was found."""
    if not IS_WINDOWS:
        return False
    import ctypes
    from ctypes import wintypes

    user32 = ctypes.WinDLL("user32", use_last_error=True)
    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)

    def exe_of(hwnd):
        pid = wintypes.DWORD()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
        handle = kernel32.OpenProcess(0x1000, False, pid.value)  # PROCESS_QUERY_LIMITED_INFORMATION
        if not handle:
            return ""
        try:
            buf = ctypes.create_unicode_buffer(1024)
            size = wintypes.DWORD(1024)
            if kernel32.QueryFullProcessImageNameW(handle, 0, buf, ctypes.byref(size)):
                return buf.value.rsplit("\\", 1)[-1].lower()
            return ""
        finally:
            kernel32.CloseHandle(handle)

    windows = []

    @ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    def each(hwnd, _):
        if user32.IsWindowVisible(hwnd):
            length = user32.GetWindowTextLengthW(hwnd)
            if length:
                title = ctypes.create_unicode_buffer(length + 1)
                user32.GetWindowTextW(hwnd, title, length + 1)
                if exe_of(hwnd) in ("ms-teams.exe", "teams.exe"):
                    windows.append((hwnd, title.value))
        return True

    user32.EnumWindows(each, 0)
    if not windows:
        log.info("teams mute: no Teams window")
        return False
    # the meeting window if there is one, else any Teams window
    menu_words = ("chat", "activity", "calendar", "teams |", "microsoft teams")

    def score(item):
        t = item[1].lower()
        return (0 if any(w in t for w in ("meeting", "call", "|")) and not t.startswith(menu_words) else 1)

    hwnd, title = sorted(windows, key=score)[0]
    before = user32.GetForegroundWindow()

    VK_CONTROL, VK_SHIFT, VK_M = 0x11, 0x10, 0x4D
    KEYUP = 0x0002

    def key(vk, up=False):
        user32.keybd_event(vk, 0, KEYUP if up else 0, 0)

    def bring_front(target):
        # Windows only lets the app in front hand over the focus: join its input queue for a moment
        front = user32.GetForegroundWindow()
        theirs = user32.GetWindowThreadProcessId(front, None) if front else 0
        mine = kernel32.GetCurrentThreadId()
        attached = bool(theirs and theirs != mine and user32.AttachThreadInput(mine, theirs, True))
        try:
            if user32.IsIconic(target):
                user32.ShowWindow(target, 9)  # SW_RESTORE
            user32.BringWindowToTop(target)
            user32.SetForegroundWindow(target)
        finally:
            if attached:
                user32.AttachThreadInput(mine, theirs, False)

    bring_front(hwnd)
    time.sleep(0.25)
    key(VK_CONTROL)
    key(VK_SHIFT)
    key(VK_M)
    key(VK_M, up=True)
    key(VK_SHIFT, up=True)
    key(VK_CONTROL, up=True)
    time.sleep(0.2)
    if before and before != hwnd:
        bring_front(before)
    log.info("teams mute: pressed Ctrl+Shift+M in %r", title)
    return True
