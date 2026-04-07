#!/usr/bin/env python3
# -*- coding: utf-8 -*-
import sys, io
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", errors="replace")
sys.stderr = io.TextIOWrapper(sys.stderr.buffer, encoding="utf-8", errors="replace")
"""
ShellOS Compiler - Arduino C to Lua transpiler + .shpkg builder + uploader
No external dependencies (stdlib only: re, json, zipfile, struct, urllib).

Usage:
  python shellos_compiler.py build  <sketch.ino>              [--out dir/]
  python shellos_compiler.py upload <device-ip> <pkg.shpkg>
  python shellos_compiler.py run    <device-ip> <pkg-name>
  python shellos_compiler.py stop   <device-ip> <pkg-name>
  python shellos_compiler.py list   <device-ip>
  python shellos_compiler.py logs   <device-ip> <pkg-name>
"""
import re
import json
import struct
import os
import argparse
import urllib.request
import urllib.error

# ─────────────────────────────────────────────────────────────────────────────
# SHPKG writer (matches firmware shpkg.c format)
# ─────────────────────────────────────────────────────────────────────────────
SHPKG_MAGIC   = b"SHPK"
SHPKG_VERSION = 0x01

def write_shpkg(output_path: str, files: dict[str, bytes]) -> None:
    """
    files: {relative_path: bytes_content}
    """
    with open(output_path, "wb") as f:
        f.write(SHPKG_MAGIC)
        f.write(struct.pack("<B", SHPKG_VERSION))
        f.write(struct.pack("<H", len(files)))
        for name, data in files.items():
            name_b = name.encode("utf-8")
            if len(name_b) > 255:
                raise ValueError(f"filename too long: {name}")
            f.write(struct.pack("<B", len(name_b)))
            f.write(name_b)
            f.write(struct.pack("<I", len(data)))
            f.write(data)

# ─────────────────────────────────────────────────────────────────────────────
# Arduino C → Lua transpiler
# ─────────────────────────────────────────────────────────────────────────────

