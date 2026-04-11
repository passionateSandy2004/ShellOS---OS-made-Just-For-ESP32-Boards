#!/usr/bin/env python3
"""
ShellOS Imager — flash ShellOS to ESP32-CAM (or compatible ESP32 @ 4MB) over USB.
Requires: PyQt6, pyserial, esptool (see requirements.txt)
"""

from __future__ import annotations

import codecs
import json
import re
import socket
from functools import partial
import sys
import time
import threading
import urllib.request
import urllib.error
from pathlib import Path


from PyQt6.QtCore import QProcess, QSettings, Qt, QThread, QTimer, pyqtSignal
from PyQt6.QtGui import QColor, QFont, QFontInfo, QFontMetrics, QTextCharFormat, QTextCursor
from PyQt6.QtWidgets import (
    QApplication,
    QComboBox,
    QDialog,
    QFileDialog,
    QFrame,
    QHBoxLayout,
    QHeaderView,
    QLabel,
    QLineEdit,
    QMessageBox,
    QProgressBar,
    QPushButton,
    QScrollArea,
    QSizePolicy,
    QTabWidget,
    QTableWidget,
    QTableWidgetItem,
    QTextEdit,
    QVBoxLayout,
    QWidget,
)

try:
    from serial.tools import list_ports
except ImportError:
    list_ports = None


APP_NAME = "ShellOS Imager"
APP_VERSION = "1.0"

# Layout: generous spacing (Raspberry Pi Imager–style)
UI_WIN_MIN_W  = 880
UI_WIN_MIN_H  = 760
UI_WIN_DEFAULT_W = 1020
UI_WIN_DEFAULT_H = 900
UI_MARGIN_OUTER = 28
UI_MARGIN_TAB = 26
UI_SPACING_MAJOR = 24
UI_SPACING_BLOCK = 18
UI_FORM_LABEL_W = 168

class FlashTarget:
    def __init__(
        self,
        *,
        id: str,
        label: str,
        chip: str,
        flash_mode: str,
        flash_freq: str,
        flash_size: str,
        layout: list[tuple[int, str]],
        fw_subdir: str,
    ) -> None:
        self.id = id
        self.label = label
        self.chip = chip
        self.flash_mode = flash_mode
        self.flash_freq = flash_freq
        self.flash_size = flash_size
        self.layout = layout
        self.fw_subdir = fw_subdir


# Must match build/flash_args in the ShellOS ESP-IDF project.
# Each board target has its own firmware folder under imager/firmware/<target>/.
FLASH_TARGETS: dict[str, FlashTarget] = {
    "esp32-cam": FlashTarget(
        id="esp32-cam",
        label="ESP32-CAM",
        chip="esp32",
        flash_mode="dio",
        flash_freq="80m",
        flash_size="4MB",
        layout=[
            (0x1000, "bootloader.bin"),
            (0x8000, "partition-table.bin"),
            (0x10000, "esp32_shell_os.bin"),
        ],
        fw_subdir="esp32-cam",
    ),
    "esp32-c6": FlashTarget(
        id="esp32-c6",
        label="ESP32-C6",
        chip="esp32c6",
        flash_mode="dio",
        flash_freq="80m",
        flash_size="4MB",
        layout=[
            (0x0,     "bootloader.bin"),      # ESP32-C6 bootloader lives at 0x0 (not 0x1000)
            (0x8000,  "partition-table.bin"),
            (0x10000, "esp32_shell_os.bin"),
        ],
        fw_subdir="esp32-c6",
    ),
}

DEFAULT_FLASH_TARGET_ID = "esp32-cam"
DEFAULT_BAUD = "460800"
# Same as SHELL_UART_BAUD in firmware (components/drivers/uart_driver.h)
MONITOR_BAUD = 115200
# Package HTTP upload server port (matches http_upload.h HTTP_UPLOAD_PORT)
HTTP_UPLOAD_PORT = 8080
# TCP shell port (netsh) — PuTTY RAW; imager uses HTTP on HTTP_UPLOAD_PORT instead
SHELL_TCP_PORT = 2323
TCP_PROBE_TIMEOUT_S = 3.0
# Banner + box lines + ESP-IDF logs need a wide terminal (see kernel_print_banner).
SERIAL_MONITOR_MIN_COLUMNS = 122
SERIAL_MONITOR_OPEN_COLUMNS = 132


def _tcp_port_open(host: str, port: int, timeout: float) -> tuple[bool, str]:
    """
    Return (True, "") if a TCP connection to host:port succeeds, else (False, reason).
    Used to verify the Package API port before HTTP (no Qt calls — safe from any thread).
    """
    try:
        with socket.create_connection((host, port), timeout=timeout):
            pass
        return True, ""
    except socket.timeout:
        return False, f"TCP {port} timed out (host unreachable or firewall blocking this app)"
    except OSError as e:
        msg = str(e).lower()
        if "refused" in msg or getattr(e, "winerror", None) == 10061:
            return False, f"TCP {port} refused — is ShellOS running and WiFi connected?"
        return False, f"TCP {port}: {e}"


def _xterm256_hex(code: int) -> str:
    """Map 256-color index to #rrggbb (matches common terminals / shell_theme.h)."""
    code = max(0, min(255, code))
    if code < 8:
        rgb = (
            (0, 0, 0),
            (205, 0, 0),
            (0, 205, 0),
            (205, 205, 0),
            (0, 0, 238),
            (205, 0, 205),
            (0, 205, 205),
            (229, 229, 229),
        )[code]
    elif code < 16:
        rgb = (
            (127, 127, 127),
            (255, 0, 0),
            (0, 255, 0),
            (255, 255, 0),
            (92, 92, 255),
            (255, 0, 255),
            (0, 255, 255),
            (255, 255, 255),
        )[code - 8]
    elif code < 232:
        code -= 16
        b, code = code % 6, code // 6
        g, r = code % 6, code // 6

        def ch(x: int) -> int:
            return 0 if x == 0 else 55 + 40 * (x - 1)

        rgb = (ch(r), ch(g), ch(b))
    else:
        g = 8 + (code - 232) * 10
        rgb = (g, g, g)
    return f"#{rgb[0]:02x}{rgb[1]:02x}{rgb[2]:02x}"


XTERM256_HEX = [_xterm256_hex(i) for i in range(256)]

# Standard + bright foregrounds (ShellOS uses 90–97 and 38;5;n — see shell_theme.h)
_ANSI_FG = {
    30: "#555555",
    31: "#b91c1c",
    32: "#15803d",
    33: "#a16207",
    34: "#1d4ed8",
    35: "#a21caf",
    36: "#0e7490",
    37: "#d4d4d4",
}
_ANSI_BRIGHT = {
    0: "#555555",
    1: "#f87171",
    2: "#4ade80",
    3: "#facc15",
    4: "#60a5fa",
    5: "#e879f9",
    6: "#22d3ee",
    7: "#ffffff",
}
_ANSI_BG = {
    40: "#000000",
    41: "#7f1d1d",
    42: "#14532d",
    43: "#713f12",
    44: "#1e3a8a",
    45: "#701a75",
    46: "#164e63",
    47: "#525252",
}

_DEFAULT_MONITOR_FG = "#e5e5e5"
_DEFAULT_MONITOR_BG = "#000000"


class AnsiParser:
    """
    Incremental ANSI (CSI SGR, clear-screen) → list[(text, QTextCharFormat)].
    Uses QTextCharFormat so Qt renders every character with the same document font —
    no HTML font-family mismatches, no per-glyph width differences.
    """

    def __init__(self) -> None:
        self._decoder = codecs.getincrementaldecoder("utf-8")(errors="replace")
        self._pending = ""
        self.fg: str = _DEFAULT_MONITOR_FG
        self.bg: str | None = None
        self.bold: bool = False
        self.dim: bool = False
        self.italic: bool = False

    def reset_styles(self) -> None:
        self.fg = _DEFAULT_MONITOR_FG
        self.bg = None
        self.bold = False
        self.dim = False
        self.italic = False

    def full_reset(self) -> None:
        self._decoder = codecs.getincrementaldecoder("utf-8")(errors="replace")
        self._pending = ""
        self.reset_styles()

    def _make_fmt(self) -> QTextCharFormat:
        fmt = QTextCharFormat()
        fg = QColor(self.fg)
        if self.dim:
            fg.setAlphaF(0.65)
        fmt.setForeground(fg)
        if self.bg:
            fmt.setBackground(QColor(self.bg))
        if self.bold:
            fmt.setFontWeight(700)
        if self.italic:
            fmt.setFontItalic(True)
        return fmt

    def _parse_csi_params(self, inner: str) -> list[int]:
        if not inner:
            return [0]
        out: list[int] = []
        for p in inner.split(";"):
            p = p.strip()
            if p == "":
                out.append(0)
            elif p.isdigit():
                out.append(int(p))
            else:
                digits = "".join(c for c in p if c.isdigit())
                out.append(int(digits) if digits else 0)
        return out

    def _apply_sgr(self, inner: str) -> None:
        nums = self._parse_csi_params(inner)
        i = 0
        while i < len(nums):
            n = nums[i]
            if n == 0:
                self.reset_styles()
            elif n == 1:
                self.bold = True
            elif n == 2:
                self.dim = True
            elif n == 22:
                self.bold = False
                self.dim = False
            elif n == 3:
                self.italic = True
            elif n == 23:
                self.italic = False
            elif 30 <= n <= 37:
                self.fg = _ANSI_FG[n]
            elif n == 39:
                self.fg = _DEFAULT_MONITOR_FG
            elif 40 <= n <= 47:
                self.bg = _ANSI_BG[n]
            elif n == 49:
                self.bg = None
            elif 90 <= n <= 97:
                self.fg = _ANSI_BRIGHT[n - 90]
            elif n == 38 and i + 2 < len(nums) and nums[i + 1] == 5:
                self.fg = XTERM256_HEX[max(0, min(255, nums[i + 2]))]
                i += 2
            elif n == 38 and i + 4 < len(nums) and nums[i + 1] == 2:
                r, g, b = nums[i + 2], nums[i + 3], nums[i + 4]
                self.fg = f"#{max(0, min(255, r)):02x}{max(0, min(255, g)):02x}{max(0, min(255, b)):02x}"
                i += 4
            elif n == 48 and i + 2 < len(nums) and nums[i + 1] == 5:
                self.bg = XTERM256_HEX[max(0, min(255, nums[i + 2]))]
                i += 2
            elif n == 48 and i + 4 < len(nums) and nums[i + 1] == 2:
                r, g, b = nums[i + 2], nums[i + 3], nums[i + 4]
                self.bg = f"#{max(0, min(255, r)):02x}{max(0, min(255, g)):02x}{max(0, min(255, b)):02x}"
                i += 4
            i += 1

    def feed(self, data: bytes) -> tuple[list[tuple[str, QTextCharFormat]], bool]:
        """Decode bytes, parse ANSI SGR; return (segments, clear_screen)."""
        text = self._decoder.decode(data, False)
        buf = self._pending + text
        self._pending = ""
        segments: list[tuple[str, QTextCharFormat]] = []
        clear = False
        pos = 0
        while pos < len(buf):
            esc = buf.find("\x1b", pos)
            if esc < 0:
                seg = buf[pos:].replace("\r\n", "\n").replace("\r", "\n")
                if seg:
                    segments.append((seg, self._make_fmt()))
                break
            if esc > pos:
                seg = buf[pos:esc].replace("\r\n", "\n").replace("\r", "\n")
                if seg:
                    segments.append((seg, self._make_fmt()))
            if esc + 1 >= len(buf):
                self._pending = buf[esc:]
                break
            ch1 = buf[esc + 1]
            if ch1 == "[":
                i = esc + 2
                while i < len(buf) and not (0x40 <= ord(buf[i]) <= 0x7E):
                    i += 1
                if i >= len(buf):
                    self._pending = buf[esc:]
                    break
                final = buf[i]
                inner = buf[esc + 2 : i]
                pos = i + 1
                if final == "m":
                    self._apply_sgr(inner)
                elif final == "J":
                    ps = self._parse_csi_params(inner)
                    p = ps[0] if ps else 0
                    if p == 2 or p == 3:
                        clear = True
                        segments.clear()
                        self.reset_styles()
                continue
            if ch1 == "]":
                bel = buf.find("\x07", esc + 2)
                st = buf.find("\x1b\\", esc + 2)
                if bel >= 0 and (st < 0 or bel <= st):
                    pos = bel + 1
                elif st >= 0:
                    pos = st + 2
                else:
                    self._pending = buf[esc:]
                    break
                continue
            if ch1 in "()":
                pos = esc + 3 if esc + 2 < len(buf) else len(buf)
                if esc + 2 >= len(buf):
                    self._pending = buf[esc:]
                continue
            pos = esc + 1
        return (segments, clear)


