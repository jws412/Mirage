#include <WinDef.h>
#include <timeapi.h>
#include <winbase.h>
#include <winuser.h>

#include "global.h"
#include "global_dict.h"
#include "init.h"
#include "logic.h"

#define VIEWPORT_FPS 60

typedef struct {
    unsigned long ticksPerS;
    unsigned short minTimerResMs, maxTimerResMs;
} sClockConstant;

typedef struct {
    sClockConstant const c;
    unsigned long long startTicks, endTicks;
    unsigned short loops, sleepMs, virtualTimerResMs;
} sClock;

// Note that the debug menu only displays some performance metrics 
// while ignoring others. An example of the latter is the 
// `globalTimerResMs` member. Additionally, this file's code assumes 
// that the `fps` member is the first member.
static struct {
    unsigned short fps, cpuPermille, handles, pagefileKi, ramKi, 
        curTimerResMs, peakTimerResMs, globalTimerResMs;
} perfStats;

static sClock constuctClock(void);
static unsigned long long getTicks(void);
static unsigned long long getCpuHundredNs(HANDLE hproc);
static void updatePerfStats(HANDLE hproc,
    unsigned long long cpuHundredNs,
    unsigned long long clockUs,
    unsigned int processors);
#define GET_PERIOD(CLOCK, PRECISION) ( ((PRECISION)*((CLOCK).endTicks-(CLOCK).startTicks)) / (CLOCK).c.ticksPerS )

LRESULT CALLBACK WindowProcedure(HWND hwnd, unsigned int msg, WPARAM wParam,
    LPARAM lParam);

