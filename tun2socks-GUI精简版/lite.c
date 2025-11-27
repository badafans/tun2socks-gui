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

// 消息定义
#define WM_TRAYICON (WM_USER + 1)
#define WM_APPEND_LOG (WM_USER + 2) 

// 托盘菜单ID
#define ID_TRAY_ICON 9001
#define ID_TRAY_OPEN 9002
#define ID_TRAY_EXIT 9003

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
#define ID_PROXY_ADDR_EDIT  1001
#define ID_PROXY_PORT_EDIT  1002
#define ID_PROXY_USER_EDIT  1003
#define ID_PROXY_PASS_EDIT  1004
#define ID_TUN_NAME_EDIT    1005
#define ID_TUN_ADDR_EDIT    1007
#define ID_TUN_MASK_EDIT    1008
#define ID_START_BTN        1013
#define ID_STOP_BTN         1014
#define ID_CLEAR_LOG_BTN    1015
#define ID_LOG_EDIT         1016
#define ID_STATUS_LABEL     1017

// 全局变量
HWND hMainWindow;
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
    char proxyAddr[MAX_SMALL_LEN];     // 代理服务器地址
    char proxyPort[16];                 // 代理端口
    char proxyUser[MAX_SMALL_LEN];     // 用户名（可选）
    char proxyPass[MAX_SMALL_LEN];     // 密码（可选）
    char tunName[MAX_SMALL_LEN];       // TUN 设备名称
    char tunAddress[MAX_SMALL_LEN];    // TUN 地址
    char tunMask[MAX_SMALL_LEN];       // TUN 子网掩码
} Config;

Config currentConfig = {
    "",                          // proxyAddr
    "1080",                      // proxyPort
    "",                          // proxyUser
    "",                          // proxyPass
    "wintun",                    // tunName
    "172.18.0.2",                // tunAddress
    "255.255.255.0",             // tunMask
};

