#!/usr/bin/env python3
"""
Generates the IAR EWARM project (EWARM/SolderStation.ewp / .ewd / .eww)
from the source tree. Run again after adding or removing source files.
"""
import glob
import os

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
EW = os.path.join(ROOT, "EWARM")
NAME = "SolderStation"
DEVICE = "STM32F401CC\tST STM32F401CC"

DEFINES = ["STM32F401xC", "HSE_VALUE=25000000"]
INCLUDES = [r"$PROJ_DIR$\..\Core\Inc",
            r"$PROJ_DIR$\..\Drivers\CMSIS\Include",
            r"$PROJ_DIR$\..\Drivers\CMSIS\Device\ST\STM32F4xx\Include"]


def opt(name, states, version=None):
    if not isinstance(states, (list, tuple)):
        states = [states]
    s = "                <option>\n                    <name>%s</name>\n" % name
    if version is not None:
        s += "                    <version>%d</version>\n" % version
    for st in states:
        s += "                    <state>%s</state>\n" % st
    s += "                </option>\n"
    return s


def settings(name, archive, version, options, debug):
    return ("            <settings>\n"
            "                <name>%s</name>\n"
            "                <archiveVersion>%d</archiveVersion>\n"
            "                <data>\n"
            "                <version>%d</version>\n"
            "                <wantNonLocal>1</wantNonLocal>\n"
            "                <debug>%d</debug>\n%s"
            "                </data>\n"
            "            </settings>\n") % (name, archive, version, debug, options)


def ewp_config(cfg, debug, opt_level):
    general = "".join([
        opt("ExePath", cfg + r"\Exe"),
        opt("ObjPath", cfg + r"\Obj"),
        opt("ListPath", cfg + r"\List"),
        opt("OGCoreOrChip", 1),
        opt("OGChipSelectEditMenu", DEVICE),
        opt("GFPUDeviceSlave", DEVICE),
        opt("CoreVariant", 39, 26),
        opt("GBECoreSlave", 39, 26),
        opt("GFPUCoreSlave2", 39, 26),
        opt("FPU2", 4, 0),
        opt("NrRegs", 0, 0),
        opt("GEndianMode", 0),
        opt("GRuntimeLibSelect", 1, 0),
        opt("GRuntimeLibSelectSlave", 1, 0),
        opt("RTConfigPath2", r"$TOOLKIT_DIR$\inc\c\DLib_Config_Normal.h"),
        opt("OGPrintfVariant", 1, 0),
        opt("OGScanfVariant", 1, 0),
        opt("OGUseCmsis", 0),
        opt("OGUseCmsisDspLib", 0),
        opt("GenLowLevelInterface", 0),
        opt("DSPExtension", 1),
    ])
    iccarm = "".join([
        opt("CCDefines", DEFINES),
        opt("CCOptLevel", opt_level),
        opt("CCOptLevelSlave", opt_level),
        opt("CCOptStrategy", 1 if not debug else 0, 0),
        opt("CCIncludePath2", INCLUDES),
        opt("IccLang", 0),
        opt("IccCDialect", 1),
        opt("IccAllowVLA", 0),
        opt("IccLanguageConformance", 0),
        opt("IccCharIs", 1),
        opt("IccFloatSemantics", 0),
        opt("CCDiagSuppress", "Pa082,Pa089"),
        opt("CCDebugInfo", 1),
    ])
    aarm = "".join([
        opt("AUserIncludes", INCLUDES),
        opt("ADebug", 1),
    ])
    objcopy = "".join([
        opt("OOCOutputFormat", 1, 3),
        opt("OCOutputOverride", 0),
        opt("OOCOutputFile", NAME + ".hex"),
        opt("OOCCommandLineProducer", 1),
        opt("OOCObjCopyEnable", 1),
    ])
    ilink = "".join([
        opt("IlinkOutputFile", NAME + ".out"),
        opt("IlinkIcfOverride", 1),
        opt("IlinkIcfFile", r"$PROJ_DIR$\stm32f401xc_flash.icf"),
        opt("IlinkProgramEntryLabelSelect", 0),
        opt("IlinkProgramEntryLabel", "__iar_program_start"),
        opt("IlinkMapFile", 1),
        opt("IlinkLogFile", 0),
        opt("IlinkDebugInfoEnable", 1),
    ])
    return ("    <configuration>\n"
            "        <name>%s</name>\n"
            "        <toolchain>\n            <name>ARM</name>\n        </toolchain>\n"
            "        <debug>%d</debug>\n%s%s%s%s%s"
            "    </configuration>\n") % (
        cfg, debug,
        settings("General", 3, 35, general, debug),
        settings("ICCARM", 2, 37, iccarm, debug),
        settings("AARM", 2, 11, aarm, debug),
        settings("OBJCOPY", 0, 1, objcopy, debug),
        settings("ILINK", 0, 25, ilink, debug))