int main(void) {
    sClock clock = constuctClock();
    sContext game;
    HANDLE hproc;
    HWND hwnd;
    unsigned long long prevCpuHundredNs;
    MirageError code;
    unsigned short prevCpuPermille, peakTimerResMs, processors;
    
    // The game loop will end immediately if this function call fails.
    // The window procedure is responsible for allocating and 
    // formating any data graphical data. Mold data incorporates 
    // information regarding the amount of pixels that composes the 
    // entity data. This intertwinement forces the window procedure to 
    // initialise the mold data. The process' entry point does not 
    // have access to any graphical functionalities or information. 
    // This limitation requires forwarding the mold directory in the 
    // `WM_CREATE` window procedure message. The window painting logic 
    // also reacts according to the actors and level.
    hwnd = constructWindow(&WindowProcedure, (void*)&game.scene, 
        sizeof&game.scene);
    
    // The window initialisation procedure is responsible for posting 
    // any quit message after an error. However, the process should 
    // still go to the main program loop regardless of errors. This 
    // flow ensures that the process can go through the same quiting 
    // procedure.
    if (hwnd != NULL) {
        SYSTEM_INFO si[1];
        
        // Set the last error to zero to anticipate potential errors 
        // from the `SetWindowLongPtr` function.
        SetLastError(0);
        
        // The window procedure must also have access to the game's 
        // current scene. The call to the `SetWindowLongPtr` function 
        // stores this scene's address as a window attribute. The 
        // window creation procedure must allocate additional memory 
        // to house this address. Calls to the `SetWindowLongPtr` can 
        // return zero and does not necessarily indicate failure. A 
        // call to the `GetLastError` function tells whether the 
        // function call failed or not.
        SetWindowLongPtr(hwnd, 0, (LONG_PTR)&game.scene);
        if (GetLastError() != 0) {
            PANIC("The process could not set a window attribute.",
                MIRAGE_WINDOW_SET_ATTRIBUTE_FAIL);
            
        }
        
        // Zero out the durations for each possible input. The window 
        // initialisation procedure is not responsible for clearing out 
        // this member.
        ZeroMemory(&game.input, sizeof game.input);
        
        // Initialize to the maximum possible value prevent a  
        // statistic update from occuring immediately.
        prevCpuHundredNs = (unsigned long long) -1;
        prevCpuPermille = 1000;
        peakTimerResMs = clock.c.maxTimerResMs;
        
        // The `GetSystemInfo` function cannot fail.
        GetSystemInfo(si);
        processors = (unsigned short) si->dwNumberOfProcessors;
        
        // The `GetCurrentProcess` function cannot fail. It returns a 
        // constant.
        hproc = GetCurrentProcess();
        
        // The process should not bother if the operating system failed 
        // to set its priority.
        SetPriorityClass(hproc, HIGH_PRIORITY_CLASS);
        
        // The process should not bother if it could not set the timer 
        // resolution.
        timeBeginPeriod(clock.virtualTimerResMs);
        
        clock.startTicks = getTicks();
        
    }
    
    for (;;) {
        MSG msg[1];
        size_t i;
        
        // This branch calculates the amount of time to sleep 
        // frame after sampling one second. Every loop calls some 
        // window update and rendering protocols. Executing these 
        // calls thus incurs time costs every frame. The branch 
        // evaluates the time elapsing between the first and 
        // sixtieth frame. The branch adjusts the sleep time 
        // according to the average milliseconds per frame. The branch 
        // aims to get the average FPS to approach the target FPS.
        if (clock.loops == VIEWPORT_FPS) {
            unsigned long long curCpuHundredNs;
            unsigned int us;
            int slowingDown;
            unsigned short actualTimerRes;
            
            clock.endTicks = getTicks();
            curCpuHundredNs = getCpuHundredNs(hproc);
            us = (unsigned long) GET_PERIOD(clock,1000000UL);
            
            // The process is slowing down if the current framerate is 
            // one below the target.
            slowingDown = us > 1000000UL*(VIEWPORT_FPS+1) / VIEWPORT_FPS;
            
            // The current amount of hundreds of nanoseconds may not 
            // vary between subsequent performance measurements.
            if (curCpuHundredNs > prevCpuHundredNs) {
                unsigned long long cpuPeriodUs = curCpuHundredNs 
                    - prevCpuHundredNs;
                int increasingInCpuTime;
                updatePerfStats(hproc, cpuPeriodUs, us, processors);
                
                increasingInCpuTime = perfStats.cpuPermille > prevCpuPermille;
                
                timeEndPeriod(clock.virtualTimerResMs);
                
                // XXX: Branch causes the timer resolution to sprial 
                // to its doom sometimes. This behaviour makes the 
                // process hold excessive CPU time permanently.
                if (slowingDown && !increasingInCpuTime) {
                    peakTimerResMs = clock.virtualTimerResMs;
                    clock.virtualTimerResMs = (unsigned short) 
                        (clock.c.minTimerResMs + clock.virtualTimerResMs) / 2;
                    
                } else if (!slowingDown && increasingInCpuTime
                        && perfStats.globalTimerResMs 
                        > clock.virtualTimerResMs) {
                    
                    // Increase the process' timer resolution without 
                    // exceeding the global timer resolution. The 
                    // latter trumps over all requests for higher and 
                    // thus lower precision timer resolutions.
                    clock.virtualTimerResMs = (unsigned short) 
                        (peakTimerResMs + clock.virtualTimerResMs) / 2;
                    
                }
                // Otherwise, do not worry about changing the 
                // current timer resolution from the process' 
                // perspective.
                
                timeBeginPeriod(clock.virtualTimerResMs);
                
                perfStats.fps = (unsigned short)
                    ((1000000UL*VIEWPORT_FPS+1000000UL/2)/us);
                perfStats.peakTimerResMs = peakTimerResMs;
                prevCpuPermille = perfStats.cpuPermille;
                
            }
            
            actualTimerRes = clock.virtualTimerResMs 
                > perfStats.globalTimerResMs ? perfStats.globalTimerResMs 
                : clock.virtualTimerResMs;
            perfStats.curTimerResMs = actualTimerRes;
            
            // The process only changes the sleep time if it cannot 
            // reach the target FPS.
            if (slowingDown) {
                unsigned short computationTimeMs;
                signed short sleepEstimateMs;
                
                // The time that the process did not spend for 
                // sleeping was for performing computations. The sleep 
                // time cannot exceed the time for completing an 
                // entire loop. The mean computation time does exhibit 
                // some error. Any period for one loop deviates by an 
                // amount equal to the timer resolution. However, this 
                // error should be zero on average across sixty 
                // iterations.
                computationTimeMs = (unsigned short)(us / (VIEWPORT_FPS*1000) 
                    - clock.sleepMs);
                sleepEstimateMs = (signed short)(1000/VIEWPORT_FPS
                    - computationTimeMs - actualTimerRes);
                
                // XXX: Is checking for positivity necessary?
                if (sleepEstimateMs >= 1) {
                    clock.sleepMs = (unsigned short)sleepEstimateMs;
                    
                }
                
            }
            
            clock.loops = 0;
            prevCpuHundredNs = curCpuHundredNs;
            
            // Resume the time for the game logic. Assume that this 
            // branch takes a negligible amount of time to complete.
            clock.startTicks = getTicks();
            
        }
        
        // This loop assumes that the current sleep period cannot 
        // subceed one millisecond. Otherwise, passing zero to any 
        // call of the `Sleep` function triggers special behaviour. 
        // In this situation, the call would cause the process to 
        // yield resources.
        Sleep(clock.sleepMs);
        
        // Interpret messages from both the active thread and the 
        // process' window.
        if (PeekMessage(msg, NULL, 0, 0, PM_REMOVE)) {
            unsigned long long procTicksStart = 0; // Uninitializaton
                                                   // cannot be a 
                                                   // problem here. 
                                                   // This 
                                                   // initialization 
                                                   // is only here to 
                                                   // pacify compilers.
            
            // The upper sixteen bits of the `message` member is for 
            // the operating system.
            msg->message &= 0xFFFF;
            if (msg->message == WM_QUIT) {
                code = (MirageError) msg->wParam;
                break;
                
            }
            
            if (msg->message == WM_NCLBUTTONDOWN) {
                procTicksStart = getTicks();
                
            }
            TranslateMessage(msg);
            DispatchMessage(msg);
            
            // Only discard time periods where the user modifies the 
            // window's dimensions. Such modifications first requires 
            // the user to click a non-client area on the window.
            if (msg->message == WM_NCLBUTTONDOWN) {
                clock.startTicks += getTicks()-procTicksStart;
                
                // Window resizes temporarily increases CPU usage. 
                // Pretending that this increase was a decrease can 
                // avoid decreasing the timer resolution.
                prevCpuHundredNs = (unsigned long long) -1;
                
                // Do not perform any updates.
                continue;
                
            }
            
        }
        
        if (GetFocus() == hwnd) {
            // XXX: Prevent overflow for hold duration.
            // Update the state of inputs.
            for (i = 0; i < KEYS; ++i) {
                
                // XXX: Implement a way to verify that each assignent sets up the 
                // correct key?
                // XXX: Static or non-static array?
                static int const keyId[KEYS] = {
                    VK_RIGHT,
                    VK_UP,
                    VK_LEFT,
                    VK_DOWN,
                    'X',
                    VK_SPACE,
                    'C'
                };
                int isDown = !!GetAsyncKeyState(keyId[i]);
                
                // Assume that the first member is `right`.
                sKey *a = &game.input.right;
                
                // The game does not consider the first frame in the 
                // hold duration.
                a[i].holdDur = (unsigned char) 
                    (isDown ? (a[i].holdDur + isDown)
                    : 0);
            }
            
        } else {
            ZeroMemory(&game.input, sizeof game.input);
            
        }
        
        // Update the player outside the window procedure. No decision 
        // in this function call shall affect the player. Non-playable 
        // characters do behave in function of whether they are 
        // on-screen or not. As such, the paint handler in the window 
        // procedure updates the non-playable characters.
        game.scene.cast.actorData.player = updatePlayer(&game);
        
        // The windows painting procedure is responsible for updating 
        // the state of the game.
        RedrawWindow(hwnd, NULL, NULL, RDW_INVALIDATE|RDW_UPDATENOW);
        
        clock.loops++;
        
        // Spinlock to reach the frame period that is closest to the  
        // next microsecond.
        do {
            clock.endTicks = getTicks();
        } while (GET_PERIOD(clock,1000000UL) < 
                clock.loops*(1000000UL/VIEWPORT_FPS));
    }
    
    // The process does not bother with the timer resolution 
    // modification failures.
    timeEndPeriod(clock.c.minTimerResMs);
    
    // The operating system automatically unregisters the process'
    // window class after termination.
    return code;
}

