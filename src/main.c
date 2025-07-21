#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <Windows.h>
#include <conio.h>
#include "iup.h"
#include "drop.h"
#include "lag.h"
#include "disconnect.h"
#include "common.h"
#include "keycode.h"
extern void setup_crash_log_handler(void);

// ! the order decides which module get processed first
Module* modules[MODULE_CNT] = {
    &lagModule,
    &dropModule,
    &throttleModule,
    &dupModule,
    &oodModule,
    &tamperModule,
	&disconnectModule,
    &resetModule,
	&bandwidthModule,
};

volatile BOOL isFiltering = FALSE;
volatile short sendState = SEND_STATUS_NONE;

// rainbow is enabled
static int rainbowModeEnabled = 1;  // 1 = rainbow on, 0 = off
static Ihandle* rainbowTimer = NULL;
static Ihandle* rainbowCheckbox;

// global iup handlers
static Ihandle *dialog, *topFrame, *bottomFrame; 
static Ihandle *statusLabel;
static Ihandle *filterText, *filterButton;
static Ihandle *filterSelectList;
static Ihandle *tabs;
static Ihandle *groupBox, *toggle, *controls, *icon;
// timer to update icons
static Ihandle *stateIcon;
static Ihandle *timer, *timer2;
static Ihandle *timeout = NULL;
// iup theme handler
static Ihandle *themeList;
static Ihandle *themeLabel;

// iup box handler
static Ihandle *keybindVBox;
static Ihandle *topVbox, *bottomVbox, *dialogVBox, *controlHbox;
static Ihandle *noneIcon, *doingIcon, *errorIcon;

// iup label handler
static Ihandle* label;

// iup misc
static Ihandle* dlg;

static Ihandle* hbox;
static Ihandle* text;

static Ihandle* child;

void showStatus(const char *line);
static int KEYPRESS_CB(Ihandle *ih, int c, int press);
static int uiOnDialogShow(Ihandle *ih, int state);
static int uiStopCb(Ihandle *ih);
static int uiStartCb(Ihandle *ih);
static int uiTimerCb(Ihandle *ih);
static int uiTimeoutCb(Ihandle *ih);
static int uiListSelectCb(Ihandle *ih, char *text, int item, int state);
static int uiFilterTextCb(Ihandle *ih);
static void uiSetupModule(Module *module, Ihandle *parent);

// serializing config files using a stupid custom format
#define CONFIG_FILE "config.txt"
#define CONFIG_MAX_RECORDS 64
#define CONFIG_BUF_SIZE 4096
typedef struct {
    char* filterName;
    char* filterValue;
} filterRecord;
UINT filtersSize;
filterRecord filters[CONFIG_MAX_RECORDS] = {0};
char configBuf[CONFIG_BUF_SIZE+2]; // add some padding to write \n
BOOL parameterized = 0; // parameterized flag, means reading args from command line

const char* actions[] = {
        "Toggle Filters",
        NULL
};

