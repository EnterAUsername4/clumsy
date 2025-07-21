#pragma once

typedef struct {
    DWORD vk;
    const char* name;
} KeyNameMap;

static KeyNameMap keyMap[] = {
    { VK_CAPITAL, "CAPSLOCK" },
    { VK_F1, "F1" }, { VK_F2, "F2" }, { VK_F3, "F3" }, { VK_F4, "F4" },
    { VK_F5, "F5" }, { VK_F6, "F6" }, { VK_F7, "F7" }, { VK_F8, "F8" },
    { VK_F9, "F9" }, { VK_F10, "F10" }, { VK_F11, "F11" }, { VK_F12, "F12" },
    { VK_LEFT, "LEFT" }, { VK_RIGHT, "RIGHT" }, { VK_UP, "UP" }, { VK_DOWN, "DOWN" },
    { VK_RETURN, "ENTER" }, { VK_ESCAPE, "ESC" }, { VK_SPACE, "SPACE" },
    { VK_TAB, "TAB" }, { VK_BACK, "BACKSPACE" }, { VK_LCONTROL, "LCTRL" },
    { VK_RCONTROL, "RCTRL" }, { VK_LSHIFT, "LSHIFT" }, { VK_RSHIFT, "RSHIFT" },
    { VK_LMENU, "LALT" }, { VK_RMENU, "RALT" },
    { VK_XBUTTON1, "MOUSE4" }, { VK_XBUTTON2, "MOUSE5" },
    { VK_NUMPAD0, "NUM_0" }, { VK_NUMPAD1, "NUM_1" }, { VK_NUMPAD2, "NUM_2" },
    { VK_NUMPAD3, "NUM_3" }, { VK_NUMPAD4, "NUM_4" }, { VK_NUMPAD5, "NUM_5" },
    { VK_NUMPAD6, "NUM_6" }, { VK_NUMPAD7, "NUM_7" }, { VK_NUMPAD8, "NUM_8" },
    { VK_NUMPAD9, "NUM_9" },
    { 'A', "A" }, { 'B', "B" }, { 'C', "C" }, { 'D', "D" }, { 'E', "E" }, { 'F', "F" },
    { 'G', "G" }, { 'H', "H" }, { 'I', "I" }, { 'J', "J" }, { 'K', "K" }, { 'L', "L" },
    { 'M', "M" }, { 'N', "N" }, { 'O', "O" }, { 'P', "P" }, { 'Q', "Q" }, { 'R', "R" },
    { 'S', "S" }, { 'T', "T" }, { 'U', "U" }, { 'V', "V" }, { 'W', "W" }, { 'X', "X" },
    { 'Y', "Y" }, { 'Z', "Z" }, { 0, NULL }
};

#define CONFIG_KEYBINDS_FILE "keybinds.txt"
#define CONFIG_KEYBIND_MAX_RECORDS 64
#define CONFIG_KEYBIND_BUF_SIZE 4096
typedef struct {
    char* key;
} filterSizeKey;
UINT filtersSizeKey;
char configBuf[CONFIG_KEYBIND_BUF_SIZE+2];

DWORD actionKeybinds[ACTION_COUNT] = {
    VK_CAPITAL, // Toggle Filters default to CAPSLOCK [Raw Key Code](20)
};