#include <wingdi.h>
#include "gfx.h"

#define DEBUG_METRIC_CHARS 7
#define DEBUG_WIDTH_PELS ~~(VIEWPORT_WIDTH/2)
#define DEBUG_HEIGHT_PELS ~~(5*VIEWPORT_HEIGHT/12)
#define FONT_DEBUG_HEIGHT_PELS ~~(DEBUG_HEIGHT_PELS/METRICS)
#define METRICS 9U  // Including two for the player's x and y.

LRESULT CALLBACK WindowProcedure(HWND hwnd,
        unsigned int message,
        WPARAM wParam,
        LPARAM lParam) {
    
    // XXX: Someway to not make it static, like a heap alloc? 
    // Incorporate these data as attributes of the window?
    static struct {
        HBITMAP hb;
        HDC memoryDc;
        HFONT hf; // The backbuffer uses a font for the logo.
                  // The atlas uses a font for item tooltips.
                  // The debug interface uses a monospace debug font.
    } backbuffer, atlas, debug;
    static struct {
        
        // Points to an array of string lengths of labels 
        // with a null terminal character.
        // XXX: structure as suffix-string length pairs for simpler
        // data arrangement. There ought to be an amount of these 
        // pairs matching the amount of metrics.
        struct {
            unsigned short widthPels;
            char const suffix[2];
        } label[METRICS];
        enum {
            DEBUG_OFF = 0,
            DEBUG_ON
        } display;
    } metric = {
        { {0, "  "}, {0, "%o"}, {0, "  "}, {0, "ki"}, {0, "ki"}, {0, "ms"},
        {0, "ms"}, {0, "  "}, {0, "  "} }, DEBUG_OFF
    };
    
    switch (message) {
        HDC hdc;
        enum {
            MIRAGE_HOTKEY_TOGGLE,
            MIRAGE_HOTKEY_TERMINATE
        };
        int error;
        
        case WM_CREATE: {
            error = 1;
            
            hdc = GetDC(hwnd);
            if (hdc == NULL) {
                PANIC("The process failed to get the current device context",
                    MIRAGE_INVALID_DC);
                break;
                
            }
        }
        // Fall-through
        do {
            BITMAPINFO bi;
            RECT debugRect;
            size_t i, lastIndex, labels, maxLen;
            HFONT initialFont;
            sScene *s;
            char const metricLabelText[] =
                "FPS:\n"
                "CPU Usage:\n"
                "Handle Count:\n"
                "Pagefile Usage:\n"
                "RAM Usage:\n"
                "Current Timer Resolution:\n"
                "Peak Timer Resoltion:\n"
                "X:\n"
                "Y:\n";
            char const fontDebug[] = "Courier New";
            unsigned char fontDebugWidthPels;
            
            bi.bmiHeader.biSize = sizeof bi.bmiHeader;
            bi.bmiHeader.biWidth = VIEWPORT_WIDTH;
            bi.bmiHeader.biHeight = VIEWPORT_HEIGHT;
            bi.bmiHeader.biPlanes = 1;
            bi.bmiHeader.biBitCount = VIEWPORT_BPP;
            bi.bmiHeader.biCompression = BI_RGB; 
            bi.bmiHeader.biSizeImage = 0; // The bitmap does not use 
                                          // compression.
            bi.bmiHeader.biXPelsPerMeter = 3809; // Roughly equal to 
                                                 // 96 DPI.
            bi.bmiHeader.biYPelsPerMeter = 3809; // Ditto.
            bi.bmiHeader.biClrUsed = 0;
            bi.bmiHeader.biClrImportant = 0;
            
            // The `CreateCompatibleBitmap` can still take a null 
            // memory device context as input.
            backbuffer.hb = CreateCompatibleBitmap(hdc, VIEWPORT_WIDTH, 
                VIEWPORT_HEIGHT);
            if (backbuffer.hb == NULL) {
                PANIC("The process failed to create the backbuffer.",
                    MIRAGE_BACKBUFFER_INIT_FAIL);
                break;
                
            }
            
            // Create a memory device context for storing the 
            // backbuffer. This device context initially selects a 
            // one-by-one pixel monochrome bitmap.
            backbuffer.memoryDc = CreateCompatibleDC(hdc);
            
            // Any device context can contain one of each type of 
            // graphic object. The process will leave the backbuffer's 
            // device context as selecting its bitmap. Ensure to 
            // delete the default one-by-one monochrome bitmap.
            if (DeleteObject(SelectObject(backbuffer.memoryDc, backbuffer.hb))
                    == 0) {
                PANIC("The process failed to delete the backbuffer's default"
                    "monochrome bitmap.", MIRAGE_DELETE_DEFAULT_BITMAP_FAIL);
                break;
                
            }
            
            // The `lParam` parameter stores a pointer to a 
            // `CREATESTRUCT` instance. The window creation procedure 
            // set the `lpCreateParams` member in this struct to a
            // pointer. This member stores the address of an 
            // `sMoldDirectory` structure instance.
            s = (sScene*) ((CREATESTRUCT*)lParam)->lpCreateParams;
            
            // Initialise all information except for mold data in the 
            // `sContext` struct instance.
            if (initContext(s)) {
                // The call to the `initContext` function does not 
                // post any message.
                PANIC("Could not load the initial stage.", 
                    MIRAGE_CANNOT_LOAD_LEVEL);
                break;
                
            }
            
            if (initMoldDirectory(&s->md, backbuffer.memoryDc, &bi)) {
                // The `initMoldDirectory` function call is 
                // responsible for posting the quit message.
                break;
                
            }
            
            atlas.memoryDc = CreateCompatibleDC(backbuffer.memoryDc);
            
            // The tile atlas must have the same bit depth as the backbuffer.
            atlas.hb = initAtlas(backbuffer.memoryDc, &bi);
            if (atlas.hb == NULL) {
                // The `initAtlas` function is responsible for posting 
                // the quit message.
                break;
                
            }
            if (DeleteObject(SelectObject(atlas.memoryDc, atlas.hb))
                    == 0) {
                PANIC("The process failed to delete the tile atlas' default "
                    "monochrome bitmap.", MIRAGE_DELETE_DEFAULT_BITMAP_FAIL);
                break;
                
            }
            
            for (i = 0, maxLen = 0, lastIndex = 0, labels = 0; 
                    labels < ARRAY_ELEMENTS(metric.label); 
                    ++i) {
                if (metricLabelText[i] == '\n') {
                    size_t chars = i-lastIndex;
                    
                    // Initially store the amount of characters making 
                    // up the label for each label.
                    metric.label[labels].widthPels = (unsigned short) chars;
                    
                    ++labels;
                    if (chars > maxLen) {
                        maxLen = chars;
                        
                    }
                    lastIndex = i;
                    
                }
            }
            
            fontDebugWidthPels = (unsigned char)
                (DEBUG_WIDTH_PELS / (maxLen+DEBUG_METRIC_CHARS));
            
            for (i = 0; i < METRICS; ++i) {
                metric.label[i].widthPels = (unsigned short)
                    (metric.label[i].widthPels * fontDebugWidthPels);
            }
            
            // The pixel data of debug interface is initially a 
            // one-by-one monochrome bitmap.
            debug.memoryDc = CreateCompatibleDC(backbuffer.memoryDc);
            // XXX: Check for null?
            
            debug.hb = allocGfx(backbuffer.memoryDc, &bi, DEBUG_WIDTH_PELS, 
                DEBUG_HEIGHT_PELS);
            if (debug.hb == NULL) {
                PANIC("Failed to load the debug menu pixel data.",
                    MIRAGE_INVALID_DEBUG_PELDATA);
                break;
                
            }
            
            // Delete the default one-by-one monochrome bitmap.
            if (DeleteObject(SelectObject(debug.memoryDc, debug.hb)) == 0) {
                PANIC("The process failed to delete the debug menu's default"
                    "monochrome bitmap.", MIRAGE_DELETE_DEFAULT_BITMAP_FAIL);
                break;
                
            }
            
            // Create the font for the debug interface.
            debug.hf = CreateFont(FONT_DEBUG_HEIGHT_PELS,
                fontDebugWidthPels,
                0, // Escapement
                0, // Orientation
                FW_DONTCARE,
                0, // No italics.
                0, // No underline.
                0, // No strikeout.
                ANSI_CHARSET,
                OUT_DEFAULT_PRECIS,
                CLIP_DEFAULT_PRECIS,
                NONANTIALIASED_QUALITY,
                FIXED_PITCH|FF_DONTCARE,
                fontDebug);
            if (debug.hf == NULL) {
                PANIC("Failed to load the debug font.",
                    MIRAGE_INVALID_FONT);
                break;
                
            }
            
            initialFont = SelectObject(debug.memoryDc, debug.hf);
            
            #undef HGDI_ERROR
            #define HGDI_ERROR (HFONT)-1
            if (initialFont == NULL || initialFont == HGDI_ERROR) {
                PANIC("Cannot access the debug font.",
                    MIRAGE_LOST_DEBUG_FONT);
                break;
                
            }
            
            // The device context of the debug menu can delete the 
            // default font without issue.
            if (DeleteObject(initialFont) == 0) {
                PANIC("The window initialisation procedure failed to delete "
                    "the debug menu's default font.", 
                    MIRAGE_DELETE_DEFAULT_FONT_FAIL);
                break;
                
            }
            
            // Don't bother if the process could not modify font 
            // attributes.
            SetTextColor(debug.memoryDc, RGB(255, 255, 255));
            SetBkColor(debug.memoryDc, RGB(0, 0, 0));
            
            debugRect.top = 0;
            debugRect.left = 0;
            debugRect.right = DEBUG_WIDTH_PELS;
            debugRect.bottom = DEBUG_HEIGHT_PELS;
            
            // Render text over the debug menu's bitmap.
            if (DrawText(debug.memoryDc, 
                    metricLabelText, 
                    ARRAY_ELEMENTS(metricLabelText), 
                    &debugRect, 
                    DT_LEFT) == 0) {
                PANIC("Debug label render fail.", MIRAGE_CANNOT_RENDER_FONT);
                break;
                
            }
            
            // The process should not bother with any failure 
            // regarding setting up hotkeys.
            
            // Toggle display for the debug interface.
            RegisterHotKey(hwnd, MIRAGE_HOTKEY_TOGGLE, 
                MOD_CONTROL|MOD_NOREPEAT, 'D');
            
            // Terminate the process.
            RegisterHotKey(hwnd, MIRAGE_HOTKEY_TERMINATE, 
                MOD_CONTROL|MOD_NOREPEAT, 'W');
            
            // Set the error code to zero if the process carried out 
            // all initialisations correctly.
            error = 0;
        } while(0);
        // Fall-through
        {
            ReleaseDC(hwnd, hdc);
            
            // Immediately destroy the window if an error occurs. The 
            // pointers to graphic, actor, and level data can be 
            // garbage after an error. It is safer to prevent the 
            // window rendering logic from running.
            if (error) {
                DestroyWindow(hwnd);
            
            }
            
            // Then invoke the default behaviour for the `WM_CREATE`
            // message.
            break;
            
        }
        
        // Handle inputs that do not affect the player, including 
        // the following functionalities
        // *    Toggling the debug interface,
        // *    Terminating the process.
        case WM_HOTKEY: {
            
            // The `wParam` paramter stores the identifier of the 
            // hotkey.
            switch(wParam) {
                case MIRAGE_HOTKEY_TOGGLE: {
                    if (metric.display == DEBUG_OFF) {
                        metric.display = DEBUG_ON;
                        
                    } else {
                        metric.display = DEBUG_OFF;
                        
                    }
                    break;
                    
                }
                
                case MIRAGE_HOTKEY_TERMINATE: {
                    DestroyWindow(hwnd);
                    break;
                    
                }
            }
            break;
            
        }
        
        case WM_DESTROY: {
            sScene *s;
            MirageError code = MIRAGE_OK;
            unsigned int i;
            
            // Deleting all device contexts guarantees that the 
            // process can deallocate bitmaps and fonts.
            if (DeleteDC(backbuffer.memoryDc) == 0
                    || DeleteDC(atlas.memoryDc) == 0
                    || DeleteDC(debug.memoryDc) == 0) {
                code = MIRAGE_INVALID_FREE;
                
            }
            
            s = (sScene*) GetWindowLongPtr(hwnd, 0);
            if (s != NULL) {
                sMoldDirectory *md = &s->md;
                for (i = 0; i < md->molds; ++i) {
                    if (!DeleteObject(md->data[i].s.color)
                            || !DeleteObject(md->data[i].s.maskRight)
                            || !DeleteObject(md->data[i].s.maskLeft)) {
                        code = MIRAGE_INVALID_FREE;
                        
                    }
                }
                
            }
            // Otherwise, the process lets the resources leak, 
            // although during termination.
            
            
            if (DeleteObject(backbuffer.hb) == 0
                    || DeleteObject(atlas.hb) == 0
                    || DeleteObject(debug.hf) == 0
                    || UnregisterHotKey(hwnd, MIRAGE_HOTKEY_TOGGLE) == 0
                    || UnregisterHotKey(hwnd, MIRAGE_HOTKEY_TERMINATE) == 0
                    || freeLevelData()) {
                code = MIRAGE_INVALID_FREE;
                
            }
            
            PostQuitMessage(code);
            break;
            
        }
        
        case WM_PAINT: {
            PAINTSTRUCT ps[1];
            RECT wRect;
            HDC spriteMemDc;
            sScene *s;
            unsigned int i, pelsBeforePlayersLeft;
            unsigned char prevMoldId;
            
            hdc = BeginPaint(hwnd, ps);
            
            // Makes rendering less CPU-intensive. Don't bother if 
            // the function call fails.
            SetStretchBltMode(hdc, COLORONCOLOR);
            
            s = (sScene*) GetWindowLongPtr(hwnd, 0);
            
            // A call to the `GetWindowLongPtr` function can return 
            // zero if it fails.
            if (s == NULL) {
                PANIC("The process could not get a window attribute.",
                    MIRAGE_WINDOW_GET_ATTRIBUTE_FAIL);
                break;
                
            }
            
            pelsBeforePlayersLeft = (unsigned int)
                ((s->cast.actorData.player.pos.x
                + s->md.data[s->cast.actorData.player.moldId].w/2));
            if (pelsBeforePlayersLeft < VIEWPORT_WIDTH/2) {
                pelsBeforePlayersLeft = 0;
                
            } else if (pelsBeforePlayersLeft
                    + VIEWPORT_WIDTH/2 >= TILE_PELS*s->level.w) {
                pelsBeforePlayersLeft = (unsigned int)(TILE_PELS*s->level.w
                    - VIEWPORT_WIDTH);
                
            } else {
                // XXX: Consider case of being at the right-most 
                // boundary of the level.
                
                pelsBeforePlayersLeft = (unsigned int)
                    (pelsBeforePlayersLeft - VIEWPORT_WIDTH/2);
                
            }
            
            // Display all tiles visible in the viewport. This 
            // rendering process also includes the right-most column 
            // of tiles. The process offsets the screen positions of 
            // the tiles according to the player's position. This 
            // translation can only go backwards. As such, the 
            // rendering process must consider the right-most column 
            // of tiles.
            for (i = 0; 
                    i <= VIEWPORT_WIDTH/TILE_PELS; 
                    ++i) {
                unsigned int j;
                
                for (j = 0; j < VIEWPORT_HEIGHT/TILE_PELS; ++j) {
                    TILE const t = s->level.data[j 
                        + (pelsBeforePlayersLeft/TILE_PELS+i)
                        *s->level.h];
                    sCoord const tScreen = {
                        (unsigned short)(TILE_PELS*i),
                        (unsigned short)(VIEWPORT_HEIGHT - TILE_PELS*j 
                            - TILE_PELS)
                    };
                    unsigned int const shift = pelsBeforePlayersLeft 
                        % TILE_PELS;
                    if (!BitBlt(backbuffer.memoryDc, 
                            tScreen.x - (signed int)shift, 
                            tScreen.y, 
                            TILE_PELS, TILE_PELS, atlas.memoryDc,
                            0, t*TILE_PELS, SRCCOPY)) {
                        PANIC("The process failed to render a tile.",
                            MIRAGE_BACKBUFFER_WRITE_TILE_FAIL);
                        
                        // Minus two in order to cancel the effects 
                        // of the increment.
                        i = (unsigned int) -2;
                        break;
                        
                    }
                    
                }
                
            }
            
            // Display all sprites that can appear in the viewport.
            spriteMemDc = CreateCompatibleDC(backbuffer.memoryDc);
            for (i = 0, prevMoldId = MOLD_NULL; i < s->cast.actors; ++i) {
                sActor actor = s->cast.actorData.actor[i];
                
                // A mold identifier equal to zero outside the mold 
                // directory identifies a null actor.
                if (actor.moldId != MOLD_NULL) {
                    sMold const mold = s->md.data[actor.moldId];
                    HBITMAP mask;
                    POINT para[3];
                    unsigned char frameIndex;
                    
                    if ((unsigned int)(actor.pos.x+mold.w-1) 
                            < pelsBeforePlayersLeft
                            || actor.pos.x 
                            >= pelsBeforePlayersLeft+VIEWPORT_WIDTH) {
                        continue;
                        
                    }
                    
                    // Do not update the player again.
                    if (&s->cast.actorData.player 
                            - &s->cast.actorData.actor[0] != i) {
                        
                        // Update the logic of the actor. Skip the 
                        // rest of the logic if the actor died.
                        if (updateNpc(s, &s->cast.actorData.actor[i])) {
                            continue;
                            
                        }
                        
                    }
                    
                    if (actor.moldId != prevMoldId) {
                        // Ignore the return value as what the device 
                        // context previously selected is not 
                        // important. These values include default 
                        // graphic objects.
                        SelectObject(spriteMemDc, mold.s.color);
                        
                    }
                    // Otherwise, the previous iteration already 
                    // selected the source bitmap for the block 
                    // transfer.
                    
                    if (actor.frame > -1) {
                        para[0].x = actor.pos.x;
                        para[0].y = VIEWPORT_HEIGHT - actor.pos.y - mold.h;
                        para[1].x = actor.pos.x + mold.w;
                        para[1].y = VIEWPORT_HEIGHT - actor.pos.y - mold.h;
                        para[2].x = actor.pos.x;
                        para[2].y = VIEWPORT_HEIGHT - actor.pos.y;
                        
                        frameIndex = (unsigned char)actor.frame;
                        mask = mold.s.maskRight;
                        
                    } else {
                        para[0].x = actor.pos.x + mold.w - 1;
                        para[0].y = VIEWPORT_HEIGHT - actor.pos.y - mold.h;
                        para[1].x = actor.pos.x - 1;
                        para[1].y = VIEWPORT_HEIGHT - actor.pos.y - mold.h;
                        para[2].x = actor.pos.x + mold.w - 1;
                        para[2].y = VIEWPORT_HEIGHT - actor.pos.y;
                        
                        frameIndex = (unsigned char)~actor.frame;
                        mask = mold.s.maskLeft;
                        
                    }
                    
                    // Make the sprite's point of reference the left 
                    // side of the screen.
                    para[0].x = (para[0].x 
                        - (signed long int) pelsBeforePlayersLeft);
                    para[1].x = (para[1].x 
                        - (signed long int) pelsBeforePlayersLeft);
                    para[2].x = (para[2].x 
                        - (signed long int) pelsBeforePlayersLeft);
                    
                    
                    if (!PlgBlt(backbuffer.memoryDc, 
                            para,
                            spriteMemDc,
                            0, frameIndex*mold.h,
                            mold.w, mold.h,
                            mask,
                            0, frameIndex*mold.h)) {
                        PANIC("The process failed to draw a sprite.",
                            MIRAGE_BACKBUFFER_WRITE_SPRITE_FAIL);
                        break;
                        
                    }
                    prevMoldId = actor.moldId;
                    
                }
            }
            DeleteDC(spriteMemDc);
            
            if (metric.display == DEBUG_ON) {
                unsigned short metricData[METRICS];
                memcpy(&metricData, &perfStats, sizeof perfStats);
                
                // Assume that the horizontal position is at a lower 
                // address then the vertical position.
                memcpy(metricData + METRICS - 2, // X and Y coordinates
                    &s->cast.actorData.player.pos, 
                    sizeof s->cast.actorData.player.pos);
            
                // Update the metrics in the debug menu for the 
                // current frame. This process is inefficient, 
                // however. The process could be rendering the debug 
                // metric texts only when their values update. This 
                // strategy may minimize resource usage even further 
                // at the cost of lag spikes. Any metric update would 
                // trigger additional rendering logic, which would add 
                // additional overhead. Including this overhead on 
                // every frame effectively removes this risk. There is 
                // no benchmarking data to support this claim, however.
                for (i = 0; i != METRICS; ++i) {
                    unsigned short n = metricData[i], j = 7 - 2;
                    char buffer[8] = "        ";
                    
                    // XXX: Sometimes writes outside buffer. Please fix
                    memcpy(&buffer[0] + 6, &metric.label[i].suffix, 
                        sizeof metric.label[i].suffix);
                    
                    do {
                        buffer[j] = (char)(n%10 + '0');
                        --j;
                    } while (n /= 10);
                    
                    // The window initialisation procedure already 
                    // selected the debug menu's bitmap and font.
                    if (!TextOut(debug.memoryDc, 
                            (int) metric.label[i].widthPels, 
                            (int)(DEBUG_HEIGHT_PELS/METRICS * i) - (int)i,
                            &buffer[j] + 1, 7-j)) {
                        PANIC("The process failed to render the debug "
                            "interface", MIRAGE_BACKBUFFER_METRIC_FAIL);
                        break;
                        
                    }
                }
                
                if (!BitBlt(backbuffer.memoryDc, 0, 0, DEBUG_WIDTH_PELS, 
                        DEBUG_HEIGHT_PELS, debug.memoryDc, 0, 0, SRCCOPY)) {
                    PANIC("The process failed to render the debug interface",
                        MIRAGE_BACKBUFFER_WRITE_DEBUG_FAIL);
                    break;
                    
                }
                
            }
            
            // The process should not terminate the program if it 
            // could not determine window dimensions. The rectangle 
            // characterising the window's viewport is part of the 
            // window's client portion. This part does not consider 
            // the borders surrounding the viewport.
            GetClientRect(hwnd, &wRect);
            
            // Render on the window.
            if (!StretchBlt(hdc, 0, 0, 
                    wRect.right-wRect.left, wRect.bottom-wRect.top, 
                    backbuffer.memoryDc, 0, 0, VIEWPORT_WIDTH, 
                    VIEWPORT_HEIGHT, SRCCOPY)) {
                PANIC("The process failed to refresh the window bitmap.",
                    MIRAGE_WINDOW_WRITE_FAIL);
                break;
                
            }
            
            EndPaint(hwnd, ps);
            
            // The window procedure must return zero after processing 
            // the `WM_PAINT` message.
            return 0;
            
        }
        default: {
            
            break;
            
        }
    }
    
    return DefWindowProc(hwnd, message, wParam, lParam);
    
}