// loading up filters and fill in
void loadConfig() {
    char path[MSG_BUFSIZE];
    char *p;
    FILE *f;
    GetModuleFileName(NULL, path, MSG_BUFSIZE);
    LOG("Executable path: %s", path);
    p = strrchr(path, '\\');
    if (p == NULL) p = strrchr(path, '/'); // holy shit
    strcpy(p+1, CONFIG_FILE);
    LOG("Config path: %s", path);
    f = fopen(path, "r");
    if (f) {
        size_t len;
        char *current, *last;
        len = fread(configBuf, sizeof(char), CONFIG_BUF_SIZE, f);
        if (len == CONFIG_BUF_SIZE) {
            LOG("Config file is larger than %d bytes, get truncated.", CONFIG_BUF_SIZE);
        }
        // always patch in a newline at the end to ease parsing
        configBuf[len] = '\n';
        configBuf[len+1] = '\0';

        // parse out the kv pairs. isn't quite safe
        filtersSize = 0;
        last = current = configBuf;
        do {
            // eat up empty lines
EAT_SPACE:  while (isspace(*current)) { ++current; }
            if (*current == '#') {
                current = strchr(current, '\n');
                if (!current) break;
                current = current + 1;
                goto EAT_SPACE;
            }

            // now we can start
            last = current;
            current = strchr(last, ':');
            if (!current) break;
            *current = '\0';
            filters[filtersSize].filterName = last;
            current += 1;
            while (isspace(*current)) { ++current; } // eat potential space after :
            last = current;
            current = strchr(last, '\n');
            if (!current) break;
            filters[filtersSize].filterValue = last;
            *current = '\0';
            if (*(current-1) == '\r') *(current-1) = 0;
            last = current = current + 1;
            ++filtersSize;
        } while (last && last - configBuf < CONFIG_BUF_SIZE);
        LOG("Loaded %u records.", filtersSize);
    }

    if (!f || filtersSize == 0) {
        LOG("Failed to load from config. Fill in a simple one.");
        // config is missing or ill-formed. fill in some simple ones
        filters[filtersSize].filterName = "loopback packets";
        filters[filtersSize].filterValue = "outbound and ip.DstAddr >= 127.0.0.1 and ip.DstAddr <= 127.255.255.255";
        filtersSize = 1;
    }
}

DWORD NameToVkCode(const char* name) {
    for (int i = 0; keyMap[i].name != NULL; ++i) {
        if (_stricmp(name, keyMap[i].name) == 0)
            return keyMap[i].vk;
    }
    return 0; // unknown
}

const char* VkCodeToName(DWORD vk) {
    for (int i = 0; keyMap[i].name != NULL; ++i) {
        if (keyMap[i].vk == vk)
            return keyMap[i].name;
    }
    return "UNKNOWN";
}

LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION) {
        KBDLLHOOKSTRUCT *pKeyBoard = (KBDLLHOOKSTRUCT *)lParam;
        DWORD pressedKey = pKeyBoard->vkCode;

        if (wParam == WM_KEYDOWN) {
            if (pressedKey == actionKeybinds[0]) {  // Toggle Filters
                if (isFiltering) {
                    uiStopCb(NULL);
                    isFiltering = FALSE;
                } else {
                    uiStartCb(NULL);
                    isFiltering = TRUE;
                }
            }
        }
    }

    return CallNextHookEx(NULL, nCode, wParam, lParam);
}

void LoadKeybindsFromFile() {
    FILE* f = fopen(CONFIG_KEYBINDS_FILE, "r");
    if (!f) return;

    char line[64];
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = 0; // strip newline
        char* eq = strchr(line, '=');
        if (!eq) continue;

        *eq = 0;
        const char* key = line;
        const char* val = eq + 1;

        if (strcmp(key, "ToggleFilters") == 0) {
            DWORD vk = NameToVkCode(val);
            if (vk != 0)
                actionKeybinds[0] = vk;
        }
    }

    fclose(f);
}

void SaveKeybindsToFile() {
    FILE* f = fopen(CONFIG_KEYBINDS_FILE, "w");
    if (!f) return;

    const char* name = VkCodeToName(actionKeybinds[0]);  // Only ToggleFilters
    fprintf(f, "ToggleFilters=%s\n", name);

    fclose(f);
}

void HSVtoRGB(double h, double s, double v, double *r, double *g, double *b) {
    int sector = (int)(h * 6.0);
    double f = h * 6.0 - sector;
    double p = v * (1.0 - s);
    double q = v * (1.0 - f * s);
    double t = v * (1.0 - (1.0 - f) * s);

    switch (sector % 6) {
        case 0: *r = v; *g = t; *b = p; break;
        case 1: *r = q; *g = v; *b = p; break;
        case 2: *r = p; *g = v; *b = t; break;
        case 3: *r = p; *g = q; *b = v; break;
        case 4: *r = t; *g = p; *b = v; break;
        case 5: *r = v; *g = p; *b = q; break;
    }
}