class ArduinoTranspiler:
    """
    Converts a subset of Arduino C (.ino) to Lua.
    Supported constructs:
      #define X Y              → local X = Y
      void setup() { ... }     → extract body, place before loop
      void loop() { ... }      → while true do ... end
      int/float/bool/byte declarations → local <name> = <val>
      pinMode / digitalWrite / digitalRead / analogRead / analogWrite
      delay / millis / micros
      Serial.begin/print/println
      if / else if / else / while / for
      // comments              → -- comments
      /* block */              → --[[ block ]]
      String → string (conceptual; Lua strings work the same)
    """

    # C → Lua type‑stripped re-declarations
    PRIMITIVE_TYPES = r"\b(void|int|long|unsigned|float|double|bool|byte|char|uint8_t|uint16_t|uint32_t|int8_t|int16_t|int32_t|String|const)\b"

    def transpile(self, source: str) -> str:
        lines = source.splitlines()
        out = []

        # ── Pass 1: remove block comments, convert line comments ──
        in_block = False
        cleaned = []
        for line in lines:
            if in_block:
                end = line.find("*/")
                if end != -1:
                    line = line[end + 2:]
                    in_block = False
                else:
                    cleaned.append("--[[ " + line.strip() + " ]]")
                    continue
            # Replace block comments on same line
            line = re.sub(r"/\*.*?\*/", lambda m: "--[[" + m.group()[2:-2] + "]]", line)
            # Unclosed block comment
            bstart = line.find("/*")
            if bstart != -1:
                cleaned.append(line[:bstart] + " --[[" + line[bstart + 2:])
                in_block = True
                continue
            # Line comment
            line = re.sub(r"//", "--", line, count=1)
            cleaned.append(line)
        lines = cleaned

        # ── Pass 2: collect #defines ──
        defines = {}
        remaining = []
        for line in lines:
            m = re.match(r"\s*#define\s+(\w+)\s+(.*)", line)
            if m:
                name, val = m.group(1).strip(), m.group(2).strip()
                # Skip function-like macros
                if "(" in name:
                    remaining.append("-- #define " + name + " " + val)
                else:
                    defines[name] = val
                    remaining.append(f"local {name} = {self._convert_literal(val)}")
            else:
                remaining.append(line)
        lines = remaining

        # ── Pass 3: extract setup() and loop() bodies ──
        setup_body, loop_body, other_lines = self._extract_functions(lines)

        # ── Pass 4: convert each section ──
        result_parts = []

        # Module-level constants (#define, global vars) must come first so
        # they are visible inside both setup and loop.
        converted_other = self._convert_block(other_lines)
        if converted_other:
            result_parts.extend(converted_other)
            result_parts.append("")

        if setup_body:
            result_parts.append("-- setup --")
            result_parts.extend(self._convert_block(setup_body))
        result_parts.append("")
        result_parts.append("-- loop --")
        result_parts.append("while true do")
        if loop_body:
            for l in self._convert_block(loop_body):
                result_parts.append("  " + l)
        result_parts.append("end")

        return "\n".join(result_parts) + "\n"

    # ── helpers ──────────────────────────────────────────────────────────────

    def _extract_functions(self, lines):
        """Split source into setup body, loop body, and other lines."""
        setup_body = []
        loop_body  = []
        other_lines = []

        i = 0
        while i < len(lines):
            line = lines[i]

            # ── Single-line function: void setup() { body }
            m_single_setup = re.match(r"\s*void\s+setup\s*\(\s*\)\s*\{(.*)\}\s*$", line)
            if m_single_setup:
                inner = m_single_setup.group(1).strip()
                setup_body = [s.strip() for s in inner.split(";") if s.strip()] if inner else []
                i += 1
                continue

            m_single_loop = re.match(r"\s*void\s+loop\s*\(\s*\)\s*\{(.*)\}\s*$", line)
            if m_single_loop:
                inner = m_single_loop.group(1).strip()
                loop_body = [s.strip() for s in inner.split(";") if s.strip()] if inner else []
                i += 1
                continue

            # ── Multi-line function: void setup() { or void setup()\n{
            if re.match(r"\s*void\s+setup\s*\(\s*\)\s*\{?\s*$", line):
                i, body = self._collect_body(lines, i)
                setup_body = body
                continue
            if re.match(r"\s*void\s+loop\s*\(\s*\)\s*\{?\s*$", line):
                i, body = self._collect_body(lines, i)
                loop_body = body
                continue

            other_lines.append(line)
            i += 1

        return setup_body, loop_body, other_lines

    def _collect_body(self, lines, start):
        """Collect the { ... } body of a function starting at line 'start'.
        Returns (next_line_index, body_lines)."""
        i = start
        depth = 0
        body = []

        # Find opening brace
        while i < len(lines):
            if "{" in lines[i]:
                depth = lines[i].count("{") - lines[i].count("}")
                # Strip the opening brace line itself
                stripped = lines[i].replace("void setup() {", "").replace("void loop() {", "")
                stripped = re.sub(r"^\s*\{", "", stripped)
                if stripped.strip():
                    body.append(stripped)
                i += 1
                break
            i += 1

        while i < len(lines) and depth > 0:
            depth += lines[i].count("{") - lines[i].count("}")
            if depth > 0:
                body.append(lines[i])
            elif depth == 0:
                # Last line before closing brace
                stripped = re.sub(r"\}\s*$", "", lines[i])
                if stripped.strip():
                    body.append(stripped)
            i += 1

        return i, body

    def _convert_block(self, lines):
        """Convert a list of C lines to Lua lines."""
        out = []
        i = 0
        while i < len(lines):
            line = lines[i]
            converted = self._convert_line(line)
            if converted is not None:
                out.append(converted)
            i += 1
        return out

    def _convert_line(self, line: str) -> str | None:
        """Convert a single C statement to Lua. Returns None to drop the line."""
        stripped = line.strip()

        # Drop blank lines and preserve them
        if not stripped:
            return ""

        # Already a Lua comment (from earlier pass)
        if stripped.startswith("--"):
            return line.rstrip()

        # Include / pragma → drop with comment
        if re.match(r"#\s*(include|pragma|ifndef|ifdef|endif|else)", stripped):
            return "-- " + stripped

        # Variable declarations with optional init
        # int x = 5;  float y;  bool flag = true;
        decl_m = re.match(
            r"^\s*(?:(?:static|const|volatile|unsigned|signed)\s+)*"
            r"(?:int|long|float|double|bool|byte|char|uint8_t|uint16_t|uint32_t|int8_t|int16_t|int32_t|String)"
            r"(?:\s*\*?\s*)(\w+)\s*(?:=\s*(.+?))?\s*;?\s*$", line)
        if decl_m:
            name = decl_m.group(1)
            val  = decl_m.group(2)
            if val:
                val = self._convert_expr(val)
                return self._indent(line) + f"local {name} = {val}"
            else:
                return self._indent(line) + f"local {name} = nil"

        # ── Arduino API rewrites ──
        line = self._rewrite_api(line)

        # ── Control flow ──
        # if (cond) {
        line = re.sub(r"\bif\s*\((.+?)\)\s*\{",
                      lambda m: "if " + self._convert_expr(m.group(1)) + " then", line)
        # } else if (cond) {
        line = re.sub(r"\}\s*else\s+if\s*\((.+?)\)\s*\{",
                      lambda m: "elseif " + self._convert_expr(m.group(1)) + " then", line)
        # } else {
        line = re.sub(r"\}\s*else\s*\{", "else", line)
        # closing brace alone
        line = re.sub(r"^\s*\}\s*$", "end", line)

        # while (cond) {
        line = re.sub(r"\bwhile\s*\((.+?)\)\s*\{",
                      lambda m: "while " + self._convert_expr(m.group(1)) + " do", line)

        # for (init; cond; step) { — basic numeric for
        for_m = re.search(r"\bfor\s*\(\s*(?:int\s+)?(\w+)\s*=\s*(\w+)\s*;\s*\1\s*(<[=]?|>[=]?)\s*(\w+)\s*;\s*\1\s*(\+\+|--|\+=\s*\w+|-=\s*\w+)\s*\)\s*\{", line)
        if for_m:
            var, start, op, limit, step_str = for_m.groups()
            if step_str in ("++", "+= 1"):
                step = "1"
            elif step_str in ("--", "-= 1"):
                step = "-1"
            else:
                m = re.match(r"[+-]=\s*(\w+)", step_str)
                step = m.group(1) if m else "1"
                if "-" in step_str:
                    step = "-" + step
            if "<=" in op:
                line = f"for {var} = {start}, {limit}, {step} do"
            elif "<" in op:
                line = f"for {var} = {start}, {limit} - 1, {step} do"
            else:
                line = f"for {var} = {start}, {limit}, {step} do"

        # ── Expression cleanup ──
        # Boolean literals
        line = re.sub(r"\btrue\b", "true", line)
        line = re.sub(r"\bfalse\b", "false", line)
        line = re.sub(r"\bnull\b|\bNULL\b", "nil", line)
        # && → and,  || → or,  ! → not
        line = re.sub(r"&&", " and ", line)
        line = re.sub(r"\|\|", " or ", line)
        line = re.sub(r"!\s*(?=[^=])", "not ", line)
        # != → ~=
        line = re.sub(r"!=", "~=", line)
        # Remove semicolons
        line = re.sub(r";(\s*)$", r"\1", line)
        # String concatenation: + on strings → ..  (heuristic)
        # Remove type casts: (int)x → x
        line = re.sub(r"\((?:int|float|double|bool|char|byte)\)\s*", "", line)

        return line.rstrip()

    def _rewrite_api(self, line: str) -> str:
        """Replace Arduino API calls with Lua equivalents."""
        # pinMode(pin, OUTPUT/INPUT)
        line = re.sub(r"\bpinMode\s*\(\s*(\w+)\s*,\s*OUTPUT\s*\)",
                      r"gpio.mode(\1, gpio.OUTPUT)", line)
        line = re.sub(r"\bpinMode\s*\(\s*(\w+)\s*,\s*INPUT_PULLUP\s*\)",
                      r"gpio.mode(\1, gpio.INPUT)", line)
        line = re.sub(r"\bpinMode\s*\(\s*(\w+)\s*,\s*INPUT\s*\)",
                      r"gpio.mode(\1, gpio.INPUT)", line)

        # digitalWrite(pin, HIGH/LOW)
        line = re.sub(r"\bdigitalWrite\s*\(\s*(\w+)\s*,\s*HIGH\s*\)",
                      r"gpio.write(\1, gpio.HIGH)", line)
        line = re.sub(r"\bdigitalWrite\s*\(\s*(\w+)\s*,\s*LOW\s*\)",
                      r"gpio.write(\1, gpio.LOW)", line)
        line = re.sub(r"\bdigitalWrite\s*\(\s*(\w+)\s*,\s*(\w+)\s*\)",
                      r"gpio.write(\1, \2)", line)

        # digitalRead(pin)
        line = re.sub(r"\bdigitalRead\s*\(", "gpio.read(", line)

        # analogRead(pin)
        line = re.sub(r"\banalogRead\s*\(", "gpio.analog_read(", line)

        # analogWrite (no direct equivalent — log a stub)
        line = re.sub(r"\banalogWrite\s*\((\w+),\s*(\w+)\)",
                      r"-- analogWrite(\1, \2)  -- not supported on ShellOS", line)

        # Serial.begin(baud)
        line = re.sub(r"\bSerial\.begin\s*\(.*?\)", "serial.begin()", line)
        # Serial.print/println
        line = re.sub(r"\bSerial\.println\s*\(", "serial.println(", line)
        line = re.sub(r"\bSerial\.print\s*\(", "serial.print(", line)

        # delay(ms)  millis()  micros()  — same in Lua
        # HIGH / LOW constants used standalone (skip if already prefixed with gpio.)
        line = re.sub(r"(?<!gpio\.)\bHIGH\b", "gpio.HIGH", line)
        line = re.sub(r"(?<!gpio\.)\bLOW\b",  "gpio.LOW",  line)

        return line

    def _convert_expr(self, expr: str) -> str:
        """Convert a C expression fragment to Lua."""
        expr = expr.strip()
        expr = re.sub(r"&&", " and ", expr)
        expr = re.sub(r"\|\|", " or ", expr)
        expr = re.sub(r"!\s*(?=[^=])", "not ", expr)
        expr = re.sub(r"!=", "~=", expr)
        expr = re.sub(r"\btrue\b", "true", expr)
        expr = re.sub(r"\bfalse\b", "false", expr)
        return expr

    def _convert_literal(self, val: str) -> str:
        """Convert a #define value to Lua."""
        val = val.split("//")[0].strip()  # strip inline comments
        val = re.sub(r"&&", " and ", val)
        val = re.sub(r"\|\|", " or ", val)
        val = re.sub(r"\bHIGH\b", "1", val)
        val = re.sub(r"\bLOW\b",  "0", val)
        return val

    @staticmethod
    def _indent(line: str) -> str:
        """Preserve leading whitespace."""
        return re.match(r"^(\s*)", line).group(1)


