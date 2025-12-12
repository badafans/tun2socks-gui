/*
 * TUN2SOCKS 简化代理管理器
 * 编译命令:
 * windres resource.rc -o resource.o
 * gcc -Wall -Wextra -std=c99 -finput-charset=UTF-8 -fexec-charset=GBK -Os -s -o tun2socks-gui-lite.exe resource.o main.c -lgdi32 -lcomctl32 -lshell32 -lws2_32 -mwindows
 */

#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <commctrl.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

// 单实例互斥体名称
#define SINGLE_INSTANCE_MUTEX_NAME "TUN2SOCKS_Manager_Lite_Mutex_Unique_ID"

// 图标资源 ID
#define IDI_APP_ICON 101 

// DPI 感知函数声明
typedef BOOL (WINAPI *SetProcessDPIAwareFunc)(void);

// 版本信息
#define APP_VERSION "1.0-Lite"
#define APP_TITLE "TUN2SOCKS 代理管理器 v" APP_VERSION

// 缓冲区大小定义
#define MAX_URL_LEN 2048
#define MAX_SMALL_LEN 256
#define MAX_CMD_LEN 4096
#define MAX_NAME_LEN 256

// 服务器配置限制
#define MAX_SERVERS 50

// 消息定义
#define WM_TRAYICON (WM_USER + 1)
#define WM_APPEND_LOG (WM_USER + 2) 

// 托盘菜单ID
#define ID_TRAY_ICON 9001
#define ID_TRAY_OPEN 9002
#define ID_TRAY_EXIT 9003

// 输入对话框控件ID
#define ID_INPUT_EDIT 2001
#define ID_INPUT_OK 2002
#define ID_INPUT_CANCEL 2003

// 字体与绘图对象
HFONT hFontUI = NULL;    
HFONT hFontLog = NULL;   
HBRUSH hBrushLog = NULL;

// DPI 感知
int g_dpi = 96;
int g_scale = 100;

// 缩放函数
int Scale(int x) {
    return (x * g_scale) / 100;
}

// 窗口控件ID定义
#define ID_SERVER_COMBO     1000
#define ID_SERVER_ADD       1001
#define ID_SERVER_SAVE      1002
#define ID_SERVER_DELETE    1003
#define ID_SERVER_RENAME    1004
#define ID_PROXY_ADDR_EDIT  1005
#define ID_PROXY_PORT_EDIT  1006
#define ID_PROXY_USER_EDIT  1007
#define ID_PROXY_PASS_EDIT  1008
#define ID_TUN_NAME_EDIT    1009
#define ID_TUN_ADDR_EDIT    1010
#define ID_TUN_MASK_EDIT    1011
#define ID_START_BTN        1012
#define ID_STOP_BTN         1013
#define ID_CLEAR_LOG_BTN    1014
#define ID_LOG_EDIT         1015
#define ID_STATUS_LABEL     1016

// 全局变量
HWND hMainWindow;
HWND hServerCombo;
HWND hProxyAddrEdit, hProxyPortEdit, hProxyUserEdit, hProxyPassEdit;
HWND hTunNameEdit, hTunAddrEdit, hTunMaskEdit;
HWND hStartBtn, hStopBtn, hLogEdit, hStatusLabel;
PROCESS_INFORMATION processInfo;
HANDLE hLogPipe = NULL;
HANDLE hLogThread = NULL;
BOOL isProcessRunning = FALSE;
NOTIFYICONDATA nid;

// 运行时生成的代理URL和服务器IP
char g_proxyUrl[MAX_URL_LEN] = "";
char g_serverIp[MAX_SMALL_LEN] = "";

// 配置结构体
typedef struct {
    char name[MAX_NAME_LEN];           // 服务器名称
    char proxyAddr[MAX_SMALL_LEN];     // 代理服务器地址
    char proxyPort[16];                // 代理端口
    char proxyUser[MAX_SMALL_LEN];     // 用户名（可选）
    char proxyPass[MAX_SMALL_LEN];     // 密码（可选）
    char tunName[MAX_SMALL_LEN];       // TUN 设备名称
    char tunAddress[MAX_SMALL_LEN];    // TUN 地址
    char tunMask[MAX_SMALL_LEN];       // TUN 子网掩码
} ServerConfig;

// 全局服务器配置数组
ServerConfig servers[MAX_SERVERS];
int serverCount = 0;
int currentServerIndex = 0;

// 输入对话框数据
typedef struct {
    char* buffer;
    int bufferSize;
    const char* prompt;
    BOOL result;
} InputDialogData;

// 函数声明
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK InputDialogProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
void CreateControls(HWND hwnd);
void StartProxy(void);
void StopProxy(void);
void AppendLog(const char* text);
void AppendLogFormat(const char* fmt, ...);
void AppendLogAsync(const char* text);
DWORD WINAPI LogReaderThread(LPVOID lpParam);
void SaveConfig(void);
void LoadConfig(void);
void GetControlValues(void);
void SetControlValues(void);
void InitTrayIcon(HWND hwnd);
void ShowTrayIcon(void);
void RemoveTrayIcon(void);
BOOL IsRunAsAdmin(void);
void ElevateToAdmin(void);
BOOL RunCommand(const char* cmd, BOOL wait);
void UpdateStatus(const char* status);
BOOL BuildProxyUrl(void);
BOOL ResolveHostToIp(const char* host, char* outIp, int maxLen);

// 服务器管理函数
void InitDefaultServer(void);
void RefreshServerCombo(void);
void SwitchServer(int index);
void AddNewServer(void);
void DeleteCurrentServer(void);
void RenameCurrentServer(void);
void SaveCurrentServer(void);
ServerConfig* GetCurrentServer(void);
BOOL ShowInputDialog(HWND parent, const char* title, const char* prompt, char* buffer, int bufferSize);

// 初始化 Winsock
BOOL InitWinsock(void) {
    WSADATA wsaData;
    return WSAStartup(MAKEWORD(2, 2), &wsaData) == 0;
}

void CleanupWinsock(void) {
    WSACleanup();
}

// 检查字符串是否为有效IP地址
BOOL IsValidIpAddress(const char* str) {
    struct in_addr addr;
    return inet_pton(AF_INET, str, &addr) == 1;
}

// 解析域名到IP地址
BOOL ResolveHostToIp(const char* host, char* outIp, int maxLen) {
    if (IsValidIpAddress(host)) {
        strncpy(outIp, host, maxLen - 1);
        outIp[maxLen - 1] = '\0';
        return TRUE;
    }
    
    struct addrinfo hints, *result = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    
    if (getaddrinfo(host, NULL, &hints, &result) != 0) {
        return FALSE;
    }
    
    if (result) {
        struct sockaddr_in* addr = (struct sockaddr_in*)result->ai_addr;
        inet_ntop(AF_INET, &addr->sin_addr, outIp, maxLen);
        freeaddrinfo(result);
        return TRUE;
    }
    
    return FALSE;
}