void toggleRainbowMode(int enable) {
    rainbowModeEnabled = enable;
    if (rainbowTimer)
        IupSetAttribute(rainbowTimer, "RUN", enable ? "YES" : "NO");

    if (!enable) {
        // Restore theme text color
        const char* fg = IupGetAttribute(dialogVBox, "FGCOLOR");  // fallback to theme
        IupSetAttribute(statusLabel, "FGCOLOR", fg);
        IupSetAttribute(bottomFrame, "FGCOLOR", fg);
    }
}

static int uiRainbowTextColorCb(Ihandle *ih) {
    static double hue = 0.0;
    double r, g, b;
    char colorStr[20];

    // Smaller increment for smoother transition
    hue += 0.002;  // smoother than 0.02
    if (hue >= 1.0) hue -= 1.0;

    // Use full saturation and value for vivid color cycling
    HSVtoRGB(hue, 1.0, 1.0, &r, &g, &b);

    // Convert to RGB and set UI attributes
    sprintf(colorStr, "%d %d %d", (int)(r * 255), (int)(g * 255), (int)(b * 255));
    IupSetAttribute(statusLabel, "FGCOLOR", colorStr);
    IupSetAttribute(bottomFrame, "FGCOLOR", colorStr);

    IupFlush();
    return IUP_DEFAULT;
}

void apply_theme_recursive(Ihandle* ih, const char* bgcolor, const char* fgcolor)
{
    if (!ih) return;

    IupSetAttribute(ih, "BGCOLOR", bgcolor);
    IupSetAttribute(ih, "FGCOLOR", fgcolor);
    IupSetAttribute(ih, "FLAT", "YES");  // Helps remove native 3D borders if supported

    int count = IupGetChildCount(ih);
    for (int i = 0; i < count; ++i) {
        apply_theme_recursive(IupGetChild(ih, i), bgcolor, fgcolor);
    }
}

static int uiThemeSelectCb(Ihandle *ih, char *text, int item, int state) {
    if (state == 1) {
        const char* bg;
        const char* fg;

        if (strcmp(text, "Dark") == 0) {
            bg = "40 40 40";
            fg = "255 255 255";
        } else {
            bg = "255 255 255";
            fg = "0 0 0";
        }

        apply_theme_recursive(dialog, bg, fg);

        IupSetAttribute(statusLabel, "BGCOLOR", bg);
        IupSetAttribute(bottomFrame, "BGCOLOR", bg);

        // Only apply static FGCOLOR if rainbow mode is off
        if (!rainbowModeEnabled) {
            IupSetAttribute(statusLabel, "FGCOLOR", fg);
            IupSetAttribute(bottomFrame, "FGCOLOR", fg);
        }

        IupSetAttribute(filterText, "BGCOLOR", bg);
        IupSetAttribute(filterText, "FGCOLOR", fg);
        IupSetAttribute(filterSelectList, "BGCOLOR", bg);
        IupSetAttribute(filterSelectList, "FGCOLOR", fg);
        IupSetAttribute(dialogVBox, "FGCOLOR", fg);
        IupSetAttribute(dialogVBox, "BGCOLOR", bg);
    }

    return IUP_DEFAULT;
}

void VkCodeToString(DWORD vk, char* outStr, int outStrSize) {
    UINT scanCode = MapVirtualKey(vk, MAPVK_VK_TO_VSC);
    if (scanCode == 0) {
        strncpy(outStr, "Unknown", outStrSize);
        return;
    }

    // Adjust for arrow keys (special cases)
    switch (vk) {
        case VK_LEFT:  scanCode = 0x4B; break;
        case VK_UP:    scanCode = 0x48; break;
        case VK_RIGHT: scanCode = 0x4D; break;
        case VK_DOWN:  scanCode = 0x50; break;
    }

    if (GetKeyNameText(scanCode << 16, outStr, outStrSize) == 0) {
        strncpy(outStr, "Unknown", outStrSize);
    }
}