# ─────────────────────────────────────────────────────────────────────────────
# Build command
# ─────────────────────────────────────────────────────────────────────────────

def cmd_build(args):
    ino_path = args.sketch
    if not os.path.isfile(ino_path):
        print(f"[ERROR] File not found: {ino_path}", file=sys.stderr)
        return 1

    # Determine package name from filename (strip .ino)
    base = os.path.basename(ino_path)
    pkg_name = re.sub(r"\.ino$", "", base, flags=re.IGNORECASE)
    pkg_name = re.sub(r"[^a-zA-Z0-9_\-]", "_", pkg_name)

    print(f"[BUILD] Package: {pkg_name}")

    # Transpile
    with open(ino_path, "r", encoding="utf-8") as f:
        source = f.read()

    transpiler = ArduinoTranspiler()
    lua_code = transpiler.transpile(source)

    print(f"[BUILD] Transpiled {len(source)} bytes → {len(lua_code)} bytes Lua")

    # Manifest
    manifest = {
        "name":        pkg_name,
        "version":     getattr(args, "version", "1.0.0"),
        "description": getattr(args, "description", f"Transpiled from {base}"),
        "author":      getattr(args, "author", ""),
        "entry":       "main.lua",
        "autorun":     False,
    }
    manifest_json = json.dumps(manifest, indent=2).encode("utf-8")

    # Write .shpkg
    out_dir = args.out if args.out else os.path.dirname(ino_path) or "."
    os.makedirs(out_dir, exist_ok=True)
    shpkg_path = os.path.join(out_dir, f"{pkg_name}.shpkg")

    files = {
        "manifest.json": manifest_json,
        "main.lua":      lua_code.encode("utf-8"),
    }
    write_shpkg(shpkg_path, files)

    size = os.path.getsize(shpkg_path)
    print(f"[BUILD] Written: {shpkg_path} ({size} bytes)")
    print(f"[BUILD] Done. Upload with:")
    print(f"         python shellos_compiler.py upload <device-ip> {shpkg_path}")
    return 0