// 构建代理URL并提取服务器IP
BOOL BuildProxyUrl(void) {
    ServerConfig* cfg = GetCurrentServer();
    
    if (!ResolveHostToIp(cfg->proxyAddr, g_serverIp, sizeof(g_serverIp))) {
        AppendLogFormat("[错误] 无法解析代理服务器地址: %s\r\n", cfg->proxyAddr);
        MessageBox(hMainWindow, 
            "无法解析代理服务器地址！\n\n"
            "请检查网络连接和 DNS 设置。",
            "错误", MB_OK | MB_ICONERROR);
        return FALSE;
    }
    
    if (!IsValidIpAddress(cfg->proxyAddr)) {
        AppendLogFormat("[信息] 代理服务器 %s 解析为: %s\r\n", 
            cfg->proxyAddr, g_serverIp);
    } else {
        AppendLogFormat("[信息] 代理服务器IP: %s\r\n", g_serverIp);
    }
    
    if (strlen(cfg->proxyUser) > 0 && strlen(cfg->proxyPass) > 0) {
        snprintf(g_proxyUrl, sizeof(g_proxyUrl), "socks5://%s:%s@%s:%s",
            cfg->proxyUser, cfg->proxyPass,
            g_serverIp, cfg->proxyPort);
    } else {
        snprintf(g_proxyUrl, sizeof(g_proxyUrl), "socks5://%s:%s",
            g_serverIp, cfg->proxyPort);
    }
    
    return TRUE;
}

// 检查是否以管理员权限运行
BOOL IsRunAsAdmin(void) {
    BOOL isAdmin = FALSE;
    PSID adminGroup = NULL;
    SID_IDENTIFIER_AUTHORITY ntAuthority = SECURITY_NT_AUTHORITY;
    
    if (AllocateAndInitializeSid(&ntAuthority, 2, 
        SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS,
        0, 0, 0, 0, 0, 0, &adminGroup)) {
        CheckTokenMembership(NULL, adminGroup, &isAdmin);
        FreeSid(adminGroup);
    }
    return isAdmin;
}

// 以管理员权限重启
void ElevateToAdmin(void) {
    char exePath[MAX_PATH];
    GetModuleFileName(NULL, exePath, MAX_PATH);
    ShellExecute(NULL, "runas", exePath, NULL, NULL, SW_SHOWNORMAL);
}