int OnKeyCapture(Ihandle* ih, int c, int press, int release, int repeat) {
    if (press) {
        for (int vk = 0x08; vk <= 0xFE; vk++) {
            if (GetAsyncKeyState(vk) & 0x8000) {
                int actionIndex = (int)(intptr_t)IupGetAttribute(ih, "ACTION_INDEX");
                actionKeybinds[actionIndex] = vk;

                char keyName[64];
                VkCodeToString(vk, keyName, sizeof(keyName));
                IupSetAttribute(ih, "VALUE", keyName);

                SaveKeybindsToFile();

                // Remove focus so more keys aren't captured
                dlg = IupGetDialog(ih);
                IupSetFocus(dlg);
                break;
            }
        }

        return IUP_IGNORE;
    }

    return IUP_DEFAULT;
}

void init(int argc, char* argv[]) {
    UINT ix;
    char* arg_value = NULL;

    // fill in config
    loadConfig();

    // iup inits
    IupOpen(&argc, &argv);

    // this is so easy to get wrong so it's pretty worth noting in the program
    statusLabel = IupLabel("NOTICE: When capturing localhost (loopback) packets, you CAN'T include inbound criteria.\n"
        "Filters like 'udp' need to be 'udp and outbound' to work. See readme for more info.\nDon't use dark theme right now does not work.");
    IupSetAttribute(statusLabel, "EXPAND", "HORIZONTAL");
    IupSetAttribute(statusLabel, "PADDING", "8x8");

    themeList = IupList(NULL);
    IupSetAttribute(themeList, "DROPDOWN", "YES");
    IupSetAttribute(themeList, "VISIBLECOLUMNS", "10");
    IupSetAttribute(themeList, "1", "Light");
    IupSetAttribute(themeList, "2", "Dark");
    IupSetAttribute(themeList, "VALUE", "1"); // default to Light
    IupSetCallback(themeList, "ACTION", (Icallback)uiThemeSelectCb);

    IupSetCallback(rainbowCheckbox, "ACTION", (Icallback)toggleRainbowMode);

    IupSetAttribute(rainbowCheckbox, "VALUE", "ON");  // Start enabled

    topFrame = IupFrame(
        topVbox = IupVbox(
            filterText = IupText(NULL),
            controlHbox = IupHbox(
                stateIcon = IupLabel(NULL),
                filterButton = IupButton("Start", NULL),
                IupFill(),
                rainbowCheckbox = IupToggle("Rainbow Text", NULL),
                IupFill(),
                IupLabel("Presets:  "),
                filterSelectList = IupList(NULL),
                NULL
            ),
            NULL
        )
    );

    // parse arguments and set globals *before* setting up UI.
    // arguments can be read and set after callbacks are setup
    // FIXME as Release is built as WindowedApp, stdout/stderr won't show
    LOG("argc: %d", argc);
    if (argc > 1) {
        if (!parseArgs(argc, argv)) {
            fprintf(stderr, "invalid argument count. ensure you're using options as \"--drop on\"");
            exit(-1); // fail fast.
        }
        parameterized = 1;
    }

    IupSetAttribute(topFrame, "TITLE", "Filtering");
    IupSetAttribute(topFrame, "EXPAND", "HORIZONTAL");
    IupSetAttribute(filterText, "EXPAND", "HORIZONTAL");
    IupSetCallback(filterText, "VALUECHANGED_CB", (Icallback)uiFilterTextCb);
    IupSetAttribute(filterButton, "PADDING", "8x");
    IupSetCallback(filterButton, "ACTION", uiStartCb);
    IupSetAttribute(topVbox, "NCMARGIN", "4x4");
    IupSetAttribute(topVbox, "NCGAP", "4x2");
    IupSetAttribute(controlHbox, "ALIGNMENT", "ACENTER");

    // setup state icon
    IupSetAttribute(stateIcon, "IMAGE", "none_icon");
    IupSetAttribute(stateIcon, "PADDING", "4x");

    // fill in options and setup callback
    IupSetAttribute(filterSelectList, "VISIBLECOLUMNS", "24");
    IupSetAttribute(filterSelectList, "DROPDOWN", "YES");
    for (ix = 0; ix < filtersSize; ++ix) {
        char ixBuf[4];
        sprintf(ixBuf, "%d", ix+1); // ! staring from 1, following lua indexing
        IupStoreAttribute(filterSelectList, ixBuf, filters[ix].filterName);
    }
    IupSetAttribute(filterSelectList, "VALUE", "1");
    IupSetCallback(filterSelectList, "ACTION", (Icallback)uiListSelectCb);
    // set filter text value since the callback won't take effect before main loop starts
    IupSetAttribute(filterText, "VALUE", filters[0].filterValue);

    // functionalities frame 
    bottomFrame = IupFrame(
        bottomVbox = IupVbox(
            NULL
        )
    );
    IupSetAttribute(bottomFrame, "TITLE", "Functions");
    IupSetAttribute(bottomVbox, "NCMARGIN", "4x4");
    IupSetAttribute(bottomVbox, "NCGAP", "4x2");

    // create icons
    noneIcon = IupImage(8, 8, icon8x8);
    doingIcon = IupImage(8, 8, icon8x8);
    errorIcon = IupImage(8, 8, icon8x8);
    IupSetAttribute(noneIcon, "0", "BGCOLOR");
    IupSetAttribute(noneIcon, "1", "224 224 224");
    IupSetAttribute(doingIcon, "0", "BGCOLOR");
    IupSetAttribute(doingIcon, "1", "145 51 255");
    IupSetAttribute(errorIcon, "0", "BGCOLOR");
    IupSetAttribute(errorIcon, "1", "208 70 72");
    IupSetHandle("none_icon", noneIcon);
    IupSetHandle("doing_icon", doingIcon);
    IupSetHandle("error_icon", errorIcon);

    // setup module uis
    for (ix = 0; ix < MODULE_CNT; ++ix) {
        uiSetupModule(*(modules+ix), bottomVbox);
    }

    keybindVBox = IupVbox(NULL);

    for (int i = 0; i < 3; ++i) {
        char keyName[64] = {0};
        VkCodeToString(actionKeybinds[i], keyName, sizeof(keyName));
        hbox = IupGetChild(keybindVBox, i);
        text = IupGetChild(hbox, 1);
        IupSetAttribute(text, "VALUE", keyName);
    }

    LoadKeybindsFromFile();

    for (int i = 0; actions[i] != NULL; i++) {
        // Create label
        label = IupLabel(actions[i]);

        // Create readonly text box for keybind
        text = IupText(NULL);
        IupSetAttribute(text, "READONLY", "YES");
        IupSetCallback(text, "K_ANY", (Icallback)OnKeyCapture);

        // Store the action index for this text box
        IupSetAttribute(text, "ACTION_INDEX", (char*)(intptr_t)i);

        // Set initial displayed key name
        char keyName[64] = {0};
        VkCodeToString(actionKeybinds[i], keyName, sizeof(keyName));
        IupSetAttribute(text, "VALUE", keyName);

        // Create horizontal box for label + textbox
        hbox = IupHbox(label, text, NULL);

        // Styling
        IupSetAttribute(hbox, "BGCOLOR", "40 40 40");
        IupSetAttribute(label, "FGCOLOR", "0 0 0");
        IupSetAttribute(label, "BGCOLOR", "255 255 255");
        IupSetAttribute(text, "BGCOLOR", "255 255 255");
        IupSetAttribute(text, "FGCOLOR", "0 0 0");
        IupSetAttribute(text, "EXPAND", "HORIZONTAL");

        IupAppend(keybindVBox, hbox);
    }

    tabs = IupTabs(
        topFrame,
        bottomFrame,
        keybindVBox,
        NULL
    );
    IupSetAttribute(tabs, "TABTITLE0", "Filters");
    IupSetAttribute(tabs, "TABTITLE1", "Modules");
    IupSetAttribute(tabs, "TABTITLE2", "Keybinds");

    // dialog
    dialog = IupDialog(
        dialogVBox = IupVbox(
            tabs,
            statusLabel,
            themeList,
            NULL
        )
    );

    IupSetAttribute(dialog, "TITLE", "clumsy " CLUMSY_VERSION);
    IupSetAttribute(dialog, "SIZE", "480x"); // add padding manually to width
    IupSetAttribute(dialog, "RESIZE", "NO");
    IupSetCallback(dialog, "SHOW_CB", (Icallback)uiOnDialogShow);
	// IupSetAttribute(dialog, "BGCOLOR", "0 0 0");

    // global layout settings to affect childrens
    IupSetAttribute(dialogVBox, "ALIGNMENT", "ACENTER");
    IupSetAttribute(dialogVBox, "NCMARGIN", "4x4");
    IupSetAttribute(dialogVBox, "NCGAP", "4x2");

    // setup timer
    timer = IupTimer();
    IupSetAttribute(timer, "TIME", STR(ICON_UPDATE_MS));
    IupSetCallback(timer, "ACTION_CB", uiTimerCb);

    timer2 = IupTimer();
    IupSetAttributes(timer2, "TIME=100, RUN=YES");
    IupSetCallback(timer2, "ACTION_CB", uiRainbowTextColorCb);

    // setup timeout of program
    arg_value = IupGetGlobal("timeout");
    if(arg_value != NULL) {
        char valueBuf[16];
        snprintf(valueBuf, "%s000", arg_value);  // convert from seconds to milliseconds

        timeout = IupTimer();
        IupStoreAttribute(timeout, "TIME", valueBuf);
        IupSetCallback(timeout, "ACTION_CB", uiTimeoutCb);
        IupSetAttribute(timeout, "RUN", "YES");
    }

     //Retrieve the applications instance
    HINSTANCE instance = GetModuleHandle(NULL);
    //Set a global Windows Hook to capture keystrokes using the function declared above
    HHOOK test1 = SetWindowsHookEx( WH_KEYBOARD_LL, LowLevelKeyboardProc, instance,0);
}