# ─────────────────────────────────────────────────────────────────────────────
# HTTP helpers
# ─────────────────────────────────────────────────────────────────────────────

def _api(ip: str, method: str, path: str, body: bytes | None = None, timeout: int = 30):
    url = f"http://{ip}:8080{path}"
    req = urllib.request.Request(url, data=body, method=method)
    if body:
        req.add_header("Content-Type", "application/octet-stream")
    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            return resp.status, resp.read().decode("utf-8", errors="replace")
    except urllib.error.HTTPError as e:
        return e.code, e.read().decode("utf-8", errors="replace")
    except Exception as e:
        return -1, str(e)


def cmd_upload(args):
    ip       = args.device_ip
    shpkg    = args.package
    if not os.path.isfile(shpkg):
        print(f"[ERROR] File not found: {shpkg}", file=sys.stderr)
        return 1

    with open(shpkg, "rb") as f:
        data = f.read()

    print(f"[UPLOAD] Sending {len(data)} bytes to {ip}:8080/pkg/upload ...")
    status, body = _api(ip, "POST", "/pkg/upload", data, timeout=60)
    if status == 200:
        try:
            j = json.loads(body)
            print(f"[UPLOAD] {j.get('message', 'OK')}")
        except Exception:
            print(f"[UPLOAD] OK — {body}")
        return 0
    else:
        print(f"[UPLOAD] FAILED ({status}): {body}", file=sys.stderr)
        return 1