def group(name, files):
    s = "    <group>\n        <name>%s</name>\n" % name
    for f in files:
        s += "        <file>\n            <name>%s</name>\n        </file>\n" % f
    s += "    </group>\n"
    return s


def rel(p):
    return "$PROJ_DIR$\\..\\" + os.path.relpath(p, ROOT).replace("/", "\\")


def main():
    app = sorted(glob.glob(os.path.join(ROOT, "Core", "Src", "*.c")))
    app = [f for f in app if not f.endswith("system_stm32f4xx.c")]
    inc = sorted(glob.glob(os.path.join(ROOT, "Core", "Inc", "*.h")))

    ewp = '<?xml version="1.0" encoding="UTF-8"?>\n<project>\n    <fileVersion>3</fileVersion>\n'
    ewp += ewp_config("Debug", 1, 1)
    ewp += ewp_config("Release", 0, 3)
    ewp += group("Application", [rel(f) for f in app])
    ewp += group("Include", [rel(f) for f in inc])
    ewp += group("CMSIS", [r"$PROJ_DIR$\startup_stm32f401xc.s",
                           r"$PROJ_DIR$\..\Core\Src\system_stm32f4xx.c"])
    ewp += "</project>\n"

    cspy = "".join([
        opt("CInput", 1),
        opt("CEndian", 1),
        opt("CProcessor", 1),
        opt("OCVariant", 0),
        opt("MacOverride", 0),
        opt("MemOverride", 0),
        opt("RunToEnable", 1),
        opt("RunToName", "main"),
        opt("CExtraOptionsCheck", 0),
        opt("DdfFileName", r"$TOOLKIT_DIR$\config\debugger\ST\STM32F401CC.ddf"),
        opt("OCDownloadSuppressDownload", 0),
        opt("OCDownloadVerifyAll", 1),
        opt("UseFlashLoader", 1),
        opt("CLowLevel", 1),
        opt("OCBE8Slave", 1),
        opt("OCDynDriverList", "STLINK_ID"),
        opt("OverrideDefFlashBoard", 0),
        opt("FlashLoadersV3", r"$TOOLKIT_DIR$\config\flashloader\ST\FlashSTM32F401xC.board"),
    ])
    stlink = "".join([
        opt("CCSTLinkInterfaceRadio", 1),       # SWD
        opt("CCSTLinkInterfaceCmdLine", 0),
        opt("CCSTLinkResetList", 0, 1),         # normal reset
        opt("CCCpuClockEdit", "84.0"),
        opt("CCSwoClockAuto", 0),
        opt("CCSwoClockEdit", 2000),
    ])
    ewd = '<?xml version="1.0" encoding="UTF-8"?>\n<project>\n    <fileVersion>3</fileVersion>\n'
    for cfg, dbg in (("Debug", 1), ("Release", 0)):
        ewd += ("    <configuration>\n        <name>%s</name>\n"
                "        <toolchain>\n            <name>ARM</name>\n        </toolchain>\n"
                "        <debug>%d</debug>\n%s%s    </configuration>\n") % (
            cfg, dbg, settings("C-SPY", 2, 32, cspy, dbg), settings("STLINK_ID", 3, 7, stlink, dbg))
    ewd += "</project>\n"

    eww = ('<?xml version="1.0" encoding="UTF-8"?>\n<workspace>\n    <project>\n'
           '        <path>$WS_DIR$\\%s.ewp</path>\n    </project>\n    <batchBuild />\n</workspace>\n') % NAME

    for ext, text in (("ewp", ewp), ("ewd", ewd), ("eww", eww)):
        with open(os.path.join(EW, NAME + "." + ext), "w", newline="\r\n") as f:
            f.write(text)
    print("IAR project written to", EW)


if __name__ == "__main__":
    main()