void startup() {
    // initialize seed
    srand((unsigned int)time(NULL));

    // kickoff event loops
    IupShowXY(dialog, IUP_CENTER, IUP_CENTER);
    IupMainLoop();
    // ! main loop won't return until program exit
}

void cleanup() {

    IupDestroy(timer);
    if (timeout) {
        IupDestroy(timeout);
    }

    IupClose();
    endTimePeriod(); // try close if not closing
}

// ui logics
void showStatus(const char *line) {
    IupStoreAttribute(statusLabel, "TITLE", line); 
}

static int KEYPRESS_CB(Ihandle *ih, int c, int press) {
    LOG("Character: %d",c);
}

// in fact only 32bit binary would run on 64 bit os
// if this happens pop out message box and exit
static BOOL check32RunningOn64(HWND hWnd) {
    BOOL is64ret;
    // consider IsWow64Process return value
    if (IsWow64Process(GetCurrentProcess(), &is64ret) && is64ret) {
        MessageBox(hWnd, (LPCSTR)"You're running 32bit clumsy on 64bit Windows, which wouldn't work. Please use the 64bit clumsy version.",
            (LPCSTR)"Aborting", MB_OK);
        return TRUE;
    }
    return FALSE;
}

static BOOL checkIsRunning() {
    //It will be closed and destroyed when programm terminates (according to MSDN).
    HANDLE hStartEvent = CreateEventW(NULL, FALSE, FALSE, L"Global\\CLUMSY_IS_RUNNING_EVENT_NAME");

    if (hStartEvent == NULL)
        return TRUE;

    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(hStartEvent);
        hStartEvent = NULL;
        return TRUE;
    }

    return FALSE;
}

