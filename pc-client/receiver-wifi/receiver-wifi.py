import json
import os
import re
import socket
import subprocess
import sys
from datetime import datetime
from typing import Any, Dict, List, Optional, Tuple


# =========================
# Config
# =========================

CONFIG_PATH = os.path.join(os.path.dirname(__file__), "config-wifi.json")


def load_config() -> Dict[str, Any]:
    if not os.path.exists(CONFIG_PATH):
        raise FileNotFoundError(
            f"config.json not found at {CONFIG_PATH}. "
            "Create a config.json file in the pc-client directory."
        )

    with open(CONFIG_PATH, "r", encoding="utf-8") as f:
        return json.load(f)


# =========================
# Platform volume backends
# =========================

class VolumeBackend:
    def set_master(self, volume_0_100: int) -> None:
        raise NotImplementedError

    def set_app(self, process_name: str, volume_0_100: int) -> bool:
        """Returns True if at least one session was updated."""
        raise NotImplementedError

    def set_spotify(self, volume_0_100: int) -> bool:
        """Best-effort; returns True if command was dispatched."""
        return False


class WindowsBackend(VolumeBackend):
    def __init__(self, apply_to_all_sessions: bool = True) -> None:
        self.apply_to_all_sessions = apply_to_all_sessions

        try:
            from comtypes import CLSCTX_ALL, CoInitialize  # type: ignore
            CoInitialize()

            from ctypes import POINTER, cast  # noqa: F401
            from pycaw.pycaw import AudioUtilities, IAudioEndpointVolume  # type: ignore

            self._AudioUtilities = AudioUtilities
            self._IAudioEndpointVolume = IAudioEndpointVolume
            self._CLSCTX_ALL = CLSCTX_ALL
            self._cast = cast
            self._POINTER = POINTER

            self._endpoint = None  # cached IAudioEndpointVolume
        except Exception as e:
            raise RuntimeError(
                "Windows audio backend not available. Install dependencies:\n"
                "  pip install pycaw comtypes\n"
                f"Original error: {e}"
            )

    def _get_endpoint(self):
        if self._endpoint is not None:
            return self._endpoint

        device = self._AudioUtilities.GetSpeakers()

        try:
            endpoint = device.EndpointVolume.QueryInterface(self._IAudioEndpointVolume)
        except Exception:
            interface = device.Activate(self._IAudioEndpointVolume._iid_, self._CLSCTX_ALL, None)
            endpoint = self._cast(interface, self._POINTER(self._IAudioEndpointVolume))

        self._endpoint = endpoint
        return endpoint

    def set_master(self, volume_0_100: int) -> None:
        volume_0_100 = max(0, min(100, int(volume_0_100)))
        vol = volume_0_100 / 100.0

        endpoint = self._get_endpoint()
        endpoint.SetMasterVolumeLevelScalar(vol, None)

    def set_app(self, process_name: str, volume_0_100: int) -> bool:
        volume_0_100 = max(0, min(100, int(volume_0_100)))
        vol = volume_0_100 / 100.0

        target = (process_name or "").lower()
        if not target:
            return False

        updated = 0
        sessions = self._AudioUtilities.GetAllSessions()
        for s in sessions:
            if s.Process is None:
                continue
            try:
                name = s.Process.name().lower()
            except Exception:
                continue

            if name != target:
                continue

            try:
                s.SimpleAudioVolume.SetMasterVolume(vol, None)
                updated += 1
                if not self.apply_to_all_sessions:
                    break
            except Exception:
                continue

        return updated > 0


class MacOSBackend(VolumeBackend):
    def __init__(self, spotify_app_name: str = "Spotify") -> None:
        self.spotify_app_name = spotify_app_name

    def _run_osascript(self, script: str) -> Tuple[int, str, str]:
        p = subprocess.run(
            ["osascript", "-e", script],
            capture_output=True,
            text=True,
        )
        return p.returncode, p.stdout.strip(), p.stderr.strip()

    def set_master(self, volume_0_100: int) -> None:
        volume_0_100 = max(0, min(100, int(volume_0_100)))
        rc, _, err = self._run_osascript(f"set volume output volume {volume_0_100}")
        if rc != 0:
            raise RuntimeError(f"macOS set volume failed: {err}")

    def set_app(self, process_name: str, volume_0_100: int) -> bool:
        _ = process_name, volume_0_100
        return False

    def set_spotify(self, volume_0_100: int) -> bool:
        volume_0_100 = max(0, min(100, int(volume_0_100)))
        rc, _, _ = self._run_osascript(
            f'tell application "{self.spotify_app_name}" to set sound volume to {volume_0_100}'
        )
        return rc == 0


def get_backend(cfg: Dict[str, Any]) -> VolumeBackend:
    if sys.platform.startswith("win"):
        apply_all = bool(cfg.get("windows", {}).get("apply_to_all_sessions", True))
        return WindowsBackend(apply_to_all_sessions=apply_all)
    if sys.platform == "darwin":
        spotify_name = str(cfg.get("macos", {}).get("spotify_app_name", "Spotify"))
        return MacOSBackend(spotify_app_name=spotify_name)
    raise RuntimeError("Unsupported OS. This script currently supports Windows and macOS only.")


# =========================
# Targets / games helpers
# =========================

def clamp01(x: int) -> int:
    return max(0, min(100, int(x)))


def _normalize_proc(name: str) -> str:
    return (name or "").strip()