// 执行命令
BOOL RunCommand(const char* cmd, BOOL wait) {
    STARTUPINFO si;
    PROCESS_INFORMATION pi;
    char cmdLine[MAX_CMD_LEN];
    
    memset(&si, 0, sizeof(si));
    memset(&pi, 0, sizeof(pi));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    
    snprintf(cmdLine, sizeof(cmdLine), "cmd.exe /c %s", cmd);
    
    if (CreateProcess(NULL, cmdLine, NULL, NULL, FALSE, 
        CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        if (wait) {
            WaitForSingleObject(pi.hProcess, 10000);
        }
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        return TRUE;
    }
    return FALSE;
}

// 配置 TUN 网卡（后台线程）
DWORD WINAPI ConfigureTunThread(LPVOID lpParam) {
    (void)lpParam;
    char cmd[MAX_CMD_LEN];
    ServerConfig* cfg = GetCurrentServer();
    
    AppendLogAsync("[网络] 等待 TUN 设备就绪...\r\n");
    Sleep(5000);
    
    AppendLogAsync("[网络] 配置 TUN 网卡 IP...\r\n");
    snprintf(cmd, sizeof(cmd), 
        "netsh interface ip set address name=\"%s\" static %s %s",
        cfg->tunName, cfg->tunAddress, cfg->tunMask);
    if (!RunCommand(cmd, TRUE)) {
        AppendLogAsync("[错误] 配置 TUN IP 失败\r\n");
        return 1;
    }
    
    AppendLogAsync("[网络] TUN 网卡配置完成!\r\n");
    AppendLogAsync("========================================\r\n");
    AppendLogAsync("TUN 代理已启动!\r\n");
    {
        char msg[512];
        snprintf(msg, sizeof(msg), 
            "TUN 网卡: %s\r\n"
            "TUN 地址: %s\r\n"
            "子网掩码: %s\r\n"
            "代理地址: %s:%s\r\n",
            cfg->tunName, 
            cfg->tunAddress,
            cfg->tunMask,
            cfg->proxyAddr, cfg->proxyPort);
        AppendLogAsync(msg);
    }
    AppendLogAsync("========================================\r\n");
    
    return 0;
}

// ========== 输入对话框实现 ==========
LRESULT CALLBACK InputDialogProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    static InputDialogData* pData = NULL;

    switch (uMsg) {
        case WM_CREATE: {
            CREATESTRUCT* cs = (CREATESTRUCT*)lParam;
            pData = (InputDialogData*)cs->lpCreateParams;
            SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)pData);
            
            int dlgW = Scale(400);
            int dlgH = Scale(160);
            int margin = Scale(20);
            int btnW = Scale(80);
            int btnH = Scale(30);
            int editH = Scale(26);
            
            HWND hPrompt = CreateWindow("STATIC", pData->prompt, 
                WS_VISIBLE | WS_CHILD | SS_LEFT,
                margin, margin, dlgW - margin * 2, Scale(20),
                hwnd, NULL, NULL, NULL);
            SendMessage(hPrompt, WM_SETFONT, (WPARAM)hFontUI, TRUE);
            
            HWND hEdit = CreateWindow("EDIT", pData->buffer,
                WS_VISIBLE | WS_CHILD | WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL,
                margin, margin + Scale(30), dlgW - margin * 2, editH,
                hwnd, (HMENU)ID_INPUT_EDIT, NULL, NULL);
            SendMessage(hEdit, WM_SETFONT, (WPARAM)hFontUI, TRUE);
            SendMessage(hEdit, EM_SETLIMITTEXT, pData->bufferSize - 1, 0);
            SendMessage(hEdit, EM_SETSEL, 0, -1);
            
            HWND hOK = CreateWindow("BUTTON", "确定",
                WS_VISIBLE | WS_CHILD | WS_TABSTOP | BS_DEFPUSHBUTTON,
                dlgW - margin - btnW * 2 - Scale(10), dlgH - margin - btnH - Scale(10),
                btnW, btnH,
                hwnd, (HMENU)ID_INPUT_OK, NULL, NULL);
            SendMessage(hOK, WM_SETFONT, (WPARAM)hFontUI, TRUE);
            
            HWND hCancel = CreateWindow("BUTTON", "取消",
                WS_VISIBLE | WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON,
                dlgW - margin - btnW, dlgH - margin - btnH - Scale(10),
                btnW, btnH,
                hwnd, (HMENU)ID_INPUT_CANCEL, NULL, NULL);
            SendMessage(hCancel, WM_SETFONT, (WPARAM)hFontUI, TRUE);
            
            SetFocus(hEdit);
            return 0;
        }

        case WM_COMMAND:
            pData = (InputDialogData*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
            
            switch (LOWORD(wParam)) {
                case ID_INPUT_OK: {
                    HWND hEdit = GetDlgItem(hwnd, ID_INPUT_EDIT);
                    GetWindowText(hEdit, pData->buffer, pData->bufferSize);
                    
                    char* start = pData->buffer;
                    while (*start == ' ' || *start == '\t') start++;
                    char* end = start + strlen(start) - 1;
                    while (end > start && (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n')) end--;
                    *(end + 1) = 0;
                    memmove(pData->buffer, start, strlen(start) + 1);
                    
                    if (strlen(pData->buffer) == 0) {
                        MessageBox(hwnd, "名称不能为空！", "提示", MB_OK | MB_ICONWARNING);
                        SetFocus(hEdit);
                        return 0;
                    }
                    
                    pData->result = TRUE;
                    DestroyWindow(hwnd);
                    return 0;
                }
                
                case ID_INPUT_CANCEL:
                    pData->result = FALSE;
                    DestroyWindow(hwnd);
                    return 0;
            }
            break;

        case WM_CLOSE:
            pData = (InputDialogData*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
            if (pData) pData->result = FALSE;
            DestroyWindow(hwnd);
            return 0;
    }
    
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

BOOL ShowInputDialog(HWND parent, const char* title, const char* prompt, char* buffer, int bufferSize) {
    InputDialogData data;
    data.buffer = buffer;
    data.bufferSize = bufferSize;
    data.prompt = prompt;
    data.result = FALSE;
    
    int dlgW = Scale(400);
    int dlgH = Scale(160);
    
    RECT parentRect;
    GetWindowRect(parent, &parentRect);
    int x = parentRect.left + (parentRect.right - parentRect.left - dlgW) / 2;
    int y = parentRect.top + (parentRect.bottom - parentRect.top - dlgH) / 2;
    
    HWND hDlg = CreateWindowEx(
        WS_EX_DLGMODALFRAME | WS_EX_TOPMOST,
        "InputDialog",
        title,
        WS_POPUP | WS_CAPTION | WS_SYSMENU,
        x, y, dlgW, dlgH,
        parent,
        NULL,
        GetModuleHandle(NULL),
        &data
    );
    
    if (!hDlg) return FALSE;
    
    ShowWindow(hDlg, SW_SHOW);
    UpdateWindow(hDlg);
    
    EnableWindow(parent, FALSE);
    
    MSG msg;
    while (IsWindow(hDlg) && GetMessage(&msg, NULL, 0, 0)) {
        if (msg.message == WM_KEYDOWN) {
            if (msg.wParam == VK_RETURN) {
                PostMessage(hDlg, WM_COMMAND, ID_INPUT_OK, 0);
                continue;
            } else if (msg.wParam == VK_ESCAPE) {
                PostMessage(hDlg, WM_COMMAND, ID_INPUT_CANCEL, 0);
                continue;
            }
        }
        
        if (!IsDialogMessage(hDlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }
    
    EnableWindow(parent, TRUE);
    SetForegroundWindow(parent);
    SetActiveWindow(parent);
    
    return data.result;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, 
                   LPSTR lpCmdLine, int nCmdShow) {
    (void)hPrevInstance; 
    (void)lpCmdLine;
    
    memset(&processInfo, 0, sizeof(processInfo));
    
    // 初始化 Winsock
    if (!InitWinsock()) {
        MessageBox(NULL, "初始化网络失败", APP_TITLE, MB_OK | MB_ICONERROR);
        return 1;
    }
    
    // 检查管理员权限
    if (!IsRunAsAdmin()) {
        int result = MessageBox(NULL, 
            "此程序需要管理员权限才能配置网络。\n\n是否以管理员权限重新启动？",
            APP_TITLE, MB_YESNO | MB_ICONWARNING);
        if (result == IDYES) {
            ElevateToAdmin();
        }
        CleanupWinsock();
        return 0;
    }
    
    // 单实例检查
    HANDLE hMutex = CreateMutex(NULL, TRUE, SINGLE_INSTANCE_MUTEX_NAME);
    if (hMutex != NULL && GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND hExistingWnd = FindWindow("TUN2SOCKSManagerLite", NULL); 
        if (hExistingWnd) {
            PostMessage(hExistingWnd, WM_TRAYICON, ID_TRAY_ICON, WM_LBUTTONUP);
        }
        CloseHandle(hMutex);
        CleanupWinsock();
        return 0; 
    }
    
    // DPI 感知
    HMODULE hUser32 = LoadLibrary("user32.dll");
    if (hUser32) {
        SetProcessDPIAwareFunc setDPIAware = 
            (SetProcessDPIAwareFunc)(void*)GetProcAddress(hUser32, "SetProcessDPIAware");
        if (setDPIAware) setDPIAware();
        FreeLibrary(hUser32);
    }
    
    HDC hdc = GetDC(NULL);
    g_dpi = GetDeviceCaps(hdc, LOGPIXELSX);
    g_scale = (g_dpi * 100) / 96;
    ReleaseDC(NULL, hdc);
    
    // 初始化公共控件
    INITCOMMONCONTROLSEX icex;
    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icex.dwICC = ICC_WIN95_CLASSES | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icex);

    // 创建字体
    hFontUI = CreateFont(Scale(17), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, 
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, 
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Microsoft YaHei UI");

    hFontLog = CreateFont(Scale(14), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, 
        ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, 
        CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, "Consolas");

    hBrushLog = CreateSolidBrush(RGB(30, 30, 30));

    // 注册窗口类
    WNDCLASS wc;
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = "TUN2SOCKSManagerLite";
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1); 
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_APP_ICON));
    if (!wc.hIcon) wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);

    if (!RegisterClass(&wc)) {
        CleanupWinsock();
        return 1;
    }

    // 注册输入对话框窗口类
    WNDCLASS wcInput;
    memset(&wcInput, 0, sizeof(wcInput));
    wcInput.lpfnWndProc = InputDialogProc;
    wcInput.hInstance = hInstance;
    wcInput.lpszClassName = "InputDialog";
    wcInput.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wcInput.hCursor = LoadCursor(NULL, IDC_ARROW);
    wcInput.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    RegisterClass(&wcInput);

    int winWidth = Scale(580);
    int winHeight = Scale(560);
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);

    // 只保留关闭按钮，不要最小化和最大化
    DWORD winStyle = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN;

    hMainWindow = CreateWindowEx(
        0, "TUN2SOCKSManagerLite", APP_TITLE, 
        winStyle,
        (screenW - winWidth) / 2, (screenH - winHeight) / 2, 
        winWidth, winHeight,
        NULL, NULL, hInstance, NULL
    );

    if (!hMainWindow) {
        CleanupWinsock();
        return 1;
    }

    InitTrayIcon(hMainWindow);
    ShowTrayIcon();

    ShowWindow(hMainWindow, nCmdShow);
    UpdateWindow(hMainWindow);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_TAB) {
            IsDialogMessage(hMainWindow, &msg);
        } else {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }
    
    CloseHandle(hMutex);
    CleanupWinsock();
    return (int)msg.wParam;
}

void InitTrayIcon(HWND hwnd) {
    memset(&nid, 0, sizeof(NOTIFYICONDATA));
    nid.cbSize = sizeof(NOTIFYICONDATA);
    nid.hWnd = hwnd;
    nid.uID = ID_TRAY_ICON;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_TRAYICON;
    nid.hIcon = LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_APP_ICON));
    if (!nid.hIcon) nid.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    strncpy(nid.szTip, APP_TITLE, sizeof(nid.szTip) - 1);
}