static int uiOnDialogShow(Ihandle *ih, int state) {
    // only need to process on show
    HWND hWnd;
    BOOL exit;
    HICON icon;
    HINSTANCE hInstance;
    if (state != IUP_SHOW) return IUP_DEFAULT;
    hWnd = (HWND)IupGetAttribute(ih, "HWND");
    hInstance = GetModuleHandle(NULL);

    // set application icon
    icon = LoadIcon(hInstance, "CLUMSY_ICON");
    SendMessage(hWnd, WM_SETICON, ICON_BIG, (LPARAM)icon);
    SendMessage(hWnd, WM_SETICON, ICON_SMALL, (LPARAM)icon);

    exit = checkIsRunning();
    if (exit) {
        MessageBox(hWnd, (LPCSTR)"Theres' already an instance of clumsy running.",
            (LPCSTR)"Aborting", MB_OK);
        return IUP_CLOSE;
    }

#ifdef _WIN32
    exit = check32RunningOn64(hWnd);
    if (exit) {
        return IUP_CLOSE;
    }
#endif

    // try elevate and decides whether to exit
    exit = tryElevate(hWnd, parameterized);

    if (!exit && parameterized) {
        setFromParameter(filterText, "VALUE", "filter");
        LOG("is parameterized, start filtering upon execution.");
        uiStartCb(filterButton);
    }

    return exit ? IUP_CLOSE : IUP_DEFAULT;
}