def build_games_map(cfg: Dict[str, Any]) -> Dict[str, List[str]]:
    games_list = cfg.get("windows", {}).get("games", [])
    games: Dict[str, List[str]] = {}

    if isinstance(games_list, list):
        for item in games_list:
            if not isinstance(item, dict):
                continue
            gid = str(item.get("id", "")).strip()
            procs = item.get("processes", [])
            if not gid:
                continue
            if isinstance(procs, str):
                procs = [procs]
            if not isinstance(procs, list):
                continue
            cleaned = [_normalize_proc(str(p)) for p in procs if str(p).strip()]
            if cleaned:
                games[gid.lower()] = cleaned

    return games


def resolve_windows_processes(target: Dict[str, Any], games_map: Dict[str, List[str]]) -> List[str]:
    ttype = str(target.get("type", "")).lower()

    if ttype == "app":
        proc = _normalize_proc(str(target.get("process", "")))
        return [proc] if proc else []

    if ttype == "game":
        gid = str(target.get("game", "")).strip().lower()
        return games_map.get(gid, [])

    if ttype in ("apps", "processes"):
        procs = target.get("processes", [])
        if isinstance(procs, str):
            procs = [procs]
        if isinstance(procs, list):
            cleaned = [_normalize_proc(str(p)) for p in procs if str(p).strip()]
            return cleaned

    return []


# =========================
# UDP parsing & control loop
# =========================

PATTERN = re.compile(r"^E=([^|]+)\|B=(.+)$")


def main() -> None:
    cfg = load_config()
    backend = get_backend(cfg)

    listen_ip = cfg.get("udp", {}).get("listen_ip", "0.0.0.0")
    listen_port = int(cfg.get("udp", {}).get("listen_port", 4210))

    enc_cfg: List[Dict[str, Any]] = cfg.get("encoders", [])
    n = len(enc_cfg)

    games_map = build_games_map(cfg) if sys.platform.startswith("win") else {}

    last_counts: List[Optional[int]] = [None] * n
    volumes: List[int] = [50] * n
    last_line: Optional[str] = None

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind((listen_ip, listen_port))
    print(f"Listening UDP on {listen_ip}:{listen_port}")
    print("Format expected: E=v1,v2,v3,v4|B=b1,b2,b3,b4")

    while True:
        data, addr = sock.recvfrom(2048)
        msg = data.decode("utf-8", errors="replace").strip()

        m = PATTERN.match(msg)
        if not m:
            print(f"[{addr[0]}] {msg}")
            continue

        e_vals = m.group(1).split(",")
        b_vals = m.group(2).split(",")

        e_vals += ["0"] * max(0, n - len(e_vals))
        b_vals += ["0"] * max(0, n - len(b_vals))
        e_vals = e_vals[:n]
        b_vals = b_vals[:n]

        counts: List[int] = []
        buttons: List[int] = []
        for i in range(n):
            try:
                counts.append(int(e_vals[i]))
            except Exception:
                counts.append(0)
            try:
                buttons.append(int(b_vals[i]))
            except Exception:
                buttons.append(0)

        for i in range(n):
            step = int(enc_cfg[i].get("step", 2))
            prev = last_counts[i]
            cur = counts[i]
            if prev is None:
                last_counts[i] = cur
                continue

            delta = cur - prev
            if delta != 0:
                volumes[i] = clamp01(volumes[i] + delta * step)
                last_counts[i] = cur

                target = enc_cfg[i].get("windows") if sys.platform.startswith("win") else enc_cfg[i].get("macos")
                if not isinstance(target, dict):
                    target = {}
                ttype = str(target.get("type", "")).lower()

                if ttype in ("master", "system"):
                    try:
                        backend.set_master(volumes[i])
                    except Exception as e:
                        print(f"OS error (encoder {i+1} master): {e}")

                elif sys.platform.startswith("win") and ttype in ("app", "game", "apps", "processes"):
                    procs = resolve_windows_processes(target, games_map)
                    any_ok = False
                    for proc in procs:
                        try:
                            if backend.set_app(proc, volumes[i]):
                                any_ok = True
                        except Exception as e:
                            print(f"OS error (encoder {i+1} app {proc}): {e}")
                    if not any_ok and procs:
                        # Session not found; likely the game/app isn't producing audio yet.
                        pass

                elif ttype == "spotify":
                    try:
                        backend.set_spotify(volumes[i])
                    except Exception as e:
                        print(f"OS error (encoder {i+1} spotify): {e}")

            if buttons[i] == 1:
                volumes[i] = 0 if volumes[i] > 0 else 50

                target = enc_cfg[i].get("windows") if sys.platform.startswith("win") else enc_cfg[i].get("macos")
                if not isinstance(target, dict):
                    target = {}
                ttype = str(target.get("type", "")).lower()

                try:
                    if ttype in ("master", "system"):
                        backend.set_master(volumes[i])
                    elif sys.platform.startswith("win") and ttype in ("app", "game", "apps", "processes"):
                        procs = resolve_windows_processes(target, games_map)
                        for proc in procs:
                            backend.set_app(proc, volumes[i])
                    elif ttype == "spotify":
                        backend.set_spotify(volumes[i])
                except Exception as e:
                    print(f"OS error (encoder {i+1} button action): {e}")

        parts_e = []
        parts_b = []
        for i in range(n):
            parts_e.append(f"E{i+1}={counts[i]:>6}({volumes[i]:>3}%)")
            parts_b.append(f"B{i+1}={buttons[i]}")
        line = "  ".join(parts_e) + "   |   " + " ".join(parts_b)

        if line != last_line:
            ts = datetime.now().strftime("%H:%M:%S")
            print(f"{ts}  {addr[0]}  {line}")
            last_line = line


if __name__ == "__main__":
    main()