void ShowTrayIcon(void) {
    Shell_NotifyIcon(NIM_ADD, &nid);
}

void RemoveTrayIcon(void) {
    Shell_NotifyIcon(NIM_DELETE, &nid);
}

void UpdateStatus(const char* status) {
    SetWindowText(hStatusLabel, status);
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_CREATE:
            CreateControls(hwnd);
            LoadConfig();
            if (serverCount == 0) InitDefaultServer();
            RefreshServerCombo();
            SetControlValues();
            break;

        case WM_SYSCOMMAND:
            return DefWindowProc(hwnd, uMsg, wParam, lParam);

        case WM_TRAYICON:
            if (lParam == WM_LBUTTONUP) {
                if (!IsWindowVisible(hwnd)) {
                    ShowWindow(hwnd, SW_RESTORE);
                }
                SetForegroundWindow(hwnd);
                SetActiveWindow(hwnd);
            } 
            else if (lParam == WM_RBUTTONUP) {
                POINT pt;
                GetCursorPos(&pt);
                HMENU hMenu = CreatePopupMenu();
                if (hMenu) {
                    AppendMenu(hMenu, MF_STRING, ID_TRAY_OPEN, "打开界面");
                    AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
                    
                    if (isProcessRunning) {
                        AppendMenu(hMenu, MF_STRING | MF_GRAYED, 0, "[*] 代理运行中");
                    } else {
                        AppendMenu(hMenu, MF_STRING | MF_GRAYED, 0, "[ ] 代理已停止");
                    }
                    
                    AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
                    AppendMenu(hMenu, MF_STRING, ID_TRAY_EXIT, "退出程序");
                    SetForegroundWindow(hwnd); 
                    TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL);
                    PostMessage(hwnd, WM_NULL, 0, 0);
                    DestroyMenu(hMenu);
                }
            }
            break;

        case WM_APPEND_LOG: {
            char* logText = (char*)lParam;
            if (logText) {
                AppendLog(logText);
                free(logText);
            }
            break;
        }

        case WM_CTLCOLORSTATIC: {
            HDC hdcStatic = (HDC)wParam;
            HWND hCtrl = (HWND)lParam;
            int ctrlId = GetDlgCtrlID(hCtrl);
            if (ctrlId == ID_LOG_EDIT) {
                SetTextColor(hdcStatic, RGB(0, 255, 0));
                SetBkColor(hdcStatic, RGB(30, 30, 30));
                SetBkMode(hdcStatic, OPAQUE);              
                return (LRESULT)hBrushLog;                 
            }
            SetBkMode(hdcStatic, TRANSPARENT);             
            return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);
        }

        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case ID_TRAY_OPEN:
                    ShowWindow(hwnd, SW_RESTORE);
                    SetForegroundWindow(hwnd);
                    SetActiveWindow(hwnd);
                    break;
                
                case ID_TRAY_EXIT:
                    if (isProcessRunning) StopProxy();
                    GetControlValues();
                    SaveConfig();
                    RemoveTrayIcon();
                    DestroyWindow(hwnd);
                    break;

                case ID_SERVER_COMBO:
                    if (HIWORD(wParam) == CBN_SELCHANGE) {
                        if (!isProcessRunning) {
                            GetControlValues();
                            int comboIndex = (int)SendMessage(hServerCombo, CB_GETCURSEL, 0, 0);
                            if (comboIndex != CB_ERR) {
                                int realIndex = (int)SendMessage(hServerCombo, CB_GETITEMDATA, comboIndex, 0);
                                if (realIndex != CB_ERR && realIndex >= 0 && realIndex < serverCount) {
                                    SwitchServer(realIndex);
                                }
                            }
                        } else {
                            int comboCount = (int)SendMessage(hServerCombo, CB_GETCOUNT, 0, 0);
                            for (int i = 0; i < comboCount; i++) {
                                int realIndex = (int)SendMessage(hServerCombo, CB_GETITEMDATA, i, 0);
                                if (realIndex == currentServerIndex) {
                                    SendMessage(hServerCombo, CB_SETCURSEL, i, 0);
                                    break;
                                }
                            }
                            MessageBox(hwnd, "请先停止当前连接后再切换服务器", "提示", MB_OK | MB_ICONWARNING);
                        }
                    }
                    break;

                case ID_SERVER_ADD:
                    if (!isProcessRunning) {
                        AddNewServer();
                    } else {
                        MessageBox(hwnd, "请先停止当前连接", "提示", MB_OK | MB_ICONWARNING);
                    }
                    break;

                case ID_SERVER_SAVE:
                    SaveCurrentServer();
                    break;

                case ID_SERVER_DELETE:
                    if (!isProcessRunning) {
                        DeleteCurrentServer();
                    } else {
                        MessageBox(hwnd, "请先停止当前连接", "提示", MB_OK | MB_ICONWARNING);
                    }
                    break;

                case ID_SERVER_RENAME:
                    if (!isProcessRunning) {
                        RenameCurrentServer();
                    } else {
                        MessageBox(hwnd, "请先停止当前连接", "提示", MB_OK | MB_ICONWARNING);
                    }
                    break;

                case ID_START_BTN:
                    if (!isProcessRunning) {
                        GetControlValues();
                        
                        ServerConfig* cfg = GetCurrentServer();
                        if (strlen(cfg->proxyAddr) == 0) {
                            MessageBox(hwnd, "请输入代理服务器地址", 
                                "提示", MB_OK | MB_ICONWARNING);
                            SetFocus(hProxyAddrEdit);
                            break;
                        }
                        if (strlen(cfg->proxyPort) == 0) {
                            MessageBox(hwnd, "请输入代理端口", 
                                "提示", MB_OK | MB_ICONWARNING);
                            SetFocus(hProxyPortEdit);
                            break;
                        }
                        
                        SaveConfig();
                        StartProxy();
                    }
                    break;

                case ID_STOP_BTN:
                    if (isProcessRunning) {
                        StopProxy();
                    }
                    break;

                case ID_CLEAR_LOG_BTN:
                    SetWindowText(hLogEdit, "");
                    break;
            }
            break;

        case WM_CLOSE:
            ShowWindow(hwnd, SW_HIDE);
            return 0;

        case WM_DESTROY:
            RemoveTrayIcon();
            if (hFontUI) DeleteObject(hFontUI);
            if (hFontLog) DeleteObject(hFontLog);
            if (hBrushLog) DeleteObject(hBrushLog);
            PostQuitMessage(0);
            break;

        default:
            return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }
    return 0;
}

void CreateLabelAndEdit(HWND parent, const char* labelText, int labelW,
    int x, int y, int editW, int editH, int editId, HWND* outEdit, BOOL isPassword) {
    
    HWND hStatic = CreateWindow("STATIC", labelText, 
        WS_VISIBLE | WS_CHILD | SS_RIGHT, 
        x, y + Scale(3), labelW, Scale(18), parent, NULL, NULL, NULL);
    SendMessage(hStatic, WM_SETFONT, (WPARAM)hFontUI, TRUE);

    DWORD style = WS_VISIBLE | WS_CHILD | WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL;
    if (isPassword) style |= ES_PASSWORD;

    *outEdit = CreateWindow("EDIT", "", style,
        x + labelW + Scale(8), y, editW, editH, parent, 
        (HMENU)(intptr_t)editId, NULL, NULL);
    SendMessage(*outEdit, WM_SETFONT, (WPARAM)hFontUI, TRUE);
}