static sClock constuctClock() {
    sClockConstant init;
    LARGE_INTEGER li;
    TIMECAPS tc[1];
    
    // The `QueryPerformanceFrequency` function can never fail on 
    // Windows XP and later.
    QueryPerformanceFrequency(&li);
    init.ticksPerS = (unsigned long) li.QuadPart;
    
    if (timeGetDevCaps(tc, sizeof(TIMECAPS)) == MMSYSERR_NOERROR) {
        init.minTimerResMs = (unsigned short) tc->wPeriodMin;
        init.maxTimerResMs = (unsigned short) tc->wPeriodMax;
        if (init.maxTimerResMs > 1000/VIEWPORT_FPS) {
            init.maxTimerResMs = 1000/VIEWPORT_FPS;
            
        }
        
    } else {
        // Educated guesses
        init.minTimerResMs = 1;
        init.maxTimerResMs = 1000/VIEWPORT_FPS;
        
    }
    
    {
        sClock clock = { init, 0, 0, 0, 0, 0 };
        
        // The virtual timer resolution is the timer resolution from 
        // the process' perspective. This resolution does not consider 
        // the influence of the global timer resolution.
        clock.virtualTimerResMs = (unsigned short) (clock.c.minTimerResMs 
            + clock.c.maxTimerResMs) / 2;
        clock.loops = 0;
        clock.sleepMs = 1000 / VIEWPORT_FPS;
        return clock;
    }
}