def is_release_bundle() -> bool:
    """True when running as a PyInstaller build (firmware is bundled under _MEIPASS)."""
    return bool(getattr(sys, "frozen", False) and hasattr(sys, "_MEIPASS"))


def firmware_base_dir() -> Path:
    """Directory containing bootloader.bin, partition-table.bin, esp32_shell_os.bin."""
    if is_release_bundle():
        return Path(sys._MEIPASS) / "firmware"
    return Path(__file__).resolve().parent / "firmware"


def find_python() -> str:
    return sys.executable


def app_ui_font() -> QFont:
    """Default UI font: avoids legacy bitmap faces (Fixedsys, 8514oem) that spam DirectWrite errors on Windows."""
    if sys.platform == "win32":
        return QFont("Segoe UI", 11)
    if sys.platform == "darwin":
        return QFont(".AppleSystemUIFont", 13)
    return QFont("Sans Serif", 10)


def app_mono_font(point_size: int = 10) -> QFont:
    """Pick first installed outline monospace font (call after QApplication exists)."""
    if sys.platform == "darwin":
        return QFont("Menlo", max(point_size, 11))
    for name in ("Cascadia Code", "Cascadia Mono", "Consolas", "Lucida Console", "Courier New"):
        f = QFont(name, point_size)
        f.setStyleHint(QFont.StyleHint.Monospace)
        f.setFixedPitch(True)
        if QFontInfo(f).exactMatch():
            return f
    f = QFont("Courier New", point_size)
    f.setStyleHint(QFont.StyleHint.Monospace)
    f.setFixedPitch(True)
    return f


class _SerialMonitorThread(QThread):
    """Background read loop; writes run on the GUI thread via write_bytes (short writes)."""

    chunk = pyqtSignal(bytes)
    failed = pyqtSignal(str)
    opened = pyqtSignal()

    def __init__(self, port: str, baud: int) -> None:
        super().__init__()
        self._port = port
        self._baud = baud
        self._stop = False
        self._ser = None

    def run(self) -> None:
        import serial

        try:
            self._ser = serial.Serial(
                self._port,
                self._baud,
                timeout=0.12,
                write_timeout=1.0,
            )
            self._ser.reset_input_buffer()
            # Many ESP32 USB bridges: pulse RTS/DTR to exit bootloader and run app (like idf monitor)
            try:
                self._ser.dtr = False
                self._ser.rts = True
                time.sleep(0.05)
                self._ser.rts = False
            except (AttributeError, OSError):
                pass
            self.opened.emit()
            while not self._stop:
                try:
                    n = self._ser.in_waiting
                    if n:
                        raw = self._ser.read(n)
                        self.chunk.emit(raw)
                except OSError:
                    break
                self.msleep(12)
        except Exception as e:
            self.failed.emit(str(e))
        finally:
            if self._ser is not None:
                try:
                    if self._ser.is_open:
                        self._ser.close()
                except OSError:
                    pass
                self._ser = None

    def stop(self) -> None:
        self._stop = True

    def write_bytes(self, data: bytes) -> bool:
        if self._stop or self._ser is None or not self._ser.is_open:
            return False
        try:
            self._ser.write(data)
            return True
        except OSError:
            return False


