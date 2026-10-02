#!/usr/bin/env python3
"""
Generates the IAR EWARM project (EWARM/SolderStation.eww/.ewp/.ewd/.ewt).

The files are produced from templates in tools/iar_template/ that were saved
by IAR EWARM 9.70 (project format 4), so the project opens in that version
and newer without conversion. Only the configuration names, output paths,
preprocessor symbols, include paths, optimization level and the file list
are replaced; every other option keeps the template value.

Run again after adding or removing source files:
    python3 tools/gen_iar.py
"""
import glob
import os
import re

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
TPL = os.path.join(ROOT, "tools", "iar_template")
OUT = os.path.join(ROOT, "EWARM")
NAME = "SolderStation"
TPL_CONFIG = "BlackPill_F401"

DEFINES = ["STM32F401xC", "HSE_VALUE=25000000"]
INCLUDES = [r"$PROJ_DIR$\..\Core\Inc",
            r"$PROJ_DIR$\..\Drivers\CMSIS\Include",
            r"$PROJ_DIR$\..\Drivers\CMSIS\Device\ST\STM32F4xx\Include"]

# name -> optimization level (0 none, 1 low, 2 medium, 3 high)
CONFIGS = [("Debug", 1), ("Release", 3)]


def read(name):
    with open(os.path.join(TPL, name), encoding="utf-8", newline="") as f:
        return f.read().replace("\r\n", "\n")


def write(name, text):
    with open(os.path.join(OUT, name), "w", encoding="utf-8", newline="\r\n") as f:
        f.write(text)


def set_option(block, name, states):
    """Replaces the <state> values of one <option>, keeps its <version>."""
    pattern = re.compile(
        r"(?P<indent>[ \t]*)<option>\n"
        r"(?P<ind2>[ \t]*)<name>" + re.escape(name) + r"</name>\n"
        r"(?P<ver>(?:[ \t]*<version>[^<]*</version>\n)?)"
        r"(?:[ \t]*<state>[^<]*</state>\n|[ \t]*<state\s*/>\n)*"
        r"(?P=indent)</option>")
    m = pattern.search(block)
    if m is None:
        raise KeyError(f"option {name} not found in template")
    ind2 = m.group("ind2")
    body = "".join(f"{ind2}<state>{s}</state>\n" for s in states)
    new = f"{m.group('indent')}<option>\n{ind2}<name>{name}</name>\n{m.group('ver')}{body}{m.group('indent')}</option>"
    return block[:m.start()] + new + block[m.end():]


def split(text):
    """Returns (head, [configuration blocks], tail after the configurations)."""
    starts = [m.start() for m in re.finditer(r"[ \t]*<configuration>\n", text)]
    ends = [m.end() for m in re.finditer(r"[ \t]*</configuration>\n", text)]
    head = text[:starts[0]]
    blocks = [text[s:e] for s, e in zip(starts, ends)]
    tail = text[ends[-1]:]
    return head, blocks, tail


def file_groups(indent="    "):
    app = sorted(glob.glob(os.path.join(ROOT, "Core", "Src", "*.c")))
    app = [f for f in app if not f.endswith("system_stm32f4xx.c")]

    def rel(p):
        return "$PROJ_DIR$\\..\\" + os.path.relpath(p, ROOT).replace("/", "\\")

    def group(name, files, subgroups="", level=1):
        i = indent * level
        s = f"{i}<group>\n{i}{indent}<name>{name}</name>\n{subgroups}"
        for f in files:
            s += f"{i}{indent}<file>\n{i}{indent}{indent}<name>{f}</name>\n{i}{indent}</file>\n"
        return s + f"{i}</group>\n"

    out = group("Application", [rel(f) for f in app])
    out += group("EWARM", [r"$PROJ_DIR$\startup_stm32f401xc.s"])
    out += group("Drivers", [], group("CMSIS", [r"$PROJ_DIR$\..\Core\Src\system_stm32f4xx.c"], level=2))
    out += group("Doc", [r"$PROJ_DIR$\..\README.md"])
    return out


def replace_groups(tail):
    """Drops the template's <group> elements and inserts ours."""
    first = tail.find("    <group>")
    if first < 0:
        return tail
    # everything after the last top level </group>
    last = tail.rfind("    </group>\n")
    rest = tail[last + len("    </group>\n"):]
    return tail[:first] + file_groups() + rest


def gen_ewp():
    head, blocks, tail = split(read("template.ewp"))
    tpl = blocks[0]
    out = []
    for cfg, opt in CONFIGS:
        b = tpl.replace(f"<name>{TPL_CONFIG}</name>", f"<name>{cfg}</name>", 1)
        for key in ("BrowseInfoPath", "ExePath", "ObjPath", "ListPath"):
            sub = {"BrowseInfoPath": "BrowseInfo", "ExePath": "Exe", "ObjPath": "Obj", "ListPath": "List"}[key]
            b = set_option(b, key, [f"{cfg}\\{sub}"])
        b = set_option(b, "BuildFilesPath", [cfg])
        b = set_option(b, "CCDefines", DEFINES)
        b = set_option(b, "CCIncludePath2", INCLUDES)
        b = set_option(b, "CCDiagSuppress", ["Pa082"])
        b = set_option(b, "CCOptLevel", [str(opt)])
        b = set_option(b, "CCOptLevelSlave", [str(opt)])
        b = set_option(b, "OOCOutputFile", [f"{NAME}.hex"])
        b = set_option(b, "IlinkOutputFile", [f"{NAME}.out"])
        b = set_option(b, "IlinkIcfFile", [r"$PROJ_DIR$\stm32f401xc_flash.icf"])
        if cfg == "Release":
            b = b.replace("<debug>1</debug>", "<debug>0</debug>")
        out.append(b)
    write(NAME + ".ewp", head + "".join(out) + replace_groups(tail))


def gen_ewd():
    head, blocks, tail = split(read("template.ewd"))
    tpl = blocks[0]
    out = []
    for cfg, _ in CONFIGS:
        b = tpl.replace(f"<name>{TPL_CONFIG}</name>", f"<name>{cfg}</name>", 1)
        # 84 MHz core clock (SWO timing of the J-Link / ST-Link drivers)
        b = b.replace("<state>72.0</state>", "<state>84.0</state>")
        if cfg == "Release":
            b = b.replace("<debug>1</debug>", "<debug>0</debug>")
        out.append(b)
    write(NAME + ".ewd", head + "".join(out) + tail)


def gen_ewt():
    head, blocks, tail = split(read("template.ewt"))
    tpl = blocks[0]
    out = []
    for cfg, _ in CONFIGS:
        b = tpl.replace(f"<name>{TPL_CONFIG}</name>", f"<name>{cfg}</name>", 1)
        b = b.replace(f"{TPL_CONFIG}/C-STAT", f"{cfg}/C-STAT")
        if cfg == "Release":
            b = b.replace("<debug>1</debug>", "<debug>0</debug>")
        out.append(b)
    write(NAME + ".ewt", head + "".join(out) + replace_groups(tail))


def gen_eww():
    write(NAME + ".eww", read("template.eww").replace("USB_Audio_DAC", NAME))


if __name__ == "__main__":
    gen_ewp()
    gen_ewd()
    gen_ewt()
    gen_eww()
    print("IAR project written to", OUT)