static int uiStartCb(Ihandle *ih) {
    char buf[MSG_BUFSIZE];
	
    UNREFERENCED_PARAMETER(ih);
	
    if (divertStart(IupGetAttribute(filterText, "VALUE"), buf) == 0) {
        showStatus(buf);
        return IUP_DEFAULT;
    }

    // successfully started
    showStatus("Started filtering. Enable functionalities to take effect.");
    IupSetAttribute(filterText, "ACTIVE", "NO");
    IupSetAttribute(filterButton, "TITLE", "Stop");
    IupSetCallback(filterButton, "ACTION", uiStopCb);
    IupSetAttribute(timer, "RUN", "YES");

    return IUP_DEFAULT;
}

static int uiStopCb(Ihandle *ih) {
    int ix;
    
	UNREFERENCED_PARAMETER(ih);
    
    // try stopping
    IupSetAttribute(filterButton, "ACTIVE", "NO");
    IupFlush(); // flush to show disabled state
    divertStop();

    IupSetAttribute(filterText, "ACTIVE", "YES");
    IupSetAttribute(filterButton, "TITLE", "Start");
    IupSetAttribute(filterButton, "ACTIVE", "YES");
    IupSetCallback(filterButton, "ACTION", uiStartCb);

    // stop timer and clean up icons
    IupSetAttribute(timer, "RUN", "NO");
    for (ix = 0; ix < MODULE_CNT; ++ix) {
        modules[ix]->processTriggered = 0; // use = here since is threads already stopped
        IupSetAttribute(modules[ix]->iconHandle, "IMAGE", "none_icon");
    }
    sendState = SEND_STATUS_NONE;
    IupSetAttribute(stateIcon, "IMAGE", "none_icon");

    showStatus("Stopped. To begin again, edit criteria and click Start.");
    return IUP_DEFAULT;
}

static int uiToggleControls(Ihandle *ih, int state) {
    controls = (Ihandle*)IupGetAttribute(ih, CONTROLS_HANDLE);
    short *target = (short*)IupGetAttribute(ih, SYNCED_VALUE);
    int controlsActive = IupGetInt(controls, "ACTIVE");
    if (controlsActive && !state) {
        IupSetAttribute(controls, "ACTIVE", "NO");
        InterlockedExchange16(target, I2S(state));
    } else if (!controlsActive && state) {
        IupSetAttribute(controls, "ACTIVE", "YES");
        InterlockedExchange16(target, I2S(state));
    }

    return IUP_DEFAULT;
}