class SerialMonitorDialog(QDialog):
    """Built-in serial monitor (same role as `idf.py monitor` for this port)."""

    def __init__(
        self,
        port: str,
        baud: int,
        parent: QWidget | None = None,
        *,
        show_wifi_setup_hint: bool = False,
    ) -> None:
        super().__init__(parent)
        self._port = port
        self._baud = baud
        self._thread: _SerialMonitorThread | None = None
        self._did_place_window = False
        self._ansi = AnsiParser()

        self.setWindowTitle(f"Serial monitor — {port} @ {baud}")
        self.setAttribute(Qt.WidgetAttribute.WA_DeleteOnClose, True)

        layout = QVBoxLayout(self)
        layout.setContentsMargins(20, 18, 20, 18)
        layout.setSpacing(14)
        hint = QLabel(
            f"USB serial @ {baud} baud — type shell commands below and press Enter or click Send."
            + (
                " After a fresh flash, use the Wi‑Fi box under this line first."
                if show_wifi_setup_hint
                else ""
            )
        )
        hint.setObjectName("serialTopHint")
        hint.setWordWrap(True)
        layout.addWidget(hint)

        if show_wifi_setup_hint:
            tip = QFrame()
            tip.setObjectName("serialWifiTip")
            tl = QVBoxLayout(tip)
            tl.setContentsMargins(14, 12, 14, 12)
            tl.setSpacing(8)
            tip_title = QLabel("Wi‑Fi setup (first boot)")
            tip_title.setObjectName("serialWifiTipTitle")
            tip_body = QLabel(
                "<p style='margin:0 0 8px 0;'>Connect ShellOS to your router so you can use the "
                "<b>Packages</b> tab and TCP shell over the network.</p>"
                "<p style='margin:0 0 6px 0;'><b>Type these in the command box at the bottom</b> "
                "(replace with your real SSID and password):</p>"
                "<p style='margin:0; font-family: Consolas, monospace; font-size: 12px; line-height: 1.5;'>"
                "<span style='color:#0d9488'>wifi scan</span><br>"
                "<span style='color:#0d9488'>wifi connect</span> <i>YourSSID</i> <i>YourPassword</i><br>"
                "<span style='color:#0d9488'>wifi status</span>"
                "</p>"
                "<p style='margin:10px 0 0 0; color:#64748b; font-size:12px;'>"
                "If you see a connection error: use 2.4&nbsp;GHz Wi‑Fi, check the password, or edit "
                "<code>config/wifi.cfg</code> on the device (LittleFS)."
                "</p>"
                "<p style='margin:10px 0 0 0; color:#334155; font-size:12px;'>"
                "After Wi‑Fi works: remote shell on TCP port <b>2323</b> (PuTTY <i>Raw</i>); "
                "package manager HTTP on port <b>8080</b>."
                "</p>"
            )
            tip_body.setTextFormat(Qt.TextFormat.RichText)
            tip_body.setWordWrap(True)
            tip_body.setOpenExternalLinks(False)
            tl.addWidget(tip_title)
            tl.addWidget(tip_body)
            layout.addWidget(tip)

        self.out = QTextEdit()
        self.out.setReadOnly(True)
        self.out.setAcceptRichText(False)
        self.out.setUndoRedoEnabled(False)
        self.out.setLineWrapMode(QTextEdit.LineWrapMode.NoWrap)
        self.out.setHorizontalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAsNeeded)
        mono = app_mono_font(11)
        doc = self.out.document()
        doc.setDefaultFont(mono)
        doc.setDocumentMargin(8)
        # Zero paragraph spacing so newlines stay at line-height only (true terminal feel)
        from PyQt6.QtGui import QTextBlockFormat
        blk_fmt = QTextBlockFormat()
        blk_fmt.setLineHeight(100, 1)
        blk_fmt.setTopMargin(0)
        blk_fmt.setBottomMargin(0)
        cur0 = self.out.textCursor()
        cur0.select(QTextCursor.SelectionType.Document)
        cur0.setBlockFormat(blk_fmt)
        self.out.setTextCursor(cur0)
        self._blk_fmt = blk_fmt
        self.out.setFont(mono)
        # Match ShellOS banner width (~120+ columns) and typical ESP-IDF log lines.
        fm = QFontMetrics(mono)
        col_px = max(
            1,
            fm.horizontalAdvance("0"),
            fm.horizontalAdvance("W"),
            fm.horizontalAdvance("█"),
            fm.horizontalAdvance("─"),
            fm.horizontalAdvance("╝"),
        )
        inner_w = int(col_px * SERIAL_MONITOR_MIN_COLUMNS + 2 * doc.documentMargin() + 28)
        open_w = int(col_px * SERIAL_MONITOR_OPEN_COLUMNS + 2 * doc.documentMargin() + 48)
        self.out.setMinimumWidth(inner_w)
        self.out.setMinimumHeight(360)
        self.setMinimumWidth(inner_w + 24)
        self.setMinimumHeight(520)
        self.resize(min(open_w + 48, 1400), 720)
        self.out.setStyleSheet(
            "QTextEdit { background-color: #000000; color: #e5e5e5; "
            "border: 1px solid #334155; border-radius: 8px; selection-background-color: #1e3a5f; }"
        )
        layout.addWidget(self.out, stretch=1)

        row = QHBoxLayout()
        self.input = QLineEdit()
        self.input.setPlaceholderText("Shell command…  (e.g. wifi scan, help)")
        self.input.returnPressed.connect(self._send_line)
        row.addWidget(self.input, stretch=1)
        btn_send = QPushButton("Send")
        btn_send.clicked.connect(self._send_line)
        row.addWidget(btn_send)
        btn_close = QPushButton("Close")
        btn_close.clicked.connect(self.close)
        row.addWidget(btn_close)
        layout.addLayout(row)

        self._apply_dialog_style()

    def _apply_dialog_style(self) -> None:
        self.setStyleSheet(
            """
            QDialog { background-color: #f8fafc; color: #0f172a; }
            QLabel { color: #475569; font-size: 13px; }
            QLabel#serialTopHint { color: #64748b; font-size: 13px; }
            QFrame#serialWifiTip {
                background-color: #ecfdf5;
                border: 1px solid #6ee7b7;
                border-radius: 10px;
            }
            QLabel#serialWifiTipTitle {
                color: #065f46;
                font-size: 14px;
                font-weight: 700;
            }
            QLineEdit {
                background-color: #ffffff;
                color: #0f172a;
                border: 1px solid #cbd5e1;
                border-radius: 8px;
                padding: 8px 12px;
                font-size: 13px;
            }
            QLineEdit:focus { border: 2px solid #14b8a6; }
            QPushButton {
                background-color: #ffffff;
                border: 1px solid #cbd5e1;
                border-radius: 8px;
                padding: 8px 16px;
                min-height: 28px;
                font-weight: 600;
                color: #334155;
            }
            QPushButton:hover { border-color: #14b8a6; color: #0f766e; }
            """
        )

    def showEvent(self, event) -> None:
        super().showEvent(event)
        if not self._did_place_window:
            self._did_place_window = True
            screen = self.screen()
            if screen is not None:
                avail = screen.availableGeometry()
                cap_w = max(360, avail.width() - 24)
                cap_h = max(280, avail.height() - 24)
                w = min(max(self.width(), self.minimumWidth()), cap_w)
                h = min(max(self.height(), self.minimumHeight()), cap_h)
                self.resize(w, h)
                fr = self.frameGeometry()
                x = avail.left() + max(0, (avail.width() - fr.width()) // 2)
                y = avail.top() + max(0, (avail.height() - fr.height()) // 2)
                self.move(x, y)
        if self._thread is None:
            self._start_thread()

    def _start_thread(self) -> None:
        self._thread = _SerialMonitorThread(self._port, self._baud)
        self._thread.chunk.connect(self._on_chunk)
        self._thread.failed.connect(self._on_failed)
        self._thread.opened.connect(self._on_opened)
        self._thread.start()

    def _insert_text(self, text: str, fmt: QTextCharFormat) -> None:
        cur = self.out.textCursor()
        cur.movePosition(QTextCursor.MoveOperation.End)
        cur.setBlockFormat(self._blk_fmt)
        cur.insertText(text, fmt)
        self.out.setTextCursor(cur)

    def _on_opened(self) -> None:
        fmt = QTextCharFormat()
        fmt.setForeground(QColor("#64748b"))
        self._insert_text(f"[opened {self._port} @ {self._baud}]\n", fmt)
        self.input.setFocus(Qt.FocusReason.OtherFocusReason)

    def _on_chunk(self, data: bytes) -> None:
        segments, clear = self._ansi.feed(data)
        if clear:
            self.out.clear()
        if segments:
            cur = self.out.textCursor()
            cur.movePosition(QTextCursor.MoveOperation.End)
            cur.beginEditBlock()
            for text, fmt in segments:
                cur.setBlockFormat(self._blk_fmt)
                cur.insertText(text, fmt)
            cur.endEditBlock()
            self.out.setTextCursor(cur)
            self.out.ensureCursorVisible()
        if self.out.document().characterCount() > 200_000:
            self.out.clear()
            self._ansi.full_reset()

    def _on_failed(self, msg: str) -> None:
        fmt = QTextCharFormat()
        fmt.setForeground(QColor("#f87171"))
        self._insert_text(f"\n{msg}\n", fmt)
        QMessageBox.warning(self, APP_NAME, f"Could not open serial port:\n{msg}")

    def _send_line(self) -> None:
        text = self.input.text()
        self.input.clear()
        if not text or self._thread is None:
            return
        line = text.rstrip("\r\n") + "\r\n"
        if not self._thread.write_bytes(line.encode("utf-8", errors="replace")):
            fmt = QTextCharFormat()
            fmt.setForeground(QColor("#f87171"))
            self._insert_text("\n[not connected — cannot send]\n", fmt)

    def closeEvent(self, event) -> None:
        if self._thread is not None:
            self._thread.stop()
            self._thread.wait(3000)
            self._thread = None
        super().closeEvent(event)


class ShellOSImager(QWidget):
    """
    Worker threads must not use QTimer.singleShot — that timer lives on a thread
    with no event loop, so callbacks never run. Use _invoke_main.emit(callable)
    to run code on the GUI thread instead.
    """

    _invoke_main = pyqtSignal(object)

    def __init__(self) -> None:
        super().__init__()
        self._invoke_main.connect(self._on_invoke_main)
        self._release = is_release_bundle()
        self._settings = QSettings("ShellOS", "Imager")
        saved_target = str(self._settings.value("flash/target", DEFAULT_FLASH_TARGET_ID) or "")
        if saved_target not in FLASH_TARGETS:
            saved_target = DEFAULT_FLASH_TARGET_ID
        self._flash_target_id = saved_target
        self._fw_dir = self._default_fw_dir_for_target(self._flash_target_id)
        self._process: QProcess | None = None
        self._monitor_dialog: SerialMonitorDialog | None = None
        self._last_flash_port: str | None = None
        self._build_ui()
        self._apply_style()
        self.refresh_ports()
        self._update_firmware_status()
        QTimer.singleShot(500, self._update_firmware_status)

    def _on_invoke_main(self, fn: object) -> None:
        if callable(fn):
            fn()

    def _form_label(self, text: str, align_top: bool = False) -> QLabel:
        """Fixed-width right-aligned label so controls line up in a column."""
        lab = QLabel(text)
        lab.setObjectName("formLabel")
        lab.setMinimumWidth(UI_FORM_LABEL_W)
        lab.setMaximumWidth(UI_FORM_LABEL_W)
        if align_top:
            lab.setAlignment(Qt.AlignmentFlag.AlignRight | Qt.AlignmentFlag.AlignTop)
            lab.setContentsMargins(0, 6, 8, 0)
        else:
            lab.setAlignment(Qt.AlignmentFlag.AlignRight | Qt.AlignmentFlag.AlignVCenter)
            lab.setContentsMargins(0, 0, 8, 0)
        return lab

    def _active_flash_target(self) -> FlashTarget:
        return FLASH_TARGETS.get(self._flash_target_id, FLASH_TARGETS[DEFAULT_FLASH_TARGET_ID])

    def _default_fw_dir_for_target(self, target_id: str) -> Path:
        t = FLASH_TARGETS.get(target_id, FLASH_TARGETS[DEFAULT_FLASH_TARGET_ID])
        return firmware_base_dir() / t.fw_subdir

    def _firmware_dir_candidates(self) -> list[Path]:
        """
        Directories to search for .bin files, in priority order.
        Supports legacy layout: bins directly under imager/firmware/ (ESP32-CAM only).
        """
        t = self._active_flash_target()
        base = firmware_base_dir()
        out: list[Path] = []
        seen: set[str] = set()

        def add(p: Path) -> None:
            key = str(p.resolve())
            if key not in seen:
                seen.add(key)
                out.append(p)

        add(self._fw_dir)
        add(base / t.fw_subdir)
        if t.id == "esp32-cam":
            add(base)
        return out

    def _resolved_fw_dir(self) -> Path | None:
        """First candidate directory that contains all required .bin files, or None."""
        t = self._active_flash_target()
        for d in self._firmware_dir_candidates():
            if all((d / name).is_file() for _, name in t.layout):
                return d
        return None

    def _set_flash_target(self, target_id: str) -> None:
        if target_id not in FLASH_TARGETS:
            target_id = DEFAULT_FLASH_TARGET_ID
        if target_id == self._flash_target_id:
            return
        self._flash_target_id = target_id
        self._settings.setValue("flash/target", target_id)
        saved = str(self._settings.value(f"flash/fw_dir/{target_id}", "") or "")
        self._fw_dir = Path(saved) if saved else self._default_fw_dir_for_target(target_id)
        if getattr(self, "lbl_fw_path", None) is not None:
            self._sync_fw_path_label()
        self._update_firmware_status()

    def _build_ui(self) -> None:
        self.setWindowTitle(f"{APP_NAME}  ·  v{APP_VERSION}")
        self.setMinimumSize(UI_WIN_MIN_W, UI_WIN_MIN_H)
        self.resize(UI_WIN_DEFAULT_W, UI_WIN_DEFAULT_H)

        root = QVBoxLayout(self)
        root.setSpacing(UI_SPACING_MAJOR)
        root.setContentsMargins(UI_MARGIN_OUTER, UI_MARGIN_OUTER, UI_MARGIN_OUTER, UI_MARGIN_OUTER)

        header = QFrame()
        header.setObjectName("headerCard")
        head_lay = QVBoxLayout(header)
        head_lay.setContentsMargins(22, 20, 22, 20)
        head_lay.setSpacing(8)
        title = QLabel("ShellOS")
        title.setObjectName("title")
        subtitle = QLabel(
            "Plug in your board, choose the port, then flash — firmware is included."
            if self._release
            else "Flash firmware or manage Wi‑Fi packages on your ESP32 boards — clean layout, ShellOS teal accents."
        )
        subtitle.setObjectName("subtitle")
        subtitle.setWordWrap(True)
        head_lay.addWidget(title)
        head_lay.addWidget(subtitle)
        root.addWidget(header)

        sep = QFrame()
        sep.setFrameShape(QFrame.Shape.HLine)
        sep.setObjectName("sep")
        root.addWidget(sep)

        self._tabs = QTabWidget()
        self._tabs.setObjectName("mainTabs")
        root.addWidget(self._tabs, stretch=1)

        self._tabs.addTab(self._build_flash_tab(), "  Flash  ")
        self._tabs.addTab(self._build_package_tab(), "  Packages  ")

    # ── Flash tab ────────────────────────────────────────────────────────────

    def _flash_field_label(self, text: str) -> QLabel:
        """Compact left label for flash rows (avoid wide form labels + grid column bugs)."""
        w = QLabel(text)
        w.setObjectName("flashFieldLabel")
        w.setFixedWidth(118)
        w.setAlignment(Qt.AlignmentFlag.AlignLeft | Qt.AlignmentFlag.AlignVCenter)
        return w

    def _build_flash_tab(self) -> QWidget:
        """
        Flash UI: one scroll area wraps the whole tab (setup, status, log, button, hints)
        so sections never overlap and short windows scroll instead of clipping.
        """
        outer = QWidget()
        outer_lay = QVBoxLayout(outer)
        outer_lay.setContentsMargins(0, 4, 0, 8)
        outer_lay.setSpacing(0)

        scroll = QScrollArea()
        scroll.setObjectName("flashTabScroll")
        scroll.setWidgetResizable(True)
        scroll.setFrameShape(QFrame.Shape.NoFrame)
        scroll.setHorizontalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAlwaysOff)
        scroll.setVerticalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAsNeeded)
        scroll.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Expanding)

        inner = QWidget()
        layout = QVBoxLayout(inner)
        layout.setSpacing(UI_SPACING_MAJOR)
        layout.setContentsMargins(4, 8, 10, 24)
        scroll.setWidget(inner)
        outer_lay.addWidget(scroll, stretch=1)

        ctrl_h = 42

        # ── Setup card ───────────────────────────────────────────────────────
        setup = QFrame()
        setup.setObjectName("flashSetupCard")
        s = QVBoxLayout(setup)
        s.setContentsMargins(24, 22, 24, 22)
        s.setSpacing(16)

        head = QLabel("Flash ShellOS")
        head.setObjectName("flashCardTitle")
        s.addWidget(head)

        sub = QLabel("Select your board, USB port, then flash. Firmware is checked automatically.")
        sub.setObjectName("flashCardSubtitle")
        sub.setWordWrap(True)
        s.addWidget(sub)

        # Board row: full width combo
        row_b = QHBoxLayout()
        row_b.setSpacing(14)
        row_b.addWidget(self._flash_field_label("Board"))
        self.combo_board = QComboBox()
        self.combo_board.setMinimumHeight(ctrl_h)
        self.combo_board.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Fixed)
        for tid, t in FLASH_TARGETS.items():
            self.combo_board.addItem(t.label, userData=tid)
        idx = self.combo_board.findData(self._flash_target_id)
        if idx >= 0:
            self.combo_board.setCurrentIndex(idx)
        self.combo_board.currentIndexChanged.connect(
            lambda _=None: self._set_flash_target(str(self.combo_board.currentData() or DEFAULT_FLASH_TARGET_ID))
        )
        row_b.addWidget(self.combo_board, stretch=1)
        s.addLayout(row_b)

        # Port row: combo stretches; buttons fixed on the right
        row_p = QHBoxLayout()
        row_p.setSpacing(14)
        row_p.addWidget(self._flash_field_label("USB port"))
        self.combo_port = QComboBox()
        self.combo_port.setMinimumHeight(ctrl_h)
        self.combo_port.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Fixed)
        self.combo_port.currentIndexChanged.connect(lambda _: self._update_firmware_status())
        self.combo_port.currentIndexChanged.connect(
            lambda _: self._settings.setValue("flash/port", self.combo_port.currentData() or "")
        )
        row_p.addWidget(self.combo_port, stretch=1)

        btn_refresh = QPushButton("Refresh ports")
        btn_refresh.setMinimumHeight(ctrl_h)
        btn_refresh.setMinimumWidth(118)
        btn_refresh.clicked.connect(self.refresh_ports)
        row_p.addWidget(btn_refresh)

        btn_monitor = QPushButton("Serial monitor")
        btn_monitor.setMinimumHeight(ctrl_h)
        btn_monitor.setMinimumWidth(128)
        btn_monitor.setToolTip(f"Open serial terminal @ {MONITOR_BAUD} baud (same as idf.py monitor)")
        btn_monitor.clicked.connect(self._on_monitor_clicked)
        row_p.addWidget(btn_monitor)
        s.addLayout(row_p)

        self.lbl_fw_path: QLabel | None = None
        if not self._release:
            fw_title = QLabel("Firmware files (developer)")
            fw_title.setObjectName("flashSubheading")
            s.addWidget(fw_title)

            self.lbl_fw_path = QLabel()
            self.lbl_fw_path.setObjectName("pathLabel")
            self.lbl_fw_path.setWordWrap(True)
            self.lbl_fw_path.setTextInteractionFlags(Qt.TextInteractionFlag.TextSelectableByMouse)
            self.lbl_fw_path.setMinimumHeight(36)
            self.lbl_fw_path.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Preferred)
            self.lbl_fw_path.setAlignment(Qt.AlignmentFlag.AlignLeft | Qt.AlignmentFlag.AlignTop)
            s.addWidget(self.lbl_fw_path)

            row_fw = QHBoxLayout()
            row_fw.addStretch(1)
            btn_browse = QPushButton("Browse folder…")
            btn_browse.setMinimumHeight(ctrl_h)
            btn_browse.setMinimumWidth(140)
            btn_browse.clicked.connect(self._browse_firmware)
            row_fw.addWidget(btn_browse)
            s.addLayout(row_fw)

        layout.addWidget(setup)

        # ── Status strip ─────────────────────────────────────────────────────
        status_wrap = QFrame()
        status_wrap.setObjectName("flashStatusStrip")
        sw = QVBoxLayout(status_wrap)
        sw.setContentsMargins(14, 12, 14, 12)
        self.lbl_fw_status = QLabel()
        self.lbl_fw_status.setObjectName("flashStatusText")
        self.lbl_fw_status.setWordWrap(True)
        self.lbl_fw_status.setTextInteractionFlags(Qt.TextInteractionFlag.TextSelectableByMouse)
        sw.addWidget(self.lbl_fw_status)
        layout.addWidget(status_wrap)

        # ── Log (inside same scroll stack; stretch absorbs extra viewport height) ─
        log_card = QFrame()
        log_card.setObjectName("flashLogCard")
        log_card.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Expanding)
        lc = QVBoxLayout(log_card)
        lc.setContentsMargins(20, 18, 20, 18)
        lc.setSpacing(14)
        log_title = QLabel("Flash output")
        log_title.setObjectName("flashCardTitle")
        lc.addWidget(log_title)

        self.log = QTextEdit()
        self.log.setObjectName("flashOutputLog")
        self.log.setReadOnly(True)
        self.log.setFont(app_mono_font(10))
        self.log.setMinimumHeight(260)
        self.log.setPlaceholderText("esptool output will appear here…")
        self.log.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Expanding)
        lc.addWidget(self.log, stretch=1)

        self.progress = QProgressBar()
        self.progress.setRange(0, 0)
        self.progress.setVisible(False)
        lc.addWidget(self.progress)
        layout.addWidget(log_card, stretch=1)

        self.btn_flash = QPushButton("Flash ShellOS to board")
        self.btn_flash.setObjectName("flashBtn")
        self.btn_flash.setMinimumHeight(52)
        self.btn_flash.clicked.connect(self._on_flash_clicked)
        layout.addWidget(self.btn_flash)

        hint_lines = [
            "USB: install CP210x or CH340 driver if the port does not appear. "
            "Hold BOOT, tap RST, then release BOOT to enter download mode.",
        ]
        if self._release:
            hint_lines.append(
                "After a successful flash, the serial monitor opens @ 115200. "
                "Then run Wi‑Fi commands in the shell, or use TCP port 2323 from your PC once connected."
            )
        hint = QLabel("\n".join(hint_lines))
        hint.setObjectName("hint")
        hint.setWordWrap(True)
        layout.addWidget(hint)

        if self.lbl_fw_path is not None:
            self._sync_fw_path_label()

        return outer

    # ── Package tab ───────────────────────────────────────────────────────────

    def _build_package_tab(self) -> QWidget:
        """
        Packages UI: one scroll area for the whole tab (connect, build, table, log, tip)
        so layout matches Flash and nothing overlaps when the window is short.
        """
        tab = QWidget()
        outer = QVBoxLayout(tab)
        outer.setContentsMargins(0, 4, 0, 8)
        outer.setSpacing(0)

        ctrl_h = 42

        _pkg_scroll = QScrollArea()
        _pkg_scroll.setObjectName("pkgTabScroll")
        _pkg_scroll.setWidgetResizable(True)
        _pkg_scroll.setFrameShape(QFrame.Shape.NoFrame)
        _pkg_scroll.setHorizontalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAsNeeded)
        _pkg_scroll.setVerticalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAsNeeded)
        _pkg_scroll.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Expanding)

        _pkg_scroll_inner = QWidget()
        top_layout = QVBoxLayout(_pkg_scroll_inner)
        top_layout.setSpacing(UI_SPACING_MAJOR)
        top_layout.setContentsMargins(4, 8, 10, 24)
        _pkg_scroll.setWidget(_pkg_scroll_inner)
        outer.addWidget(_pkg_scroll, stretch=1)

        # --- Connect card ---
        conn = QFrame()
        conn.setObjectName("pkgSectionCard")
        cv = QVBoxLayout(conn)
        cv.setContentsMargins(24, 22, 24, 22)
        cv.setSpacing(14)

        ct = QLabel("Packages on your device")
        ct.setObjectName("flashCardTitle")
        cv.addWidget(ct)
        cs = QLabel(
            "Enter the board’s IP address (from <b>wifi status</b> in the serial shell, or your router). "
            "The device must be on the same network as this PC."
        )
        cs.setTextFormat(Qt.TextFormat.RichText)
        cs.setWordWrap(True)
        cs.setObjectName("flashCardSubtitle")
        cv.addWidget(cs)

        ip_row = QHBoxLayout()
        ip_row.setSpacing(14)
        ip_row.addWidget(self._flash_field_label("Device IP"))
        self._pkg_ip = QLineEdit()
        self._pkg_ip.setPlaceholderText("e.g. 192.168.1.42")
        self._pkg_ip.setMinimumHeight(ctrl_h)
        self._pkg_ip.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Fixed)
        self._pkg_ip.textChanged.connect(self._pkg_ip_changed)
        ip_row.addWidget(self._pkg_ip, stretch=1)
        self._pkg_connect_btn = QPushButton("Connect")
        self._pkg_connect_btn.setObjectName("pkgActionBtn")
        self._pkg_connect_btn.setMinimumHeight(ctrl_h)
        self._pkg_connect_btn.setMinimumWidth(124)
        self._pkg_connect_btn.setToolTip(
            f"Checks TCP {HTTP_UPLOAD_PORT}, then loads the package list over HTTP."
        )
        self._pkg_connect_btn.clicked.connect(self._pkg_connect)
        ip_row.addWidget(self._pkg_connect_btn)
        cv.addLayout(ip_row)

        status_strip = QFrame()
        status_strip.setObjectName("pkgConnStatusStrip")
        sw = QVBoxLayout(status_strip)
        sw.setContentsMargins(12, 10, 12, 10)
        self._pkg_conn_status = QLabel("Not connected")
        self._pkg_conn_status.setObjectName("flashStatusText")
        self._pkg_conn_status.setWordWrap(True)
        self._pkg_conn_status.setStyleSheet("color: #64748b;")
        sw.addWidget(self._pkg_conn_status)
        cv.addWidget(status_strip)

        port_hint = QLabel(
            f"<b>HTTP</b> port <b>{HTTP_UPLOAD_PORT}</b> — package install and list. "
            f"<b>TCP</b> port <b>{SHELL_TCP_PORT}</b> (PuTTY Raw) — interactive shell only."
        )
        port_hint.setObjectName("hint")
        port_hint.setWordWrap(True)
        port_hint.setTextFormat(Qt.TextFormat.RichText)
        cv.addWidget(port_hint)

        top_layout.addWidget(conn)

        # --- Build card ---
        build = QFrame()
        build.setObjectName("pkgSectionCard")
        bv = QVBoxLayout(build)
        bv.setContentsMargins(24, 22, 24, 22)
        bv.setSpacing(14)

        bt = QLabel("Build and deploy")
        bt.setObjectName("flashCardTitle")
        bv.addWidget(bt)
        bs = QLabel(
            "Turn an Arduino-style .ino into a .shpkg, then send it to the device. "
            "Requires Python and shellos_compiler (see project tools/)."
        )
        bs.setObjectName("flashCardSubtitle")
        bs.setWordWrap(True)
        bv.addWidget(bs)

        ino_row = QHBoxLayout()
        ino_row.setSpacing(14)
        ino_row.setAlignment(Qt.AlignmentFlag.AlignTop)
        ino_row.addWidget(self._flash_field_label("Sketch"))
        self._pkg_ino_path = QLabel("(none selected)")
        self._pkg_ino_path.setObjectName("pathLabel")
        self._pkg_ino_path.setWordWrap(True)
        self._pkg_ino_path.setMinimumHeight(36)
        self._pkg_ino_path.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Preferred)
        ino_row.addWidget(self._pkg_ino_path, stretch=1)
        btn_ino = QPushButton("Browse…")
        btn_ino.setMinimumHeight(ctrl_h)
        btn_ino.setMinimumWidth(100)
        btn_ino.clicked.connect(self._pkg_browse_ino)
        ino_row.addWidget(btn_ino)
        bv.addLayout(ino_row)

        pkg_row = QHBoxLayout()
        pkg_row.setSpacing(14)
        pkg_row.addWidget(self._flash_field_label(".shpkg file"))
        self._pkg_shpkg_deploy = QLineEdit()
        self._pkg_shpkg_deploy.setPlaceholderText("Set after Build, or browse to an existing .shpkg")
        self._pkg_shpkg_deploy.setMinimumHeight(ctrl_h)
        self._pkg_shpkg_deploy.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Fixed)
        pkg_row.addWidget(self._pkg_shpkg_deploy, stretch=1)
        btn_shpkg = QPushButton("Browse…")
        btn_shpkg.setMinimumHeight(ctrl_h)
        btn_shpkg.setMinimumWidth(100)
        btn_shpkg.clicked.connect(self._pkg_browse_shpkg)
        pkg_row.addWidget(btn_shpkg)
        bv.addLayout(pkg_row)

        btn_row = QHBoxLayout()
        btn_row.setSpacing(10)
        self._pkg_build_btn = QPushButton("Build")
        self._pkg_build_btn.setObjectName("pkgActionBtn")
        self._pkg_build_btn.setMinimumHeight(42)
        self._pkg_build_btn.setToolTip("Compile .ino to .shpkg (no upload)")
        self._pkg_build_btn.clicked.connect(lambda: self._pkg_build(deploy_after=False))
        btn_row.addWidget(self._pkg_build_btn)

        self._pkg_deploy_btn = QPushButton("Upload && install")
        self._pkg_deploy_btn.setObjectName("pkgActionBtn")
        self._pkg_deploy_btn.setMinimumHeight(42)
        self._pkg_deploy_btn.setToolTip("POST the selected .shpkg to the device")
        self._pkg_deploy_btn.clicked.connect(self._pkg_deploy)
        btn_row.addWidget(self._pkg_deploy_btn)

        self._pkg_build_deploy_btn = QPushButton("Build + deploy + run")
        self._pkg_build_deploy_btn.setObjectName("pkgDeployBtn")
        self._pkg_build_deploy_btn.setMinimumHeight(42)
        self._pkg_build_deploy_btn.setToolTip("Build, upload, and start the package")
        self._pkg_build_deploy_btn.clicked.connect(lambda: self._pkg_build(deploy_after=True))
        btn_row.addWidget(self._pkg_build_deploy_btn)
        btn_row.addStretch(1)
        bv.addLayout(btn_row)

        self._pkg_deploy_status = QLabel("")
        self._pkg_deploy_status.setObjectName("statusLabel")
        self._pkg_deploy_status.setWordWrap(True)
        bv.addWidget(self._pkg_deploy_status)

        top_layout.addWidget(build)

        # ── Installed packages (fills remaining height) ────────────────────────
        table_card = QFrame()
        table_card.setObjectName("pkgSectionCard")
        table_card.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Expanding)
        m_layout = QVBoxLayout(table_card)
        m_layout.setContentsMargins(18, 16, 18, 16)
        m_layout.setSpacing(12)

        hdr = QHBoxLayout()
        hdr.setSpacing(12)
        tbl_title = QLabel("Installed packages")
        tbl_title.setObjectName("flashCardTitle")
        hdr.addWidget(tbl_title)
        hdr.addStretch(1)
        self._pkg_refresh_btn = QPushButton("Refresh list")
        self._pkg_refresh_btn.setMinimumHeight(38)
        self._pkg_refresh_btn.clicked.connect(self._pkg_refresh)
        hdr.addWidget(self._pkg_refresh_btn)
        self._pkg_running_count = QLabel("")
        self._pkg_running_count.setObjectName("statusLabel")
        self._pkg_running_count.setStyleSheet("color: #059669; font-size: 13px; font-weight: 600;")
        hdr.addWidget(self._pkg_running_count)
        m_layout.addLayout(hdr)

        self._pkg_table = QTableWidget(0, 5)
        self._pkg_table.setHorizontalHeaderLabels(
            ["Package", "Version", "Status", "Description", "Actions"]
        )
        _ph = self._pkg_table.horizontalHeader()
        _ph.setStretchLastSection(False)
        _ph.setSectionResizeMode(0, QHeaderView.ResizeMode.Fixed)
        _ph.setSectionResizeMode(1, QHeaderView.ResizeMode.Fixed)
        _ph.setSectionResizeMode(2, QHeaderView.ResizeMode.Fixed)
        _ph.setSectionResizeMode(3, QHeaderView.ResizeMode.Stretch)
        _ph.setSectionResizeMode(4, QHeaderView.ResizeMode.Fixed)
        self._pkg_table.setSelectionBehavior(QTableWidget.SelectionBehavior.SelectRows)
        self._pkg_table.setEditTriggers(QTableWidget.EditTrigger.NoEditTriggers)
        self._pkg_table.setAlternatingRowColors(True)
        _vh = self._pkg_table.verticalHeader()
        _vh.setVisible(False)
        _vh.setDefaultSectionSize(60)
        _vh.setMinimumSectionSize(60)
        _vh.setSectionResizeMode(QHeaderView.ResizeMode.Fixed)
        self._pkg_table.setShowGrid(False)
        self._pkg_table.setMinimumHeight(420)
        self._pkg_table.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Expanding)
        self._pkg_table.setColumnWidth(0, 150)
        self._pkg_table.setColumnWidth(1, 80)
        self._pkg_table.setColumnWidth(2, 100)
        self._pkg_table.setColumnWidth(4, 360)
        self._pkg_table.setMinimumWidth(700)
        self._pkg_table.setHorizontalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAsNeeded)
        m_layout.addWidget(self._pkg_table, stretch=1)

        top_layout.addWidget(table_card, stretch=1)

        # ── Activity log ────────────────────────────────────────────────────────
        log_card = QFrame()
        log_card.setObjectName("pkgSectionCard")
        log_card.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Preferred)
        ll = QVBoxLayout(log_card)
        ll.setContentsMargins(18, 16, 18, 16)
        ll.setSpacing(10)
        log_title = QLabel("Activity log")
        log_title.setObjectName("flashCardTitle")
        ll.addWidget(log_title)
        self._pkg_log = QTextEdit()
        self._pkg_log.setReadOnly(True)
        self._pkg_log.setFont(app_mono_font(10))
        self._pkg_log.setMinimumHeight(160)
        self._pkg_log.setPlaceholderText("Build output, uploads, and HTTP responses…")
        self._pkg_log.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Preferred)
        ll.addWidget(self._pkg_log)

        top_layout.addWidget(log_card)

        foot = QLabel(
            "Tip: if Connect fails but the shell works on port 2323, check Windows Firewall "
            "for Python — it may block outbound connections to port 8080."
        )
        foot.setObjectName("hint")
        foot.setWordWrap(True)
        top_layout.addWidget(foot)

        # Restore saved IP
        self._pkg_settings = QSettings("ShellOS", "Imager")
        saved_ip = self._pkg_settings.value("pkg/device_ip", "")
        if saved_ip:
            self._pkg_ip.setText(str(saved_ip))

        self._pkg_auto_refresh = QTimer(self)
        self._pkg_auto_refresh.setInterval(8000)
        self._pkg_auto_refresh.timeout.connect(self._pkg_refresh_silent)

        return tab

    # ── Package tab helpers ───────────────────────────────────────────────────

    def _pkg_log_append(self, msg: str) -> None:
        self._pkg_log.moveCursor(QTextCursor.MoveOperation.End)
        self._pkg_log.insertPlainText(msg + "\n")
        self._pkg_log.moveCursor(QTextCursor.MoveOperation.End)

    def _pkg_ip_changed(self, text: str) -> None:
        self._pkg_settings.setValue("pkg/device_ip", text.strip())
        if not text.strip():
            self._pkg_conn_status.setText("Not connected")
            self._pkg_conn_status.setStyleSheet("color: #64748b;")
            self._pkg_auto_refresh.stop()

    def _pkg_connect(self) -> None:
        ip = self._pkg_ip.text().strip()
        if not ip:
            QMessageBox.warning(self, APP_NAME, "Enter the device IP address first.")
            return
        self._pkg_conn_status.setText(f"Probing TCP {HTTP_UPLOAD_PORT}...")
        self._pkg_conn_status.setStyleSheet("color: #d97706; font-weight: 600;")
        self._pkg_connect_btn.setEnabled(False)

        # Fallback: re-enable button after 15 s if thread never fires
        self._connect_timeout = QTimer(self)
        self._connect_timeout.setSingleShot(True)
        self._connect_timeout.setInterval(15000)
        self._connect_timeout.timeout.connect(self._pkg_connect_timeout)
        self._connect_timeout.start()

        # Capture IP on the main thread — never read Qt widgets from worker threads
        _ip = ip

        def _do() -> None:
            ok, tcp_err = _tcp_port_open(_ip, HTTP_UPLOAD_PORT, TCP_PROBE_TIMEOUT_S)
            if not ok:
                shell_ok, _ = _tcp_port_open(_ip, SHELL_TCP_PORT, TCP_PROBE_TIMEOUT_S)
                extra = (
                    "  (TCP 2323 shell works on this PC — firewall may block Python on 8080 only.)"
                    if shell_ok
                    else ""
                )
                self._invoke_main.emit(partial(self._pkg_connect_done, -2, tcp_err + extra))
                return
            self._invoke_main.emit(partial(self._pkg_conn_status.setText, "Port open — loading list..."))
            status, body = self._pkg_api("GET", "/pkg/list", ip=_ip, timeout=12)
            self._invoke_main.emit(partial(self._pkg_connect_done, status, body))

        threading.Thread(target=_do, daemon=True).start()

    def _pkg_connect_timeout(self) -> None:
        """Called if connect thread takes too long — unblock the UI."""
        self._pkg_connect_btn.setEnabled(True)
        self._pkg_conn_status.setText(f"Timed out — is TCP {HTTP_UPLOAD_PORT} open?")
        self._pkg_conn_status.setStyleSheet("color: #dc2626; font-weight: 600;")

    def _pkg_connect_done(self, status: int, body: str) -> None:
        if hasattr(self, "_connect_timeout"):
            self._connect_timeout.stop()
        self._pkg_connect_btn.setEnabled(True)
        if status == -2:
            self._pkg_conn_status.setText(f"TCP {HTTP_UPLOAD_PORT} closed / blocked")
            self._pkg_conn_status.setStyleSheet("color: #dc2626; font-weight: 600;")
            self._pkg_log_append(f"[TCP] {body}")
            return
        if status == 200:
            self._pkg_conn_status.setText(f"Connected  {self._pkg_ip.text().strip()}:{HTTP_UPLOAD_PORT}")
            self._pkg_conn_status.setStyleSheet("color: #059669; font-weight: 600;")
            self._pkg_refresh_done(status, body)
            self._pkg_auto_refresh.start()
        else:
            self._pkg_conn_status.setText(f"HTTP failed ({status})")
            self._pkg_conn_status.setStyleSheet("color: #dc2626; font-weight: 600;")
            self._pkg_log_append(f"HTTP /pkg/list failed ({status}): {body[:200]}")

    def _pkg_browse_ino(self) -> None:
        p, _ = QFileDialog.getOpenFileName(self, "Select Arduino sketch", "", "Arduino sketch (*.ino)")
        if p:
            self._pkg_ino_path.setText(p)
            self._pkg_shpkg_deploy.setText("")

    def _pkg_browse_shpkg(self) -> None:
        p, _ = QFileDialog.getOpenFileName(self, "Select .shpkg file", "", "ShellOS package (*.shpkg)")
        if p:
            self._pkg_shpkg_deploy.setText(p)

    def _pkg_build(self, deploy_after: bool = False) -> None:
        ino = self._pkg_ino_path.text()
        if not ino or ino == "(none selected)":
            QMessageBox.warning(self, APP_NAME, "Select an .ino sketch first.")
            return
        if not Path(ino).is_file():
            QMessageBox.warning(self, APP_NAME, f"File not found:\n{ino}")
            return

        compiler = Path(__file__).parent.parent / "tools" / "shellos_compiler" / "shellos_compiler.py"
        if not compiler.is_file():
            QMessageBox.warning(self, APP_NAME, f"Compiler not found:\n{compiler}")
            return

        out_dir = str(Path(ino).parent)
        self._pkg_build_btn.setEnabled(False)
        self._pkg_build_deploy_btn.setEnabled(False)
        self._pkg_deploy_btn.setEnabled(False)
        self._pkg_deploy_status.setText("Building...")
        self._pkg_deploy_status.setStyleSheet("color: #d97706; font-weight: 600;")
        self._pkg_log_append(f"Building {Path(ino).name} ...")

        proc = QProcess(self)
        proc.setProcessChannelMode(QProcess.ProcessChannelMode.MergedChannels)

        def _on_out() -> None:
            data = proc.readAllStandardOutput().data().decode(errors="replace")
            for line in data.splitlines():
                self._pkg_log_append(line)

        def _on_done(code: int, _status: QProcess.ExitStatus) -> None:
            self._pkg_build_btn.setEnabled(True)
            self._pkg_build_deploy_btn.setEnabled(True)
            self._pkg_deploy_btn.setEnabled(True)
            if code == 0:
                ino_stem = re.sub(r"\.ino$", "", Path(ino).name, flags=re.IGNORECASE)
                shpkg = Path(out_dir) / f"{ino_stem}.shpkg"
                if shpkg.is_file():
                    self._pkg_shpkg_deploy.setText(str(shpkg))
                    self._pkg_log_append(f"[OK] Built: {shpkg.name}")
                    self._pkg_deploy_status.setText("")
                    if deploy_after:
                        self._pkg_deploy(run_after=True)
                else:
                    self._pkg_deploy_status.setText("Build ok but .shpkg missing")
                    self._pkg_deploy_status.setStyleSheet("color: #dc2626; font-weight: 600;")
            else:
                self._pkg_deploy_status.setText(f"Build failed (exit {code})")
                self._pkg_deploy_status.setStyleSheet("color: #dc2626; font-weight: 600;")

        proc.readyReadStandardOutput.connect(_on_out)
        proc.finished.connect(_on_done)
        proc.start(find_python(), [str(compiler), "build", ino, "--out", out_dir])
        if not proc.waitForStarted(3000):
            self._pkg_build_btn.setEnabled(True)
            self._pkg_build_deploy_btn.setEnabled(True)
            self._pkg_deploy_btn.setEnabled(True)
            self._pkg_log_append("ERROR: could not start compiler")

    def _pkg_api(self, method: str, path: str, body: bytes | None = None,
                 ip: str = "", timeout: int = 12) -> tuple[int, str]:
        """
        Call from background threads only. Pass ip= explicitly (never read Qt widgets here).

        Always sets Connection: close so ESP32's httpd (which closes after every response)
        does not leave Python's urllib trying to reuse a dead keep-alive socket, which would
        cause the next request to hang until a socket.timeout fires.
        """
        if not ip:
            return -1, "No device IP set"
        url = f"http://{ip}:{HTTP_UPLOAD_PORT}{path}"
        req = urllib.request.Request(url, data=body, method=method)
        req.add_header("Connection", "close")
        if body:
            req.add_header("Content-Type", "application/octet-stream")
        try:
            with urllib.request.urlopen(req, timeout=timeout) as resp:
                return resp.status, resp.read().decode("utf-8", errors="replace")
        except urllib.error.HTTPError as e:
            return e.code, e.read().decode("utf-8", errors="replace")
        except Exception as e:
            return -1, str(e)

    def _pkg_deploy(self, run_after: bool = False) -> None:
        shpkg = self._pkg_shpkg_deploy.text().strip()
        if not shpkg:
            QMessageBox.warning(self, APP_NAME, "Select a .shpkg file to upload.")
            return
        if not Path(shpkg).is_file():
            QMessageBox.warning(self, APP_NAME, f"File not found:\n{shpkg}")
            return
        _ip = self._pkg_ip.text().strip()
        if not _ip:
            QMessageBox.warning(self, APP_NAME, "Enter the device IP address.")
            return

        self._pkg_deploy_btn.setEnabled(False)
        self._pkg_deploy_status.setText("Uploading...")
        self._pkg_deploy_status.setStyleSheet("color: #d97706; font-weight: 600;")
        self._pkg_log_append(f"Uploading {Path(shpkg).name} to {_ip} ...")
        data = Path(shpkg).read_bytes()
        _pkg_name = re.sub(r"\.shpkg$", "", Path(shpkg).name, flags=re.IGNORECASE)

        def _do_upload() -> None:
            status, body = self._pkg_api("POST", "/pkg/upload", data, ip=_ip, timeout=30)
            self._invoke_main.emit(
                partial(self._pkg_deploy_done, status, body, _pkg_name if run_after else None)
            )

        threading.Thread(target=_do_upload, daemon=True).start()

    def _pkg_deploy_done(self, status: int, body: str, run_name: str | None = None) -> None:
        self._pkg_deploy_btn.setEnabled(True)
        if status == 200:
            try:
                msg = json.loads(body).get("message", "installed")
            except Exception:
                msg = "installed"
            self._pkg_deploy_status.setStyleSheet("color: #059669; font-weight: 600;")
            self._pkg_deploy_status.setText(f"[OK] {msg}")
            self._pkg_log_append(f"[OK] Installed: {msg}")
            if run_name:
                self._pkg_run(run_name)
            else:
                self._pkg_refresh()
        else:
            self._pkg_deploy_status.setStyleSheet("color: #dc2626; font-weight: 600;")
            self._pkg_deploy_status.setText(f"Upload failed ({status})")
            self._pkg_log_append(f"[ERR] Upload ({status}): {body[:100]}")

    def _pkg_refresh(self) -> None:
        _ip = self._pkg_ip.text().strip()
        if not _ip:
            return
        self._pkg_refresh_btn.setEnabled(False)

        def _do_fetch() -> None:
            status, body = self._pkg_api("GET", "/pkg/list", ip=_ip)
            self._invoke_main.emit(partial(self._pkg_refresh_done, status, body))

        threading.Thread(target=_do_fetch, daemon=True).start()

    def _pkg_refresh_silent(self) -> None:
        _ip = self._pkg_ip.text().strip()
        if not _ip:
            return

        def _do() -> None:
            status, body = self._pkg_api("GET", "/pkg/list", ip=_ip)
            if status == 200:
                self._invoke_main.emit(partial(self._pkg_refresh_done, status, body, True))

        threading.Thread(target=_do, daemon=True).start()

    def _pkg_refresh_done(self, status: int, body: str, silent: bool = False) -> None:
        self._pkg_refresh_btn.setEnabled(True)
        if status != 200:
            if not silent:
                self._pkg_log_append(f"[ERR] List failed ({status}): {body[:80]}")
            self._pkg_conn_status.setText("Disconnected")
            self._pkg_conn_status.setStyleSheet("color: #dc2626; font-weight: 600;")
            return

        try:
            # The firmware may embed literal control characters (newlines, tabs,
            # carriage returns) inside JSON string values when a package manifest
            # has multi-line or trailing-whitespace description fields.
            # JSON requires those to be escaped (\n, \t, …); replace them with a
            # space so the parser doesn't reject an otherwise valid response.
            sanitised = re.sub(r'[\x00-\x1f\x7f]', ' ', body)
            packages = json.loads(sanitised)
        except Exception:
            if not silent:
                self._pkg_log_append(f"[ERR] Bad response: {body[:80]}")
            return

        running_count = sum(1 for p in packages if p.get("running", False))
        self._pkg_running_count.setText(
            f"{running_count} running / {len(packages)} total" if packages else ""
        )

        _ta = Qt.AlignmentFlag
        _al = _ta.AlignVCenter | _ta.AlignLeft
        _ac = _ta.AlignVCenter | _ta.AlignHCenter

        self._pkg_table.setRowCount(0)
        for pkg in packages:
            name    = pkg.get("name", "?")
            version = pkg.get("version", "?")
            desc    = str(pkg.get("description", "") or "").strip()
            if desc == "," or desc == "，":
                desc = ""
            running = pkg.get("running", False)

            row = self._pkg_table.rowCount()
            self._pkg_table.insertRow(row)

            name_item = QTableWidgetItem(name)
            name_item.setForeground(QColor("#0f172a"))
            name_item.setTextAlignment(_al)
            self._pkg_table.setItem(row, 0, name_item)

            ver_item = QTableWidgetItem(version)
            ver_item.setForeground(QColor("#334155"))
            ver_item.setTextAlignment(_al)
            self._pkg_table.setItem(row, 1, ver_item)

            status_item = QTableWidgetItem("running" if running else "stopped")
            status_item.setForeground(QColor("#059669") if running else QColor("#64748b"))
            status_item.setTextAlignment(_ac)
            self._pkg_table.setItem(row, 2, status_item)

            desc_item = QTableWidgetItem(desc)
            desc_item.setForeground(QColor("#475569"))
            desc_item.setTextAlignment(_al)
            self._pkg_table.setItem(row, 3, desc_item)

            action_widget = QWidget()
            action_widget.setMinimumHeight(58)
            action_layout = QHBoxLayout(action_widget)
            action_layout.setContentsMargins(16, 0, 16, 0)
            action_layout.setSpacing(8)
            action_layout.setAlignment(_ta.AlignVCenter)

            btn_run = QPushButton("Run")
            btn_run.setEnabled(not running)
            btn_run.setFixedSize(80, 34)
            btn_run.setObjectName("pkgRunBtn" if not running else "pkgRunBtnOff")
            btn_run.clicked.connect(lambda _, n=name: self._pkg_run(n))
            action_layout.addWidget(btn_run)

            btn_stop = QPushButton("Stop")
            btn_stop.setEnabled(running)
            btn_stop.setFixedSize(80, 34)
            btn_stop.setObjectName("pkgStopBtn")
            btn_stop.clicked.connect(lambda _, n=name: self._pkg_stop(n))
            action_layout.addWidget(btn_stop)

            btn_remove = QPushButton("Remove")
            btn_remove.setEnabled(not running)
            btn_remove.setFixedSize(96, 34)
            btn_remove.setObjectName("pkgRemoveBtn")
            btn_remove.clicked.connect(lambda _, n=name: self._pkg_remove(n))
            action_layout.addWidget(btn_remove)

            self._pkg_table.setCellWidget(row, 4, action_widget)
            self._pkg_table.setRowHeight(row, 60)

        if not silent:
            self._pkg_log_append(f"[OK] {len(packages)} package(s)  ({running_count} running)")

    def _pkg_run(self, name: str) -> None:
        _ip = self._pkg_ip.text().strip()
        self._pkg_log_append(f"Starting {name} ...")

        def _do() -> None:
            status, body = self._pkg_api("POST", f"/pkg/run/{name}", ip=_ip)

            def _finish() -> None:
                self._pkg_log_append(
                    f"[OK] {name} started" if status == 200 else f"[ERR] start failed ({status}): {body}"
                )
                self._pkg_refresh()

            self._invoke_main.emit(_finish)

        threading.Thread(target=_do, daemon=True).start()

    def _pkg_stop(self, name: str) -> None:
        _ip = self._pkg_ip.text().strip()
        self._pkg_log_append(f"Stopping {name} ...")

        def _do() -> None:
            status, body = self._pkg_api("POST", f"/pkg/stop/{name}", ip=_ip)

            def _finish() -> None:
                self._pkg_log_append(
                    f"[OK] {name} stopped" if status == 200 else f"[ERR] stop failed ({status}): {body}"
                )
                self._pkg_refresh()

            self._invoke_main.emit(_finish)

        threading.Thread(target=_do, daemon=True).start()

    def _pkg_remove(self, name: str) -> None:
        r = QMessageBox.question(
            self, APP_NAME,
            f"Remove package '{name}' from the device?\n\nThis deletes all its files.",
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No,
            QMessageBox.StandardButton.No,
        )
        if r != QMessageBox.StandardButton.Yes:
            return
        _ip = self._pkg_ip.text().strip()
        self._pkg_log_append(f"Removing {name} ...")

        def _do() -> None:
            status, body = self._pkg_api("POST", f"/pkg/remove/{name}", ip=_ip)

            def _finish() -> None:
                self._pkg_log_append(
                    f"[OK] {name} removed" if status == 200 else f"[ERR] remove failed ({status}): {body}"
                )
                self._pkg_refresh()

            self._invoke_main.emit(_finish)

        threading.Thread(target=_do, daemon=True).start()

    # ── Styling ───────────────────────────────────────────────────────────────

    def _apply_style(self) -> None:
        self.setStyleSheet(
            """
            QWidget { background-color: #f1f5f9; color: #0f172a; }
            QFrame#headerCard {
                background-color: #ffffff;
                border: 1px solid #e2e8f0;
                border-radius: 12px;
            }
            QLabel#title {
                font-size: 32px;
                font-weight: 700;
                letter-spacing: -0.5px;
                color: #0f766e;
            }
            QLabel#subtitle {
                color: #64748b;
                font-size: 14px;
                line-height: 1.5;
            }
            QLabel#formLabel {
                color: #475569;
                font-size: 13px;
                font-weight: 600;
            }
            QLabel#pathLabel { color: #2563eb; font-size: 12px; line-height: 1.4; }
            QLabel#statusLabel { font-size: 13px; color: #334155; }
            QLabel#hint {
                color: #64748b;
                font-size: 12px;
                line-height: 1.55;
                padding-top: 6px;
            }
            QFrame#flashSetupCard, QFrame#flashLogCard {
                background-color: #ffffff;
                border: 1px solid #e2e8f0;
                border-radius: 12px;
            }
            QLabel#flashCardTitle {
                font-size: 17px;
                font-weight: 700;
                color: #0f172a;
            }
            QLabel#flashCardSubtitle {
                font-size: 13px;
                color: #64748b;
                line-height: 1.45;
            }
            QLabel#flashFieldLabel {
                color: #475569;
                font-size: 13px;
                font-weight: 600;
            }
            QLabel#flashSubheading {
                font-size: 11px;
                font-weight: 700;
                color: #64748b;
                letter-spacing: 0.06em;
            }
            QFrame#flashStatusStrip {
                background-color: #f8fafc;
                border: 1px solid #e2e8f0;
                border-radius: 10px;
            }
            QLabel#flashStatusText {
                font-size: 13px;
                color: #334155;
                line-height: 1.45;
            }
            QFrame#sep { background-color: #e2e8f0; max-height: 1px; margin-top: 6px; margin-bottom: 6px; border: none; }
            QScrollArea#flashTabScroll, QScrollArea#pkgTabScroll {
                background-color: transparent;
                border: none;
            }
            QScrollArea#flashTabScroll > QWidget > QWidget,
            QScrollArea#pkgTabScroll > QWidget > QWidget {
                background-color: transparent;
            }
            QTextEdit#flashOutputLog {
                background-color: #f8fafc;
                color: #1e293b;
                border: 1px solid #cbd5e1;
                border-radius: 10px;
            }
            QFrame#pkgSectionCard {
                background-color: #ffffff;
                border: 1px solid #e2e8f0;
                border-radius: 12px;
            }
            QFrame#pkgConnStatusStrip {
                background-color: #f8fafc;
                border: 1px solid #e2e8f0;
                border-radius: 10px;
            }
            QTabWidget#mainTabs::pane {
                border: 1px solid #e2e8f0;
                border-radius: 10px;
                background-color: #ffffff;
                top: -1px;
                padding: 12px;
            }
            QTabBar::tab {
                background: #e2e8f0;
                border: 1px solid #cbd5e1;
                border-bottom: none;
                border-radius: 8px 8px 0 0;
                padding: 10px 28px;
                margin-right: 4px;
                min-width: 100px;
                font-size: 14px;
                font-weight: 600;
                color: #64748b;
            }
            QTabBar::tab:selected {
                background: #ffffff;
                color: #0f766e;
                border-color: #e2e8f0;
                border-bottom: 1px solid #ffffff;
                margin-bottom: -1px;
            }
            QTabBar::tab:hover:!selected { color: #334155; background: #f1f5f9; }
            QPushButton#pkgActionBtn {
                background-color: #eff6ff;
                color: #1d4ed8;
                border: 1px solid #93c5fd;
                border-radius: 8px;
                padding: 8px 18px;
                min-height: 28px;
                font-weight: 600;
            }
            QPushButton#pkgActionBtn:hover { background-color: #dbeafe; border-color: #3b82f6; color: #1e40af; }
            QPushButton#pkgActionBtn:disabled { background-color: #f1f5f9; color: #94a3b8; border-color: #e2e8f0; }
            QPushButton#pkgDeployBtn {
                background-color: #ecfdf5;
                color: #047857;
                border: 1px solid #6ee7b7;
                border-radius: 8px;
                padding: 8px 20px;
                min-height: 28px;
                font-weight: 700;
            }
            QPushButton#pkgDeployBtn:hover { background-color: #d1fae5; border-color: #10b981; color: #065f46; }
            QPushButton#pkgDeployBtn:disabled { background-color: #f1f5f9; color: #94a3b8; border-color: #e2e8f0; }
            QPushButton#pkgRunBtn {
                background-color: #ecfdf5;
                color: #047857;
                border: 1px solid #6ee7b7;
                border-radius: 7px;
                font-size: 12px;
                font-weight: 700;
            }
            QPushButton#pkgRunBtn:hover { background-color: #d1fae5; border-color: #10b981; color: #065f46; }
            QPushButton#pkgRunBtn:disabled { background-color: #f1f5f9; color: #94a3b8; border-color: #e2e8f0; }
            QPushButton#pkgRunBtnOff {
                background-color: #f1f5f9;
                color: #94a3b8;
                border: 1px solid #e2e8f0;
                border-radius: 7px;
                font-size: 12px;
                font-weight: 600;
            }
            QPushButton#pkgStopBtn {
                background-color: #fff7ed;
                color: #c2410c;
                border: 1px solid #fdba74;
                border-radius: 7px;
                font-size: 12px;
                font-weight: 700;
            }
            QPushButton#pkgStopBtn:hover { background-color: #ffedd5; border-color: #f97316; color: #9a3412; }
            QPushButton#pkgStopBtn:disabled { background-color: #f1f5f9; color: #94a3b8; border-color: #e2e8f0; }
            QPushButton#pkgRemoveBtn {
                background-color: #fef2f2;
                color: #b91c1c;
                border: 1px solid #fca5a5;
                border-radius: 7px;
                font-size: 12px;
                font-weight: 700;
            }
            QPushButton#pkgRemoveBtn:hover { background-color: #fee2e2; border-color: #ef4444; color: #991b1b; }
            QPushButton#pkgRemoveBtn:disabled { background-color: #f8fafc; color: #cbd5e1; border-color: #e2e8f0; }
            QTableWidget {
                background-color: #ffffff;
                border: 1px solid #e2e8f0;
                border-radius: 8px;
                gridline-color: transparent;
                alternate-background-color: #f8fafc;
            }
            QTableWidget::item { padding: 16px 14px; border: none; }
            QTableWidget::item:selected { background-color: #ccfbf1; color: #0f172a; }
            QHeaderView::section {
                background-color: #f8fafc;
                color: #475569;
                border: none;
                border-bottom: 2px solid #e2e8f0;
                padding: 12px 14px;
                font-size: 12px;
                font-weight: 700;
            }
            QLineEdit {
                background-color: #ffffff;
                border: 1px solid #cbd5e1;
                border-radius: 8px;
                padding: 8px 12px;
                font-size: 13px;
                color: #0f172a;
                selection-background-color: #99f6e4;
            }
            QLineEdit:focus { border: 2px solid #14b8a6; }
            QComboBox, QPushButton {
                background-color: #ffffff;
                border: 1px solid #cbd5e1;
                border-radius: 8px;
                padding: 8px 14px;
                min-height: 24px;
                font-size: 13px;
                color: #0f172a;
            }
            QComboBox:hover, QPushButton:hover { border-color: #14b8a6; }
            QComboBox::drop-down { border: none; width: 28px; }
            QComboBox QAbstractItemView {
                background: #ffffff;
                border: 1px solid #e2e8f0;
                selection-background-color: #ccfbf1;
                selection-color: #0f172a;
                padding: 4px;
            }
            QPushButton#flashBtn {
                background-color: #0d9488;
                color: #ffffff;
                font-weight: 700;
                font-size: 15px;
                border: none;
                border-radius: 10px;
                padding: 14px 24px;
            }
            QPushButton#flashBtn:hover { background-color: #0f766e; }
            QPushButton#flashBtn:disabled { background-color: #e2e8f0; color: #94a3b8; }
            QTextEdit {
                background-color: #ffffff;
                color: #334155;
                border: 1px solid #e2e8f0;
                border-radius: 10px;
                padding: 12px 14px;
                font-size: 12px;
                selection-background-color: #99f6e4;
                selection-color: #0f172a;
            }
            QProgressBar {
                border: 1px solid #e2e8f0;
                border-radius: 6px;
                text-align: center;
                height: 10px;
                margin-top: 4px;
                background: #f1f5f9;
            }
            QProgressBar::chunk { background-color: #14b8a6; border-radius: 4px; }
            """
        )

    def _sync_fw_path_label(self) -> None:
        if self.lbl_fw_path is None:
            return
        resolved = self._resolved_fw_dir()
        if resolved is not None:
            try:
                legacy = resolved.resolve() != self._fw_dir.resolve()
            except OSError:
                legacy = str(resolved) != str(self._fw_dir)
            if legacy:
                self.lbl_fw_path.setText(
                    f"{resolved}\n"
                    f"(Legacy layout is OK. Optional: copy the three .bin files to {self._fw_dir}.)"
                )
            else:
                self.lbl_fw_path.setText(str(resolved))
        else:
            self.lbl_fw_path.setText(str(self._fw_dir))

    def _browse_firmware(self) -> None:
        d = QFileDialog.getExistingDirectory(self, "Select folder with .bin files", str(self._fw_dir))
        if d:
            self._fw_dir = Path(d)
            self._settings.setValue(f"flash/fw_dir/{self._flash_target_id}", str(self._fw_dir))
            self._sync_fw_path_label()
            self._update_firmware_status()

    def _missing_files(self) -> list[str]:
        t = self._active_flash_target()
        if self._resolved_fw_dir() is not None:
            return []
        missing: list[str] = []
        cands = self._firmware_dir_candidates()
        for _, name in t.layout:
            if not any((d / name).is_file() for d in cands):
                missing.append(name)
        return missing

    def _update_firmware_status(self) -> None:
        missing = self._missing_files()
        port_ok = self.combo_port.currentData() is not None
        if missing:
            if self._release:
                self.lbl_fw_status.setTextFormat(Qt.TextFormat.PlainText)
                self.lbl_fw_status.setText(
                    "This app build is incomplete. Rebuild the imager with firmware included, or use a fresh download."
                )
            else:
                extra = ""
                if self._flash_target_id == "esp32-cam":
                    extra = (
                        f" For ESP32-CAM you can use either <b>imager/firmware/esp32-cam/</b> "
                        f"or the older flat folder <b>imager/firmware/</b> (same three .bin files)."
                    )
                self.lbl_fw_status.setTextFormat(Qt.TextFormat.RichText)
                self.lbl_fw_status.setText(
                    f"Missing: {', '.join(missing)}  — copy build outputs (see firmware/README.txt).{extra}"
                )
            self.lbl_fw_status.setStyleSheet("color: #dc2626; font-weight: 600;")
            self.btn_flash.setEnabled(False)
        else:
            self.lbl_fw_status.setTextFormat(Qt.TextFormat.PlainText)
            if self._release:
                self.lbl_fw_status.setText(
                    "Ready to flash."
                    if port_ok
                    else "Connect your board and select a serial port."
                )
            else:
                self.lbl_fw_status.setText(
                    "All firmware files found — ready to flash."
                    if port_ok
                    else "Firmware OK — connect board and pick a serial port."
                )
            self.lbl_fw_status.setStyleSheet(
                "color: #059669; font-weight: 600;" if port_ok else "color: #d97706; font-weight: 600;"
            )
            self.btn_flash.setEnabled(port_ok)
        if getattr(self, "lbl_fw_path", None) is not None:
            self._sync_fw_path_label()

    def refresh_ports(self) -> None:
        self.combo_port.clear()
        if list_ports is None:
            self._append_log("pyserial not installed — run: pip install pyserial")
            self._update_firmware_status()
            return
        for p in sorted(list_ports.comports(), key=lambda x: x.device):
            label = f"{p.device}"
            if p.description and p.description != p.device:
                label += f"  —  {p.description}"
            self.combo_port.addItem(label, userData=p.device)
        if self.combo_port.count() == 0:
            self.combo_port.addItem("(no serial ports found)", userData=None)
            self._update_firmware_status()
            return

        # Auto-select last used port (or best guess) for smoother first-run UX.
        saved = str(self._settings.value("flash/port", "") or "")
        if saved:
            idx = self.combo_port.findData(saved)
            if idx >= 0:
                self.combo_port.setCurrentIndex(idx)
                self._update_firmware_status()
                return

        preferred = -1
        for i in range(self.combo_port.count()):
            dev = str(self.combo_port.itemData(i) or "")
            text = (self.combo_port.itemText(i) or "").lower()
            if any(k in text for k in ("cp210", "ch340", "silicon labs", "usb serial", "jtag", "cdc")):
                preferred = i
                break
            if sys.platform == "win32" and dev.upper().startswith("COM"):
                preferred = 0 if preferred < 0 else preferred
        if preferred >= 0:
            self.combo_port.setCurrentIndex(preferred)

        self._update_firmware_status()

    def _append_log(self, text: str) -> None:
        self.log.moveCursor(QTextCursor.MoveOperation.End)
        self.log.insertPlainText(text + "\n")
        self.log.moveCursor(QTextCursor.MoveOperation.End)

    def _on_flash_clicked(self) -> None:
        port = self.combo_port.currentData()
        if not port:
            QMessageBox.warning(self, APP_NAME, "Select a serial port.")
            return
        missing = self._missing_files()
        if missing:
            if self._release:
                QMessageBox.warning(
                    self,
                    APP_NAME,
                    "Firmware is missing from this app package. Rebuild or re-download ShellOS Imager.",
                )
            else:
                QMessageBox.warning(
                    self,
                    APP_NAME,
                    "Firmware files missing:\n" + "\n".join(missing),
                )
            return
        r = QMessageBox.question(
            self,
            APP_NAME,
            f"This will erase and write flash on the connected {self._active_flash_target().label}.\n\nContinue?",
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No,
            QMessageBox.StandardButton.No,
        )
        if r != QMessageBox.StandardButton.Yes:
            return
        self._run_esptool(port)

    def _close_serial_monitor(self) -> None:
        if self._monitor_dialog is not None:
            self._monitor_dialog.close()
            self._monitor_dialog = None

    def _open_serial_monitor(self, port: str | None, *, show_wifi_setup_hint: bool = False) -> None:
        if not port:
            return
        if list_ports is None:
            QMessageBox.warning(self, APP_NAME, "pyserial is not installed.")
            return
        try:
            import serial  # noqa: F401
        except ImportError:
            QMessageBox.warning(self, APP_NAME, "pyserial is not installed.")
            return
        self._close_serial_monitor()
        dlg = SerialMonitorDialog(
            port, MONITOR_BAUD, self, show_wifi_setup_hint=show_wifi_setup_hint
        )
        self._monitor_dialog = dlg

        def _clear_ref(_code: int = 0) -> None:
            if self._monitor_dialog is dlg:
                self._monitor_dialog = None

        dlg.finished.connect(_clear_ref)
        dlg.show()

    def _on_monitor_clicked(self) -> None:
        port = self.combo_port.currentData()
        if not port:
            QMessageBox.warning(self, APP_NAME, "Select a serial port first.")
            return
        self._open_serial_monitor(port, show_wifi_setup_hint=False)

    def _run_esptool(self, port: str) -> None:
        self._last_flash_port = port
        self._close_serial_monitor()
        self.btn_flash.setEnabled(False)
        self.progress.setVisible(True)
        self.log.clear()
        t = self._active_flash_target()
        fw_root = self._resolved_fw_dir()
        if fw_root is None:
            self._append_log("ERROR: firmware .bin files not found.")
            self._flash_done(False)
            return
        self._append_log(f"→ Port: {port}")
        self._append_log(f"→ Board: {t.label}")
        self._append_log(
            "→ Firmware: bundled with app" if self._release else f"→ Firmware: {fw_root}"
        )
        self._append_log("")

        args = [
            "-m",
            "esptool",
            "--chip",
            t.chip,
            "--port",
            port,
            "--baud",
            DEFAULT_BAUD,
            "write_flash",
            "--flash_mode",
            t.flash_mode,
            "--flash_freq",
            t.flash_freq,
            "--flash_size",
            t.flash_size,
        ]
        for addr, name in t.layout:
            path = fw_root / name
            args.extend([f"0x{addr:x}", str(path)])

        self._process = QProcess(self)
        self._process.setProcessChannelMode(QProcess.ProcessChannelMode.MergedChannels)
        self._process.readyReadStandardOutput.connect(self._on_process_output)
        self._process.finished.connect(self._on_process_finished)
        self._process.start(find_python(), args)
        if not self._process.waitForStarted(3000):
            self._append_log("ERROR: could not start esptool. Is it installed?  pip install esptool")
            self._flash_done(False)

    def _on_process_output(self) -> None:
        if self._process is None:
            return
        data = self._process.readAllStandardOutput().data().decode(errors="replace")
        for line in data.splitlines():
            self._append_log(line)

    def _on_process_finished(self, exit_code: int, _status: QProcess.ExitStatus) -> None:
        ok = exit_code == 0
        self._append_log("")
        self._append_log("Done." if ok else f"Failed (exit code {exit_code}). Try baud 115200 or check USB cable.")
        self._flash_done(ok)

    def _flash_done(self, ok: bool) -> None:
        self.progress.setVisible(False)
        self.btn_flash.setEnabled(True)
        self._update_firmware_status()
        if ok:
            port = self._last_flash_port
            # Wi‑Fi steps are shown inside the serial monitor (green box), not only in a popup.
            QTimer.singleShot(
                650,
                lambda p=port: self._open_serial_monitor(p, show_wifi_setup_hint=True),
            )


def main() -> int:
    # Qt6: use rounding policy (PyQt6 has no setHighDpiScaleFactorPolicy)
    try:
        QApplication.setHighDpiScaleFactorRoundingPolicy(
            Qt.HighDpiScaleFactorRoundingPolicy.PassThrough
        )
    except AttributeError:
        pass
    app = QApplication(sys.argv)
    app.setApplicationName(APP_NAME)
    app.setFont(app_ui_font())
    w = ShellOSImager()
    w.show()
    return app.exec()


if __name__ == "__main__":
    sys.exit(main())