def cmd_run(args):
    status, body = _api(args.device_ip, "POST", f"/pkg/run/{args.name}")
    if status == 200:
        print(f"[RUN] {args.name}: started")
    else:
        print(f"[RUN] Failed ({status}): {body}", file=sys.stderr)
    return 0 if status == 200 else 1


def cmd_stop(args):
    status, body = _api(args.device_ip, "POST", f"/pkg/stop/{args.name}")
    if status == 200:
        print(f"[STOP] {args.name}: stopped")
    else:
        print(f"[STOP] Failed ({status}): {body}", file=sys.stderr)
    return 0 if status == 200 else 1


def cmd_list(args):
    status, body = _api(args.device_ip, "GET", "/pkg/list")
    if status != 200:
        print(f"[LIST] Failed ({status}): {body}", file=sys.stderr)
        return 1
    try:
        packages = json.loads(body)
    except Exception:
        print(body)
        return 0

    if not packages:
        print("(no packages installed)")
        return 0

    print(f"\n  {'Name':<20}  {'Version':<12}  Status")
    print("  " + "─" * 50)
    for p in packages:
        running = "● running" if p.get("running") else "○ stopped"
        print(f"  {p.get('name','?'):<20}  {p.get('version','?'):<12}  {running}")
    print()
    return 0


def cmd_logs(args):
    # Logs are served via shell; for now, just a reminder
    print(f"[LOGS] Use the ShellOS TCP shell: pkg logs {args.name}")
    print(f"       Or: netcat {args.device_ip} 2323  →  pkg logs {args.name}")
    return 0


# ─────────────────────────────────────────────────────────────────────────────
# CLI parser
# ─────────────────────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(
        prog="shellos_compiler",
        description="ShellOS Package Compiler — Arduino C → Lua + deploy"
    )
    sub = parser.add_subparsers(dest="command", required=True)

    # build
    p_build = sub.add_parser("build", help="Transpile .ino → .shpkg")
    p_build.add_argument("sketch", help="Path to .ino file")
    p_build.add_argument("--out",         default=None,    help="Output directory (default: same as .ino)")
    p_build.add_argument("--version",     default="1.0.0", help="Package version string")
    p_build.add_argument("--description", default="",      help="Package description")
    p_build.add_argument("--author",      default="",      help="Package author")

    # upload
    p_upload = sub.add_parser("upload", help="Upload .shpkg to device over WiFi")
    p_upload.add_argument("device_ip", help="Device IP address")
    p_upload.add_argument("package",   help="Path to .shpkg file")

    # run
    p_run = sub.add_parser("run", help="Start a package on the device")
    p_run.add_argument("device_ip")
    p_run.add_argument("name", help="Package name")

    # stop
    p_stop = sub.add_parser("stop", help="Stop a running package")
    p_stop.add_argument("device_ip")
    p_stop.add_argument("name", help="Package name")

    # list
    p_list = sub.add_parser("list", help="List installed packages")
    p_list.add_argument("device_ip")

    # logs
    p_logs = sub.add_parser("logs", help="Show package logs")
    p_logs.add_argument("device_ip")
    p_logs.add_argument("name", help="Package name")

    args = parser.parse_args()

    dispatch = {
        "build":  cmd_build,
        "upload": cmd_upload,
        "run":    cmd_run,
        "stop":   cmd_stop,
        "list":   cmd_list,
        "logs":   cmd_logs,
    }
    sys.exit(dispatch[args.command](args))


if __name__ == "__main__":
    main()