void CreateControls(HWND hwnd) {
    RECT rect;
    GetClientRect(hwnd, &rect);
    int winW = rect.right;
    int margin = Scale(15);
    int groupW = winW - (margin * 2);
    int lineHeight = Scale(28);
    int lineGap = Scale(6);
    int editH = Scale(24);
    int labelW = Scale(70);
    int curY = margin;

    // ========== 服务器管理区域 ==========
    int serverMgrH = Scale(65);
    HWND hGroupServer = CreateWindow("BUTTON", "服务器管理", 
        WS_VISIBLE | WS_CHILD | BS_GROUPBOX,
        margin, curY, groupW, serverMgrH, hwnd, NULL, NULL, NULL);
    SendMessage(hGroupServer, WM_SETFONT, (WPARAM)hFontUI, TRUE);

    int innerY = curY + Scale(22);
    int innerX = margin + Scale(15);

    HWND hLblServer = CreateWindow("STATIC", "选择服务器:", 
        WS_VISIBLE | WS_CHILD, 
        innerX, innerY + Scale(3), Scale(85), Scale(20), hwnd, NULL, NULL, NULL);
    SendMessage(hLblServer, WM_SETFONT, (WPARAM)hFontUI, TRUE);

    hServerCombo = CreateWindow("COMBOBOX", "", 
        WS_VISIBLE | WS_CHILD | WS_TABSTOP | CBS_DROPDOWNLIST | CBS_HASSTRINGS | WS_VSCROLL,
        innerX + Scale(90), innerY, Scale(180), Scale(200), 
        hwnd, (HMENU)ID_SERVER_COMBO, NULL, NULL);
    SendMessage(hServerCombo, WM_SETFONT, (WPARAM)hFontUI, TRUE);

    int btnX = innerX + Scale(90) + Scale(190);
    int btnW = Scale(55);
    int btnH = Scale(24);
    int btnGap = Scale(5);

    HWND hBtnAdd = CreateWindow("BUTTON", "新增", 
        WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        btnX, innerY, btnW, btnH, hwnd, (HMENU)ID_SERVER_ADD, NULL, NULL);
    SendMessage(hBtnAdd, WM_SETFONT, (WPARAM)hFontUI, TRUE);

    HWND hBtnSave = CreateWindow("BUTTON", "保存", 
        WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        btnX + btnW + btnGap, innerY, btnW, btnH, hwnd, (HMENU)ID_SERVER_SAVE, NULL, NULL);
    SendMessage(hBtnSave, WM_SETFONT, (WPARAM)hFontUI, TRUE);

    HWND hBtnRename = CreateWindow("BUTTON", "重命名", 
        WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        btnX + (btnW + btnGap) * 2, innerY, btnW + Scale(10), btnH, hwnd, (HMENU)ID_SERVER_RENAME, NULL, NULL);
    SendMessage(hBtnRename, WM_SETFONT, (WPARAM)hFontUI, TRUE);

    HWND hBtnDelete = CreateWindow("BUTTON", "删除", 
        WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        btnX + (btnW + btnGap) * 2 + btnW + Scale(10) + btnGap, innerY, btnW, btnH, hwnd, (HMENU)ID_SERVER_DELETE, NULL, NULL);
    SendMessage(hBtnDelete, WM_SETFONT, (WPARAM)hFontUI, TRUE);

    curY += serverMgrH + Scale(10);

    // ========== 代理配置组 ==========
    int group1H = Scale(130);
    HWND hGroup1 = CreateWindow("BUTTON", "SOCKS5 代理配置", 
        WS_VISIBLE | WS_CHILD | BS_GROUPBOX,
        margin, curY, groupW, group1H, hwnd, NULL, NULL, NULL);
    SendMessage(hGroup1, WM_SETFONT, (WPARAM)hFontUI, TRUE);
    
    innerX = margin + Scale(15);
    innerY = curY + Scale(22);
    
    // 代理服务器地址 和 端口（同一行）
    int addrEditW = Scale(200);
    int portLabelW = Scale(40);
    int portEditW = Scale(70);
    
    CreateLabelAndEdit(hwnd, "地址:", labelW, innerX, innerY, 
        addrEditW, editH, ID_PROXY_ADDR_EDIT, &hProxyAddrEdit, FALSE);
    
    int portX = innerX + labelW + Scale(8) + addrEditW + Scale(15);
    CreateLabelAndEdit(hwnd, "端口:", portLabelW, portX, innerY, 
        portEditW, editH, ID_PROXY_PORT_EDIT, &hProxyPortEdit, FALSE);
    
    innerY += lineHeight + lineGap;
    
    // 用户名 和 密码（同一行）
    int halfW = Scale(130);
    CreateLabelAndEdit(hwnd, "用户名:", labelW, innerX, innerY, 
        halfW, editH, ID_PROXY_USER_EDIT, &hProxyUserEdit, FALSE);
    
    int passX = innerX + labelW + Scale(8) + halfW + Scale(15);
    CreateLabelAndEdit(hwnd, "密码:", Scale(45), passX, innerY, 
        halfW, editH, ID_PROXY_PASS_EDIT, &hProxyPassEdit, TRUE);
    
    // 添加提示标签
    innerY += lineHeight + lineGap - Scale(2);
    HWND hTip = CreateWindow("STATIC", "(用户名和密码可留空)", 
        WS_VISIBLE | WS_CHILD, 
        innerX + labelW + Scale(8), innerY, Scale(200), Scale(16), 
        hwnd, NULL, NULL, NULL);
    SendMessage(hTip, WM_SETFONT, (WPARAM)hFontUI, TRUE);

    curY += group1H + Scale(10);

    // ========== TUN 设备配置组 ==========
    int group2H = Scale(75);
    HWND hGroup2 = CreateWindow("BUTTON", "TUN 设备配置", 
        WS_VISIBLE | WS_CHILD | BS_GROUPBOX,
        margin, curY, groupW, group2H, hwnd, NULL, NULL, NULL);
    SendMessage(hGroup2, WM_SETFONT, (WPARAM)hFontUI, TRUE);
    
    innerY = curY + Scale(22);
    
    // TUN 设备名称、TUN 地址 和 子网掩码（同一行）
    CreateLabelAndEdit(hwnd, "设备名:", labelW, innerX, innerY, 
        Scale(80), editH, ID_TUN_NAME_EDIT, &hTunNameEdit, FALSE);
    
    int addrX = innerX + labelW + Scale(8) + Scale(80) + Scale(15);
    CreateLabelAndEdit(hwnd, "地址:", Scale(45), addrX, innerY, 
        Scale(100), editH, ID_TUN_ADDR_EDIT, &hTunAddrEdit, FALSE);
    
    int maskX = addrX + Scale(45) + Scale(8) + Scale(100) + Scale(15);
    CreateLabelAndEdit(hwnd, "掩码:", Scale(45), maskX, innerY, 
        Scale(100), editH, ID_TUN_MASK_EDIT, &hTunMaskEdit, FALSE);

    curY += group2H + Scale(12);

    // ========== 按钮栏 ==========
    int btnW2 = Scale(100);
    int btnH2 = Scale(32);
    int btnGap2 = Scale(15);

    hStartBtn = CreateWindow("BUTTON", "启动代理", 
        WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON,
        margin, curY, btnW2, btnH2, hwnd, (HMENU)ID_START_BTN, NULL, NULL);
    SendMessage(hStartBtn, WM_SETFONT, (WPARAM)hFontUI, TRUE);

    hStopBtn = CreateWindow("BUTTON", "停止", 
        WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        margin + btnW2 + btnGap2, curY, btnW2, btnH2, hwnd, (HMENU)ID_STOP_BTN, NULL, NULL);
    SendMessage(hStopBtn, WM_SETFONT, (WPARAM)hFontUI, TRUE);
    EnableWindow(hStopBtn, FALSE);

    HWND hClrBtn = CreateWindow("BUTTON", "清空日志", 
        WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        rect.right - margin - btnW2, curY, btnW2, btnH2, hwnd, 
        (HMENU)ID_CLEAR_LOG_BTN, NULL, NULL);
    SendMessage(hClrBtn, WM_SETFONT, (WPARAM)hFontUI, TRUE);

    // 状态标签
    hStatusLabel = CreateWindow("STATIC", "状态: 已停止", 
        WS_VISIBLE | WS_CHILD | SS_CENTER,
        margin + btnW2 * 2 + btnGap2 * 2, curY + Scale(6), 
        Scale(100), Scale(20), hwnd, (HMENU)ID_STATUS_LABEL, NULL, NULL);
    SendMessage(hStatusLabel, WM_SETFONT, (WPARAM)hFontUI, TRUE);

    curY += btnH2 + Scale(10);

    // ========== 日志区域 ==========
    HWND hLogLabel = CreateWindow("STATIC", "运行日志:", 
        WS_VISIBLE | WS_CHILD, 
        margin, curY, Scale(80), Scale(18), hwnd, NULL, NULL, NULL);
    SendMessage(hLogLabel, WM_SETFONT, (WPARAM)hFontUI, TRUE);
    
    curY += Scale(20);

    int logHeight = rect.bottom - curY - margin;
    hLogEdit = CreateWindow("EDIT", "", 
        WS_VISIBLE | WS_CHILD | WS_BORDER | WS_VSCROLL | 
        ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL, 
        margin, curY, winW - (margin * 2), logHeight, 
        hwnd, (HMENU)ID_LOG_EDIT, NULL, NULL);
    SendMessage(hLogEdit, WM_SETFONT, (WPARAM)hFontLog, TRUE);
    SendMessage(hLogEdit, EM_SETLIMITTEXT, 0, 0);
}

