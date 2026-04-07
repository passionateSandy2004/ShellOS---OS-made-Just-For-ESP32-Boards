#!/usr/bin/env python3
"""
Download and extract Lua 5.4.7 source into components/lua_runtime/lua/
Run this once before building ShellOS with the package system:
    python tools/get_lua.py
"""
import os, sys, tarfile, urllib.request, shutil

LUA_VERSION = "5.4.7"
LUA_URL = f"https://www.lua.org/ftp/lua-{LUA_VERSION}.tar.gz"
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJ_ROOT  = os.path.dirname(SCRIPT_DIR)
LUA_DEST   = os.path.join(PROJ_ROOT, "components", "lua_runtime", "lua")

CORE_SRCS = [
    "lapi.c","lcode.c","lctype.c","ldebug.c","ldo.c","ldump.c",
    "lfunc.c","lgc.c","linit.c","llex.c","lmem.c","lobject.c",
    "lopcodes.c","lparser.c","lstate.c","lstring.c","ltable.c",
    "ltm.c","lundump.c","lvm.c","lzio.c",
    "lauxlib.c","lbaselib.c","lcorolib.c","lmathlib.c",
    "lstrlib.c","ltablib.c","lutf8lib.c",
]
# Not included: liolib.c (use our file API), loslib.c, ldblib.c, loadlib.c

def main():
    if os.path.isfile(os.path.join(LUA_DEST, "lvm.c")):
        print(f"[OK] Lua source already present at {LUA_DEST}")
        return 0

    os.makedirs(LUA_DEST, exist_ok=True)
    tarball = os.path.join(PROJ_ROOT, f"lua-{LUA_VERSION}.tar.gz")

    if not os.path.isfile(tarball):
        print(f"Downloading Lua {LUA_VERSION} from {LUA_URL} ...")
        try:
            urllib.request.urlretrieve(LUA_URL, tarball)
        except Exception as e:
            print(f"[FAIL] Download error: {e}")
            return 1
    else:
        print(f"Found existing tarball: {tarball}")

    print("Extracting ...")
    with tarfile.open(tarball, "r:gz") as tf:
        lua_dir_in_tar = f"lua-{LUA_VERSION}/src/"
        for member in tf.getmembers():
            if not member.name.startswith(lua_dir_in_tar):
                continue
            filename = os.path.basename(member.name)
            if filename in CORE_SRCS or (filename.endswith(".h") and filename not in ("lua.hpp",)):
                member.name = filename
                tf.extract(member, LUA_DEST)

    # Remove downloaded tarball to keep repo clean
    try:
        os.remove(tarball)
    except OSError:
        pass

    found = [f for f in CORE_SRCS if os.path.isfile(os.path.join(LUA_DEST, f))]
    print(f"[OK] Extracted {len(found)}/{len(CORE_SRCS)} Lua source files to {LUA_DEST}")
    if len(found) < len(CORE_SRCS):
        missing = [f for f in CORE_SRCS if f not in found]
        print(f"[WARN] Missing: {missing}")
    return 0

if __name__ == "__main__":
    sys.exit(main())
