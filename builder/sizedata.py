import re

Import("env")


def pioSizeIsRamSectionCustom(env, section):
    if section and re.search(
        r"\.dram0\.data|\.dram0\.bss|\.noinit", section.get("name", "")
    ):
        return True

    return False


def pioSizeIsFlashectionCustom(env, section):
    if section and re.search(
        r"\.iram0\.text|\.iram0\.vectors|\.dram0\.data|\.flash\.text|\.flash\.rodata|\.flash\.appdesc",
        section.get("name", ""),
    ):
        return True

    return False


env.AddMethod(pioSizeIsRamSectionCustom, "pioSizeIsRamSection")
env.AddMethod(pioSizeIsFlashectionCustom, "pioSizeIsFlashSection")