// ========== 服务器管理函数 ==========

void InitDefaultServer(void) {
    serverCount = 1;
    currentServerIndex = 0;
    strcpy(servers[0].name, "默认服务器");
    strcpy(servers[0].proxyAddr, "");
    strcpy(servers[0].proxyPort, "1080");
    strcpy(servers[0].proxyUser, "");
    strcpy(servers[0].proxyPass, "");
    strcpy(servers[0].tunName, "wintun");
    strcpy(servers[0].tunAddress, "172.18.0.2");
    strcpy(servers[0].tunMask, "255.255.255.0");
}

void RefreshServerCombo(void) {
    SendMessage(hServerCombo, CB_RESETCONTENT, 0, 0);
    
    if (serverCount == 0) return;
    
    // 创建索引数组
    int indices[MAX_SERVERS];
    for (int i = 0; i < serverCount; i++) {
        indices[i] = i;
    }
    
    // 按名称排序索引（不区分大小写）
    for (int i = 0; i < serverCount - 1; i++) {
        for (int j = 0; j < serverCount - 1 - i; j++) {
            if (_stricmp(servers[indices[j]].name, servers[indices[j + 1]].name) > 0) {
                int temp = indices[j];
                indices[j] = indices[j + 1];
                indices[j + 1] = temp;
            }
        }
    }
    
    // 按排序后的顺序添加到 combo box，并存储真实索引
    int currentComboIndex = 0;
    for (int i = 0; i < serverCount; i++) {
        int realIndex = indices[i];
        int comboIdx = (int)SendMessage(hServerCombo, CB_ADDSTRING, 0, (LPARAM)servers[realIndex].name);
        SendMessage(hServerCombo, CB_SETITEMDATA, comboIdx, (LPARAM)realIndex);
        if (realIndex == currentServerIndex) {
            currentComboIndex = i;
        }
    }
    
    SendMessage(hServerCombo, CB_SETCURSEL, currentComboIndex, 0);
}

void SwitchServer(int index) {
    if (index < 0 || index >= serverCount) return;
    currentServerIndex = index;
    SetControlValues();
    SaveConfig();
    char msg[512];
    snprintf(msg, sizeof(msg), "[系统] 已切换到服务器: %s\r\n", servers[index].name);
    AppendLog(msg);
}

void AddNewServer(void) {
    if (serverCount >= MAX_SERVERS) {
        MessageBox(hMainWindow, "服务器数量已达上限", "提示", MB_OK | MB_ICONWARNING);
        return;
    }
    
    char newName[MAX_NAME_LEN] = "新服务器";
    if (!ShowInputDialog(hMainWindow, "新增服务器", "请输入服务器名称:", newName, MAX_NAME_LEN)) {
        return;
    }
    
    for (int i = 0; i < serverCount; i++) {
        if (strcmp(servers[i].name, newName) == 0) {
            MessageBox(hMainWindow, "服务器名称已存在，请使用其他名称", "提示", MB_OK | MB_ICONWARNING);
            return;
        }
    }
    
    ServerConfig* newServer = &servers[serverCount];
    if (serverCount > 0) {
        memcpy(newServer, &servers[currentServerIndex], sizeof(ServerConfig));
    } else {
        memset(newServer, 0, sizeof(ServerConfig));
        strcpy(newServer->proxyPort, "1080");
        strcpy(newServer->tunName, "wintun");
        strcpy(newServer->tunAddress, "172.18.0.2");
        strcpy(newServer->tunMask, "255.255.255.0");
    }
    
    strcpy(newServer->name, newName);
    
    serverCount++;
    currentServerIndex = serverCount - 1;
    
    RefreshServerCombo();
    SetControlValues();
    SaveConfig();
    
    char logMsg[512];
    snprintf(logMsg, sizeof(logMsg), "[系统] 已添加新服务器: %s\r\n", newName);
    AppendLog(logMsg);
}

void SaveCurrentServer(void) {
    GetControlValues();
    SaveConfig();
    
    ServerConfig* cfg = GetCurrentServer();
    char logMsg[512];
    snprintf(logMsg, sizeof(logMsg), "[系统] 服务器 \"%s\" 配置已保存\r\n", cfg->name);
    AppendLog(logMsg);
}