static int uiTimerCb(Ihandle *ih) {
    int ix;
    UNREFERENCED_PARAMETER(ih);
    for (ix = 0; ix < MODULE_CNT; ++ix) {
        if (modules[ix]->processTriggered) {
            IupSetAttribute(modules[ix]->iconHandle, "IMAGE", "doing_icon");
            InterlockedAnd16(&(modules[ix]->processTriggered), 0);
        } else {
            IupSetAttribute(modules[ix]->iconHandle, "IMAGE", "none_icon");
        }
    }

    // update global send status icon
    switch (sendState) {
    case SEND_STATUS_NONE:
        IupSetAttribute(stateIcon, "IMAGE", "none_icon");
        break;
    case SEND_STATUS_SEND:
        IupSetAttribute(stateIcon, "IMAGE", "doing_icon");
        InterlockedAnd16(&sendState, SEND_STATUS_NONE);
        break;
    case SEND_STATUS_FAIL:
        IupSetAttribute(stateIcon, "IMAGE", "error_icon");
        InterlockedAnd16(&sendState, SEND_STATUS_NONE);
        break;
    }

    return IUP_DEFAULT;
}

static int uiTimeoutCb(Ihandle *ih) {
    UNREFERENCED_PARAMETER(ih);
    return IUP_CLOSE;
 }

static int uiListSelectCb(Ihandle *ih, char *text, int item, int state) {
    UNREFERENCED_PARAMETER(text);
    UNREFERENCED_PARAMETER(ih);
    if (state == 1) {
        IupSetAttribute(filterText, "VALUE", filters[item-1].filterValue);
    }
    return IUP_DEFAULT;
}

static int uiFilterTextCb(Ihandle *ih)  {
    UNREFERENCED_PARAMETER(ih);
    // unselect list
    IupSetAttribute(filterSelectList, "VALUE", "0");
    return IUP_DEFAULT;
}

static void uiSetupModule(Module *module, Ihandle *parent) {
    groupBox, toggle, controls, icon;
    groupBox = IupHbox(
        icon = IupLabel(NULL),
        toggle = IupToggle(module->displayName, NULL),
        IupFill(),
        controls = module->setupUIFunc(),
        NULL
    );
    IupSetAttribute(groupBox, "EXPAND", "HORIZONTAL");
    IupSetAttribute(groupBox, "ALIGNMENT", "ACENTER");
    IupSetAttribute(controls, "ALIGNMENT", "ACENTER");
    IupAppend(parent, groupBox);

    // set controls as attribute to toggle and enable toggle callback
    IupSetCallback(toggle, "ACTION", (Icallback)uiToggleControls);
    IupSetAttribute(toggle, CONTROLS_HANDLE, (char*)controls);
    IupSetAttribute(toggle, SYNCED_VALUE, (char*)module->enabledFlag);
    IupSetAttribute(controls, "ACTIVE", "NO"); // startup as inactive
    IupSetAttribute(controls, "NCGAP", "4"); // startup as inactive

    // set default icon
    IupSetAttribute(icon, "IMAGE", "none_icon");
    IupSetAttribute(icon, "PADDING", "4x");
    module->iconHandle = icon;
	module->toggleHandle = toggle;

    // parameterize toggle
    if (parameterized) {
        setFromParameter(toggle, "VALUE", module->shortName);
    }
}

int main(int argc, char* argv[]) {
	setup_crash_log_handler();
    LOG("Is Run As Admin: %d", IsRunAsAdmin());
    LOG("Is Elevated: %d", IsElevated());
    LoadKeybindsFromFile();
    init(argc, argv);
    startup();
    cleanup();
    return 0;
}

void setEnabled(BOOL value) {
    if (value) {
        uiStartCb(NULL);
    } else {
        uiStopCb(NULL);
    }
}

void setFilter(const char* value) {
    setFromValue(filterText, "VALUE", value);
}
