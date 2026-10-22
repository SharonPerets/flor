"""Finds new versions of Gorgi and Gorgi Helper on GitHub, and installs them.

Each finished version is a GitHub release called "gorgi-v<version>" with:
  GorgiHelper-Setup-<version>.exe   the Windows installer
  gorgi-firmware-<version>.bin      Gorgi's firmware, sent to Gorgi over Wi-Fi
  gorgi-full-<version>.bin          the same firmware for a first install by USB
"""

import hashlib
import logging
import re
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path

import httpx

log = logging.getLogger("gorgi.update")

REPO = "SharonPerets/flor"
LATEST_URL = f"https://api.github.com/repos/{REPO}/releases/latest"
RELEASES_PAGE = f"https://github.com/{REPO}/releases/latest"
TAG_PREFIX = "gorgi-v"
UPDATE_PORT = 8080  # Gorgi listens for firmware here


def parse_version(text):
    """'3.4' -> (3, 4); anything odd -> (0,)"""
    nums = re.findall(r"\d+", text or "")
    return tuple(int(n) for n in nums[:3]) if nums else (0,)


def is_newer(candidate, current):
    return parse_version(candidate) > parse_version(current)


@dataclass
class Release:
    version: str
    helper_url: str = ""
    firmware_url: str = ""
    page_url: str = RELEASES_PAGE


async def fetch_latest(client):
    """The newest published release, or None if there isn't one yet."""
    resp = await client.get(LATEST_URL, headers={"Accept": "application/vnd.github+json",
                                                 "User-Agent": "GorgiHelper"}, timeout=20)
    if resp.status_code == 404:
        return None
    resp.raise_for_status()
    data = resp.json()
    tag = data.get("tag_name", "")
    if not tag.startswith(TAG_PREFIX):
        return None
    release = Release(version=tag[len(TAG_PREFIX):], page_url=data.get("html_url") or RELEASES_PAGE)
    for asset in data.get("assets", []):
        name, url = asset.get("name", ""), asset.get("browser_download_url", "")
        if name.startswith("GorgiHelper-Setup") and name.endswith(".exe"):
            release.helper_url = url
        elif name.startswith("gorgi-firmware") and name.endswith(".bin"):
            release.firmware_url = url
    return release


async def download(client, url):
    resp = await client.get(url, follow_redirects=True, timeout=600,
                            headers={"User-Agent": "GorgiHelper"})
    resp.raise_for_status()
    return resp.content


async def send_firmware(client, address, key, data):
    """Uploads firmware to Gorgi. Returns (ok, message)."""
    params = {"key": key, "size": str(len(data)), "md5": hashlib.md5(data).hexdigest()}
    try:
        resp = await client.post(f"http://{address}:{UPDATE_PORT}/update", params=params,
                                 files={"firmware": ("firmware.bin", data, "application/octet-stream")},
                                 timeout=300)
    except httpx.HTTPError as e:
        return False, f"couldn't reach Gorgi ({e})"
    if resp.status_code == 200:
        return True, "installed"
    if resp.status_code == 403:
        return False, "Gorgi is paired with a different helper"
    return False, resp.text.strip() or f"error {resp.status_code}"


def save_installer(data, version):
    path = Path(tempfile.gettempdir()) / f"GorgiHelper-Setup-{version}.exe"
    path.write_bytes(data)
    return path


def run_installer(path):
    """Starts the installer quietly. It closes this app, updates it and starts it again."""
    flags = 0
    if sys.platform == "win32":
        flags = subprocess.DETACHED_PROCESS | subprocess.CREATE_NEW_PROCESS_GROUP
    subprocess.Popen([str(path), "/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART", "/CLOSEAPPLICATIONS"],
                     close_fds=True, creationflags=flags)