void DeleteCurrentServer(void) {
    if (serverCount <= 1) {
        MessageBox(hMainWindow, "至少需要保留一个服务器配置", "提示", MB_OK | MB_ICONWARNING);
        return;
    }
    
    char msg[512];
    snprintf(msg, sizeof(msg), "确定要删除服务器 \"%s\" 吗？", servers[currentServerIndex].name);
    if (MessageBox(hMainWindow, msg, "确认删除", MB_YESNO | MB_ICONQUESTION) != IDYES) {
        return;
    }
    
    char deletedName[MAX_NAME_LEN];
    strcpy(deletedName, servers[currentServerIndex].name);
    
    for (int i = currentServerIndex; i < serverCount - 1; i++) {
        memcpy(&servers[i], &servers[i + 1], sizeof(ServerConfig));
    }
    serverCount--;
    
    if (currentServerIndex >= serverCount) {
        currentServerIndex = serverCount - 1;
    }
    
    RefreshServerCombo();
    SetControlValues();
    SaveConfig();
    
    snprintf(msg, sizeof(msg), "[系统] 已删除服务器: %s\r\n", deletedName);
    AppendLog(msg);
}

void RenameCurrentServer(void) {
    char newName[MAX_NAME_LEN];
    strcpy(newName, servers[currentServerIndex].name);
    
    if (!ShowInputDialog(hMainWindow, "重命名服务器", "请输入新的服务器名称:", newName, MAX_NAME_LEN)) {
        return;
    }
    
    for (int i = 0; i < serverCount; i++) {
        if (i != currentServerIndex && strcmp(servers[i].name, newName) == 0) {
            MessageBox(hMainWindow, "服务器名称已存在，请使用其他名称", "提示", MB_OK | MB_ICONWARNING);
            return;
        }
    }
    
    char oldName[MAX_NAME_LEN];
    strcpy(oldName, servers[currentServerIndex].name);
    strcpy(servers[currentServerIndex].name, newName);
    
    RefreshServerCombo();
    SaveConfig();
    
    char logMsg[512];
    snprintf(logMsg, sizeof(logMsg), "[系统] 服务器已重命名: %s -> %s\r\n", oldName, newName);
    AppendLog(logMsg);
}

ServerConfig* GetCurrentServer(void) {
    if (currentServerIndex >= 0 && currentServerIndex < serverCount) {
        return &servers[currentServerIndex];
    }
    return &servers[0];
}

void GetControlValues(void) {
    ServerConfig* cfg = GetCurrentServer();
    
    GetWindowText(hProxyAddrEdit, cfg->proxyAddr, sizeof(cfg->proxyAddr));
    GetWindowText(hProxyPortEdit, cfg->proxyPort, sizeof(cfg->proxyPort));
    GetWindowText(hProxyUserEdit, cfg->proxyUser, sizeof(cfg->proxyUser));
    GetWindowText(hProxyPassEdit, cfg->proxyPass, sizeof(cfg->proxyPass));
    GetWindowText(hTunNameEdit, cfg->tunName, sizeof(cfg->tunName));
    GetWindowText(hTunAddrEdit, cfg->tunAddress, sizeof(cfg->tunAddress));
    GetWindowText(hTunMaskEdit, cfg->tunMask, sizeof(cfg->tunMask));
}

void SetControlValues(void) {
    ServerConfig* cfg = GetCurrentServer();
    
    SetWindowText(hProxyAddrEdit, cfg->proxyAddr);
    SetWindowText(hProxyPortEdit, cfg->proxyPort);
    SetWindowText(hProxyUserEdit, cfg->proxyUser);
    SetWindowText(hProxyPassEdit, cfg->proxyPass);
    SetWindowText(hTunNameEdit, cfg->tunName);
    SetWindowText(hTunAddrEdit, cfg->tunAddress);
    SetWindowText(hTunMaskEdit, cfg->tunMask);
}

void StartProxy(void) {
    ServerConfig* cfg = GetCurrentServer();
    char cmdLine[MAX_CMD_LEN];
    char exePath[MAX_PATH] = "tun2socks.exe";
    
    // 检查 tun2socks 是否存在
    if (GetFileAttributes(exePath) == INVALID_FILE_ATTRIBUTES) {
        AppendLog("[错误] 找不到 tun2socks.exe 文件!\r\n");
        MessageBox(hMainWindow, 
            "找不到 tun2socks.exe 文件!\n\n"
            "请确保该文件与本程序在同一目录下。",
            "错误", MB_OK | MB_ICONERROR);
        return;
    }
    
    // 构建代理URL并解析服务器IP
    if (!BuildProxyUrl()) {
        return;
    }
    
    // 构建命令行
    snprintf(cmdLine, MAX_CMD_LEN, 
        "\"%s\" -device tun://%s -proxy %s -loglevel info",
        exePath, cfg->tunName, g_proxyUrl);
    
    AppendLogFormat("[系统] 启动 tun2socks...\r\n");

    // 创建管道读取输出
    SECURITY_ATTRIBUTES sa;
    memset(&sa, 0, sizeof(sa));
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = TRUE;
    
    HANDLE hRead, hWrite;
    if (!CreatePipe(&hRead, &hWrite, &sa, 0)) {
        AppendLog("[错误] 创建管道失败\r\n");
        return;
    }

    STARTUPINFO si;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.hStdOutput = hWrite;
    si.hStdError = hWrite;
    si.wShowWindow = SW_HIDE;

    memset(&processInfo, 0, sizeof(processInfo));
    
    if (CreateProcess(NULL, cmdLine, NULL, NULL, TRUE, 
        CREATE_NO_WINDOW, NULL, NULL, &si, &processInfo)) {
        
        CloseHandle(hWrite);
        hLogPipe = hRead;
        isProcessRunning = TRUE;
        
        hLogThread = CreateThread(NULL, 0, LogReaderThread, NULL, 0, NULL);
        
        // 更新界面状态
        EnableWindow(hStartBtn, FALSE);
        EnableWindow(hStopBtn, TRUE);
        EnableWindow(hProxyAddrEdit, FALSE);
        EnableWindow(hProxyPortEdit, FALSE);
        EnableWindow(hProxyUserEdit, FALSE);
        EnableWindow(hProxyPassEdit, FALSE);
        EnableWindow(hTunNameEdit, FALSE);
        EnableWindow(hTunAddrEdit, FALSE);
        EnableWindow(hTunMaskEdit, FALSE);
        EnableWindow(hServerCombo, FALSE);
        UpdateStatus("状态: 运行中");
        
        AppendLogFormat("[系统] TUN2SOCKS 进程已启动 (服务器: %s)\r\n", cfg->name);
        
        // 在后台线程中配置 TUN 网卡
        CreateThread(NULL, 0, ConfigureTunThread, NULL, 0, NULL);
        
    } else {
        CloseHandle(hRead);
        CloseHandle(hWrite);
        AppendLogFormat("[错误] 启动失败，错误代码: %lu\r\n", GetLastError());
    }
}