static unsigned long long getCpuHundredNs(HANDLE hproc) {
    FILETIME kernelTime[1], userTime[1], dummy[1];
    ULARGE_INTEGER ia, ib;
    
    GetProcessTimes(hproc, dummy, dummy, kernelTime, userTime);
    ia.LowPart = kernelTime->dwLowDateTime;
    ia.HighPart = kernelTime->dwHighDateTime;
    ib.LowPart = userTime->dwLowDateTime;
    ib.HighPart = userTime->dwHighDateTime;
    return ia.QuadPart+ib.QuadPart;
}

static unsigned long long getTicks(void) {
    LARGE_INTEGER li;
    if (QueryPerformanceCounter(&li) == 0) {
        PANIC("The process could not fetch the current state "
            "of the performance counter.", MIRAGE_INVALID_PERFCOUNTER_ARG);
        return 0;
    
    }
    return (unsigned long long) li.QuadPart;
}

#include <psapi.h>
#include <Ntdef.h>
#include <ntstatus.h>
#include <winternl.h>
static void updatePerfStats(HANDLE hproc,
        unsigned long long cpuHundredNs,
        unsigned long long clockUs,
        unsigned int processors) {
    
    unsigned long handles[1], dummy[1], globalTimerResHundredNs;
    PROCESS_MEMORY_COUNTERS mc[1];
    
    perfStats.cpuPermille = (unsigned short)
        ((100*cpuHundredNs 
        / processors) // This application runs only on one 
                      // core since it only has one thread.
        / clockUs);
    
    if (NtQueryTimerResolution(dummy, dummy, &globalTimerResHundredNs)
            == STATUS_SUCCESS) {
        perfStats.globalTimerResMs = (unsigned short)
            (globalTimerResHundredNs / 10000);
        
    }
    
    // Only update if the function call succeeds.
    if (GetProcessHandleCount(hproc, handles)) {
        perfStats.handles = (unsigned short) *handles;
        
    }
    
    if (GetProcessMemoryInfo(hproc, mc, sizeof mc)) {
        perfStats.pagefileKi = (unsigned short) (mc->PagefileUsage/1024);
        perfStats.ramKi = (unsigned short) (mc->WorkingSetSize/1024);
        
    }
        
    return;
}