// 函数声明
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
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
    // 如果已经是IP地址，直接返回
    if (IsValidIpAddress(host)) {
        strncpy(outIp, host, maxLen - 1);
        outIp[maxLen - 1] = '\0';
        return TRUE;
    }
    
    // 解析域名
    struct addrinfo hints, *result = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;  // IPv4
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
    // 解析服务器IP
    if (!ResolveHostToIp(currentConfig.proxyAddr, g_serverIp, sizeof(g_serverIp))) {
        AppendLogFormat("[错误] 无法解析代理服务器地址: %s\r\n", currentConfig.proxyAddr);
        MessageBox(hMainWindow, 
            "无法解析代理服务器地址！\n\n"
            "请检查网络连接和 DNS 设置。",
            "错误", MB_OK | MB_ICONERROR);
        return FALSE;
    }
    
    // 如果输入的是域名，显示解析结果
    if (!IsValidIpAddress(currentConfig.proxyAddr)) {
        AppendLogFormat("[信息] 代理服务器 %s 解析为: %s\r\n", 
            currentConfig.proxyAddr, g_serverIp);
    } else {
        AppendLogFormat("[信息] 代理服务器IP: %s\r\n", g_serverIp);
    }
    
    // 构建代理URL - 使用解析后的IP地址
    if (strlen(currentConfig.proxyUser) > 0 && strlen(currentConfig.proxyPass) > 0) {
        // 带认证
        snprintf(g_proxyUrl, sizeof(g_proxyUrl), "socks5://%s:%s@%s:%s",
            currentConfig.proxyUser, currentConfig.proxyPass,
            g_serverIp, currentConfig.proxyPort);
    } else {
        // 无认证
        snprintf(g_proxyUrl, sizeof(g_proxyUrl), "socks5://%s:%s",
            g_serverIp, currentConfig.proxyPort);
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
    
    AppendLogAsync("[网络] 等待 TUN 设备就绪...\r\n");
    Sleep(5000);
    
    // 设置 TUN 网卡 IP 和子网掩码（不设置网关）
    AppendLogAsync("[网络] 配置 TUN 网卡 IP...\r\n");
    snprintf(cmd, sizeof(cmd), 
        "netsh interface ip set address name=\"%s\" static %s %s",
        currentConfig.tunName, currentConfig.tunAddress, currentConfig.tunMask);
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
            currentConfig.tunName, 
            currentConfig.tunAddress,
            currentConfig.tunMask,
            currentConfig.proxyAddr, currentConfig.proxyPort);
        AppendLogAsync(msg);
    }
    AppendLogAsync("========================================\r\n");
    
    return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, 
                   LPSTR lpCmdLine, int nCmdShow) {
    (void)hPrevInstance; 
    (void)lpCmdLine;
    
    memset(&processInfo, 0, sizeof(processInfo));
    
    // 初始化 Winsock（用于域名解析）
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

    int winWidth = Scale(580);
    int winHeight = Scale(490);  // 进一步减小窗口高度
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);

    DWORD winStyle = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | 
                     WS_MINIMIZEBOX | WS_CLIPCHILDREN;

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
            SetControlValues();
            break;

        case WM_SYSCOMMAND:
            if ((wParam & 0xFFF0) == SC_MINIMIZE) {
                ShowWindow(hwnd, SW_HIDE); 
                ShowTrayIcon();            
                return 0;                  
            }
            return DefWindowProc(hwnd, uMsg, wParam, lParam);

        case WM_TRAYICON:
            if (lParam == WM_LBUTTONUP) {
                ShowWindow(hwnd, SW_RESTORE);
                SetForegroundWindow(hwnd);
                RemoveTrayIcon();
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
                    RemoveTrayIcon();
                    break;
                
                case ID_TRAY_EXIT:
                    SendMessage(hwnd, WM_CLOSE, 0, 0);
                    break;

                case ID_START_BTN:
                    if (!isProcessRunning) {
                        GetControlValues();
                        
                        // 验证必填项
                        if (strlen(currentConfig.proxyAddr) == 0) {
                            MessageBox(hwnd, "请输入代理服务器地址", 
                                "提示", MB_OK | MB_ICONWARNING);
                            SetFocus(hProxyAddrEdit);
                            break;
                        }
                        if (strlen(currentConfig.proxyPort) == 0) {
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
            if (isProcessRunning) {
                int result = MessageBox(hwnd, 
                    "代理正在运行中，确定要停止并退出吗？", 
                    "确认退出", MB_YESNO | MB_ICONQUESTION);
                if (result != IDYES) {
                    return 0;
                }
                StopProxy();
            }
            RemoveTrayIcon();
            GetControlValues();
            SaveConfig();
            DestroyWindow(hwnd);
            break;

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

    // ========== 代理配置组 ==========
    int group1H = Scale(130);
    HWND hGroup1 = CreateWindow("BUTTON", "SOCKS5 代理配置", 
        WS_VISIBLE | WS_CHILD | BS_GROUPBOX,
        margin, curY, groupW, group1H, hwnd, NULL, NULL, NULL);
    SendMessage(hGroup1, WM_SETFONT, (WPARAM)hFontUI, TRUE);
    
    int innerX = margin + Scale(15);
    int innerY = curY + Scale(22);
    
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
    int group2H = Scale(75);  // 减小高度，因为只有一行配置
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
    int btnW = Scale(100);
    int btnH = Scale(32);
    int btnGap = Scale(15);

    hStartBtn = CreateWindow("BUTTON", "启动代理", 
        WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON,
        margin, curY, btnW, btnH, hwnd, (HMENU)ID_START_BTN, NULL, NULL);
    SendMessage(hStartBtn, WM_SETFONT, (WPARAM)hFontUI, TRUE);

    hStopBtn = CreateWindow("BUTTON", "停止", 
        WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        margin + btnW + btnGap, curY, btnW, btnH, hwnd, (HMENU)ID_STOP_BTN, NULL, NULL);
    SendMessage(hStopBtn, WM_SETFONT, (WPARAM)hFontUI, TRUE);
    EnableWindow(hStopBtn, FALSE);

    HWND hClrBtn = CreateWindow("BUTTON", "清空日志", 
        WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        rect.right - margin - btnW, curY, btnW, btnH, hwnd, 
        (HMENU)ID_CLEAR_LOG_BTN, NULL, NULL);
    SendMessage(hClrBtn, WM_SETFONT, (WPARAM)hFontUI, TRUE);

    // 状态标签
    hStatusLabel = CreateWindow("STATIC", "状态: 已停止", 
        WS_VISIBLE | WS_CHILD | SS_CENTER,
        margin + btnW * 2 + btnGap * 2, curY + Scale(6), 
        Scale(100), Scale(20), hwnd, (HMENU)ID_STATUS_LABEL, NULL, NULL);
    SendMessage(hStatusLabel, WM_SETFONT, (WPARAM)hFontUI, TRUE);

    curY += btnH + Scale(10);

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

void GetControlValues(void) {
    GetWindowText(hProxyAddrEdit, currentConfig.proxyAddr, sizeof(currentConfig.proxyAddr));
    GetWindowText(hProxyPortEdit, currentConfig.proxyPort, sizeof(currentConfig.proxyPort));
    GetWindowText(hProxyUserEdit, currentConfig.proxyUser, sizeof(currentConfig.proxyUser));
    GetWindowText(hProxyPassEdit, currentConfig.proxyPass, sizeof(currentConfig.proxyPass));
    GetWindowText(hTunNameEdit, currentConfig.tunName, sizeof(currentConfig.tunName));
    GetWindowText(hTunAddrEdit, currentConfig.tunAddress, sizeof(currentConfig.tunAddress));
    GetWindowText(hTunMaskEdit, currentConfig.tunMask, sizeof(currentConfig.tunMask));
}

void SetControlValues(void) {
    SetWindowText(hProxyAddrEdit, currentConfig.proxyAddr);
    SetWindowText(hProxyPortEdit, currentConfig.proxyPort);
    SetWindowText(hProxyUserEdit, currentConfig.proxyUser);
    SetWindowText(hProxyPassEdit, currentConfig.proxyPass);
    SetWindowText(hTunNameEdit, currentConfig.tunName);
    SetWindowText(hTunAddrEdit, currentConfig.tunAddress);
    SetWindowText(hTunMaskEdit, currentConfig.tunMask);
}

void StartProxy(void) {
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
    
    // 构建命令行（不包含网关参数）
    snprintf(cmdLine, MAX_CMD_LEN, 
        "\"%s\" -device tun://%s -proxy %s -loglevel info",
        exePath, currentConfig.tunName, g_proxyUrl);
    
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
        UpdateStatus("状态: 运行中");
        
        AppendLog("[系统] TUN2SOCKS 进程已启动\r\n");
        
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
    
    fprintf(f, "[TUN2SOCKS]\n");
    fprintf(f, "proxyAddr=%s\n", currentConfig.proxyAddr);
    fprintf(f, "proxyPort=%s\n", currentConfig.proxyPort);
    fprintf(f, "proxyUser=%s\n", currentConfig.proxyUser);
    fprintf(f, "proxyPass=%s\n", currentConfig.proxyPass);
    fprintf(f, "tunName=%s\n", currentConfig.tunName);
    fprintf(f, "tunAddress=%s\n", currentConfig.tunAddress);
    fprintf(f, "tunMask=%s\n", currentConfig.tunMask);
    
    fclose(f);
}

void LoadConfig(void) {
    FILE* f = fopen("config.ini", "r");
    if (!f) return;
    
    char line[MAX_URL_LEN];
    while (fgets(line, sizeof(line), f)) {
        char* nl = strchr(line, '\n');
        if (nl) *nl = 0;
        char* cr = strchr(line, '\r');
        if (cr) *cr = 0;
        
        char* val = strchr(line, '=');
        if (!val) continue;
        *val++ = 0;

        if (!strcmp(line, "proxyAddr")) 
            strncpy(currentConfig.proxyAddr, val, sizeof(currentConfig.proxyAddr) - 1);
        else if (!strcmp(line, "proxyPort")) 
            strncpy(currentConfig.proxyPort, val, sizeof(currentConfig.proxyPort) - 1);
        else if (!strcmp(line, "proxyUser")) 
            strncpy(currentConfig.proxyUser, val, sizeof(currentConfig.proxyUser) - 1);
        else if (!strcmp(line, "proxyPass")) 
            strncpy(currentConfig.proxyPass, val, sizeof(currentConfig.proxyPass) - 1);
        else if (!strcmp(line, "tunName")) 
            strncpy(currentConfig.tunName, val, sizeof(currentConfig.tunName) - 1);
        else if (!strcmp(line, "tunAddress")) 
            strncpy(currentConfig.tunAddress, val, sizeof(currentConfig.tunAddress) - 1);
        else if (!strcmp(line, "tunMask")) 
            strncpy(currentConfig.tunMask, val, sizeof(currentConfig.tunMask) - 1);
    }
    fclose(f);
}