void StopProxy(void) {
    AppendLog("[系统] 正在停止代理...\r\n");
    
    isProcessRunning = FALSE;

    if (hLogPipe) {
        CloseHandle(hLogPipe);
        hLogPipe = NULL;
    }

    if (processInfo.hProcess) {
        TerminateProcess(processInfo.hProcess, 0);
        WaitForSingleObject(processInfo.hProcess, 3000);
        CloseHandle(processInfo.hProcess);
        CloseHandle(processInfo.hThread);
        memset(&processInfo, 0, sizeof(processInfo));
    }

    if (hLogThread) {
        if (WaitForSingleObject(hLogThread, 1000) == WAIT_TIMEOUT) {
            TerminateThread(hLogThread, 0);
        }
        CloseHandle(hLogThread);
        hLogThread = NULL;
    }
    
    if (IsWindow(hMainWindow)) {
        EnableWindow(hStartBtn, TRUE);
        EnableWindow(hStopBtn, FALSE);
        EnableWindow(hProxyAddrEdit, TRUE);
        EnableWindow(hProxyPortEdit, TRUE);
        EnableWindow(hProxyUserEdit, TRUE);
        EnableWindow(hProxyPassEdit, TRUE);
        EnableWindow(hTunNameEdit, TRUE);
        EnableWindow(hTunAddrEdit, TRUE);
        EnableWindow(hTunMaskEdit, TRUE);
        EnableWindow(hServerCombo, TRUE);
        UpdateStatus("状态: 已停止");
        AppendLog("[系统] 代理已停止\r\n");
    }
}

void AppendLogAsync(const char* text) {
    if (!text) return;
    char* msgCopy = _strdup(text); 
    if (msgCopy) {
        if (!PostMessage(hMainWindow, WM_APPEND_LOG, 0, (LPARAM)msgCopy)) {
            free(msgCopy);
        }
    }
}

DWORD WINAPI LogReaderThread(LPVOID lpParam) {
    (void)lpParam;
    char buf[1024];
    char u8Buf[2048];
    DWORD bytesRead;
    
    while (isProcessRunning && hLogPipe) {
        if (ReadFile(hLogPipe, buf, sizeof(buf)-1, &bytesRead, NULL) && bytesRead > 0) {
            buf[bytesRead] = 0;
            
            int wLen = MultiByteToWideChar(CP_UTF8, 0, buf, -1, NULL, 0);
            if (wLen > 0) {
                WCHAR* wBuf = (WCHAR*)malloc((size_t)wLen * sizeof(WCHAR));
                if (wBuf) {
                    MultiByteToWideChar(CP_UTF8, 0, buf, -1, wBuf, wLen);
                    WideCharToMultiByte(CP_ACP, 0, wBuf, -1, u8Buf, sizeof(u8Buf), NULL, NULL);
                    AppendLogAsync(u8Buf);
                    free(wBuf);
                }
            } else {
                AppendLogAsync(buf);
            }
        } else {
            break; 
        }
    }
    return 0;
}

void AppendLog(const char* text) {
    if (!IsWindow(hLogEdit)) return;
    
    int len = GetWindowTextLength(hLogEdit);
    if (len > 50000) {
        SetWindowText(hLogEdit, "");
        len = 0;
    }
    
    SendMessage(hLogEdit, EM_SETSEL, len, len);
    SendMessage(hLogEdit, EM_REPLACESEL, FALSE, (LPARAM)text);
    SendMessage(hLogEdit, EM_SCROLLCARET, 0, 0);
}

void AppendLogFormat(const char* fmt, ...) {
    char buf[2048];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    AppendLog(buf);
}

void SaveConfig(void) {
    FILE* f = fopen("config.ini", "w");
    if (!f) return;
    
    fprintf(f, "[Settings]\n");
    fprintf(f, "current_server=%d\n", currentServerIndex);
    fprintf(f, "server_count=%d\n\n", serverCount);
    
    for (int i = 0; i < serverCount; i++) {
        fprintf(f, "[Server%d]\n", i);
        fprintf(f, "name=%s\n", servers[i].name);
        fprintf(f, "proxyAddr=%s\n", servers[i].proxyAddr);
        fprintf(f, "proxyPort=%s\n", servers[i].proxyPort);
        fprintf(f, "proxyUser=%s\n", servers[i].proxyUser);
        fprintf(f, "proxyPass=%s\n", servers[i].proxyPass);
        fprintf(f, "tunName=%s\n", servers[i].tunName);
        fprintf(f, "tunAddress=%s\n", servers[i].tunAddress);
        fprintf(f, "tunMask=%s\n\n", servers[i].tunMask);
    }
    
    fclose(f);
}

void LoadConfig(void) {
    FILE* f = fopen("config.ini", "r");
    if (!f) return;
    
    char line[MAX_URL_LEN];
    int currentSection = -1;
    
    while (fgets(line, sizeof(line), f)) {
        char* nl = strchr(line, '\n');
        if (nl) *nl = 0;
        char* cr = strchr(line, '\r');
        if (cr) *cr = 0;
        
        if (line[0] == 0 || line[0] == ';' || line[0] == '#') continue;
        
        if (line[0] == '[') {
            if (strncmp(line, "[Settings]", 10) == 0) {
                currentSection = -1;
            } else if (strncmp(line, "[Server", 7) == 0) {
                int idx;
                if (sscanf(line, "[Server%d]", &idx) == 1) {
                    currentSection = idx;
                }
            }
            continue;
        }
        
        char* val = strchr(line, '=');
        if (!val) continue;
        *val++ = 0;
        
        if (currentSection == -1) {
            if (strcmp(line, "current_server") == 0) {
                currentServerIndex = atoi(val);
            } else if (strcmp(line, "server_count") == 0) {
                serverCount = atoi(val);
                if (serverCount > MAX_SERVERS) serverCount = MAX_SERVERS;
                if (serverCount < 0) serverCount = 0;
            }
        } else if (currentSection >= 0 && currentSection < MAX_SERVERS) {
            ServerConfig* srv = &servers[currentSection];
            if (strcmp(line, "name") == 0) {
                strncpy(srv->name, val, MAX_NAME_LEN - 1);
                srv->name[MAX_NAME_LEN - 1] = 0;
            } else if (strcmp(line, "proxyAddr") == 0) {
                strncpy(srv->proxyAddr, val, MAX_SMALL_LEN - 1);
            } else if (strcmp(line, "proxyPort") == 0) {
                strncpy(srv->proxyPort, val, sizeof(srv->proxyPort) - 1);
            } else if (strcmp(line, "proxyUser") == 0) {
                strncpy(srv->proxyUser, val, MAX_SMALL_LEN - 1);
            } else if (strcmp(line, "proxyPass") == 0) {
                strncpy(srv->proxyPass, val, MAX_SMALL_LEN - 1);
            } else if (strcmp(line, "tunName") == 0) {
                strncpy(srv->tunName, val, MAX_SMALL_LEN - 1);
            } else if (strcmp(line, "tunAddress") == 0) {
                strncpy(srv->tunAddress, val, MAX_SMALL_LEN - 1);
            } else if (strcmp(line, "tunMask") == 0) {
                strncpy(srv->tunMask, val, MAX_SMALL_LEN - 1);
            }
        }
    }
    
    fclose(f);
    
    if (currentServerIndex < 0 || currentServerIndex >= serverCount) {
        currentServerIndex = 0;
    }
}