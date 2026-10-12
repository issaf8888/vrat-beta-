#define UNICODE
#define _UNICODE

#include <windows.h>
#include <propidl.h>
#include <gdiplus.h>
#include <vector>
#include <string>
#include <memory>
#include <fstream>
#include <sstream>

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "dwmapi.lib")

using namespace Gdiplus;

// --- Configuration & Constants ---
// Ultra-wide dashboard dimensions (1160 x 560)
const int WINDOW_WIDTH = 1160;
const int WINDOW_HEIGHT = 560;
const int CORNER_RADIUS = 20;
const int SIDEBAR_WIDTH = 78;
const int HEADER_HEIGHT = 58;

// Fallback embedded ASCII art in case f.txt is relocated
const char* FALLBACK_ASCII_RAT =
"\n"
"             xxxxxxxxxxxxxxxxxxxxxxxxxxxx\n"
"          xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx     __\n"
"        xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx// \\\n"
"       xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx((_  )x\n"
"      xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx_xxxxx\n"
"     xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx(o)xxxxxxx\n"
"    rrxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxo\n"
"   rrrxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx/|\\xx/\n"
"  rrrrxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx__/x| \\\n"
" rrr   xwxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx\n"
"rrr      wwwxxxxxxxxx ..                    xwwww...\n"
"rrr        wwwxxxxxxx .                       www ..\n"
"rrr          wwwww  .....                      ====...\n"
" rrr          =========\n"
"  rrr\n"
"   'rr._\n"
"     ''rrrrrrrrrrrrrrrr\n";

struct MenuItem {
    std::wstring filename;
    std::wstring label;
    std::unique_ptr<Gdiplus::Bitmap> icon;
    RECT rect;
};

std::wstring GetExecutableDir() {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring ws(path);
    size_t pos = ws.find_last_of(L"\\/");
    return (pos != std::wstring::npos) ? ws.substr(0, pos + 1) : L"";
}

// Display terminal window for 2 seconds with rat ASCII in dark blue to white horizontal gradient
void ShowStartupTerminal() {
    HWND hCon = GetConsoleWindow();
    bool allocated = false;
    if (!hCon) {
        AllocConsole();
        hCon = GetConsoleWindow();
        allocated = true;
    }

    if (hCon) {
        SetConsoleTitleW(L"vrat");
        ShowWindow(hCon, SW_SHOW);
        SetForegroundWindow(hCon);
    }

    HANDLE hOut = CreateFileW(L"CONOUT$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (hOut == INVALID_HANDLE_VALUE) {
        hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    }

    DWORD mode = 0;
    GetConsoleMode(hOut, &mode);
    SetConsoleMode(hOut, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);

    // Hide cursor & clear terminal screen
    std::string initSeq = "\x1b[?25l\x1b[2J\x1b[H";
    DWORD written = 0;
    WriteConsoleA(hOut, initSeq.c_str(), (DWORD)initSeq.length(), &written, NULL);

    // Read f.txt from executable dir or working dir
    std::vector<std::string> lines;
    std::wstring txtPath = GetExecutableDir() + L"f.txt";
    std::ifstream file(txtPath.c_str());
    if (!file.is_open()) {
        file.open("f.txt");
    }

    if (file.is_open()) {
        std::string line;
        while (std::getline(file, line)) {
            lines.push_back(line);
        }
        file.close();
    } else {
        std::istringstream iss(FALLBACK_ASCII_RAT);
        std::string line;
        while (std::getline(iss, line)) {
            lines.push_back(line);
        }
    }

    size_t maxLen = 0;
    for (const auto& l : lines) {
        if (l.length() > maxLen) maxLen = l.length();
    }
    if (maxLen == 0) maxLen = 65;

    // Build colored output with horizontal gradient: Dark Blue (18, 50, 180) -> White (255, 255, 255)
    std::string output = "\n";
    for (const auto& line : lines) {
        for (size_t col = 0; col < line.length(); ++col) {
            float t = (maxLen > 1) ? (float)col / (float)(maxLen - 1) : 0.0f;
            if (t > 1.0f) t = 1.0f;

            int r = (int)(18.0f + t * (255.0f - 18.0f));
            int g = (int)(50.0f + t * (255.0f - 50.0f));
            int b = (int)(180.0f + t * (255.0f - 180.0f));

            char buf[64];
            snprintf(buf, sizeof(buf), "\x1b[38;2;%d;%d;%dm%c", r, g, b, line[col]);
            output += buf;
        }
        output += "\x1b[0m\n";
    }

    output += "\n\x1b[38;2;100;160;255m  Initializing vrat...\x1b[0m\n";
    WriteConsoleA(hOut, output.c_str(), (DWORD)output.length(), &written, NULL);

    // Terminal stays visible for exactly 2 seconds as requested
    Sleep(2000);

    // Hide / close terminal window
    if (hCon) {
        ShowWindow(hCon, SW_HIDE);
    }
    if (allocated) {
        FreeConsole();
    }
    if (hOut != INVALID_HANDLE_VALUE && hOut != GetStdHandle(STD_OUTPUT_HANDLE)) {
        CloseHandle(hOut);
    }
}

class VratMenuApp {
public:
    VratMenuApp(HINSTANCE hInstance) : m_hInstance(hInstance), m_hwnd(nullptr) {
        initMenuItems();
    }

    ~VratMenuApp() {
        if (m_memDC) DeleteDC(m_memDC);
        if (m_hDIB) DeleteObject(m_hDIB);
    }

    bool initialize() {
        WNDCLASSEXW wc = { sizeof(WNDCLASSEXW) };
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = StaticWndProc;
        wc.cbClsExtra = 0;
        wc.cbWndExtra = sizeof(VratMenuApp*);
        wc.hInstance = m_hInstance;
        wc.hIcon = LoadIconW(m_hInstance, MAKEINTRESOURCEW(1));
        wc.hIconSm = (HICON)LoadImageW(m_hInstance, MAKEINTRESOURCEW(1), IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR);
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = nullptr;
        wc.lpszClassName = L"VratMenuWindowClass";

        if (!RegisterClassExW(&wc)) return false;

        // Position window in center of screen
        int screenW = GetSystemMetrics(SM_CXSCREEN);
        int screenH = GetSystemMetrics(SM_CYSCREEN);
        int posX = (screenW - WINDOW_WIDTH) / 2;
        int posY = (screenH - WINDOW_HEIGHT) / 2;

        DWORD exStyle = WS_EX_LAYERED | WS_EX_APPWINDOW;
        DWORD style = WS_POPUP;

        m_hwnd = CreateWindowExW(
            exStyle,
            wc.lpszClassName,
            L"vrat",
            style,
            posX, posY, WINDOW_WIDTH, WINDOW_HEIGHT,
            nullptr, nullptr, m_hInstance, this
        );

        if (!m_hwnd) return false;

        if (wc.hIcon) SendMessageW(m_hwnd, WM_SETICON, ICON_BIG, (LPARAM)wc.hIcon);
        if (wc.hIconSm) SendMessageW(m_hwnd, WM_SETICON, ICON_SMALL, (LPARAM)wc.hIconSm);

        initGraphicsBuffer();
        loadIcons();
        render();

        ShowWindow(m_hwnd, SW_SHOW);
        UpdateWindow(m_hwnd);

        return true;
    }

    void run() {
        MSG msg;
        while (GetMessageW(&msg, nullptr, 0, 0)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

private:
    HINSTANCE m_hInstance;
    HWND m_hwnd;
    HDC m_memDC = nullptr;
    HBITMAP m_hDIB = nullptr;
    void* m_pBits = nullptr;

    std::unique_ptr<Gdiplus::Bitmap> m_logo;
    std::vector<MenuItem> m_items;
    int m_hoveredIndex = -1;
    int m_selectedIndex = 0; // 0 = Home, 1 = Overview, etc.

    // Window control buttons (Top-Right)
    RECT m_closeBtnRect = { WINDOW_WIDTH - 44, 15, WINDOW_WIDTH - 16, 43 };
    RECT m_minBtnRect = { WINDOW_WIDTH - 78, 15, WINDOW_WIDTH - 50, 43 };
    bool m_hoverClose = false;
    bool m_hoverMin = false;
    bool m_isMouseTracking = false;

    void initMenuItems() {
        m_items.push_back({ L"home icon.png", L"Home", nullptr, {} });
        m_items.push_back({ L"overview.png", L"Overview", nullptr, {} });
        m_items.push_back({ L"injections.png", L"Injections", nullptr, {} });
        m_items.push_back({ L"remote.png", L"Remote", nullptr, {} });
        m_items.push_back({ L"file export.png", L"File Export", nullptr, {} });
        m_items.push_back({ L"webcam.png", L"Webcam", nullptr, {} });
        m_items.push_back({ L"build.png", L"Build", nullptr, {} });

        // Calculate item rectangles in the sidebar
        int startY = HEADER_HEIGHT + 16;
        int itemSize = 46;
        int itemGap = 12;
        int startX = (SIDEBAR_WIDTH - itemSize) / 2;

        for (size_t i = 0; i < m_items.size(); ++i) {
            int y = startY + (int)i * (itemSize + itemGap);
            m_items[i].rect = { startX, y, startX + itemSize, y + itemSize };
        }
    }

    void loadIcons() {
        std::wstring baseDir = GetExecutableDir();

        // Load corner logo (logo.png or logo (2).png)
        std::wstring logoCandidates[] = {
            baseDir + L"logo.png",
            baseDir + L"logo (2).png",
            L"logo.png",
            L"logo (2).png"
        };
        for (const auto& path : logoCandidates) {
            m_logo = std::make_unique<Gdiplus::Bitmap>(path.c_str());
            if (m_logo->GetLastStatus() == Gdiplus::Ok) {
                break;
            }
        }

        // Load menu icons
        for (auto& item : m_items) {
            std::wstring fullPath = baseDir + item.filename;
            item.icon = std::make_unique<Gdiplus::Bitmap>(fullPath.c_str());
            if (item.icon->GetLastStatus() != Gdiplus::Ok) {
                // Fallback to relative path in current working directory
                item.icon = std::make_unique<Gdiplus::Bitmap>(item.filename.c_str());
            }
        }
    }

    void initGraphicsBuffer() {
        HDC screenDC = GetDC(nullptr);
        m_memDC = CreateCompatibleDC(screenDC);

        BITMAPINFO bmi = {};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = WINDOW_WIDTH;
        bmi.bmiHeader.biHeight = -WINDOW_HEIGHT; // Top-down DIB
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        m_hDIB = CreateDIBSection(m_memDC, &bmi, DIB_RGB_COLORS, &m_pBits, nullptr, 0);
        SelectObject(m_memDC, m_hDIB);
        ReleaseDC(nullptr, screenDC);
    }

    void addRoundedRectPath(GraphicsPath& path, const RectF& rect, float radius) {
        float diameter = radius * 2.0f;
        path.AddArc(rect.X, rect.Y, diameter, diameter, 180.0f, 90.0f);
        path.AddArc(rect.X + rect.Width - diameter, rect.Y, diameter, diameter, 270.0f, 90.0f);
        path.AddArc(rect.X + rect.Width - diameter, rect.Y + rect.Height - diameter, diameter, diameter, 0.0f, 90.0f);
        path.AddArc(rect.X, rect.Y + rect.Height - diameter, diameter, diameter, 90.0f, 90.0f);
        path.CloseFigure();
    }

    void render() {
        if (!m_memDC || !m_hwnd) return;

        Graphics g(m_memDC);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
        g.SetPixelOffsetMode(PixelOffsetModeHighQuality);
        g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);

        // Clear transparent background
        g.Clear(Color(0, 0, 0, 0));

        // 1. Sleek subtle outer glow / shadow
        for (int i = 3; i >= 1; --i) {
            GraphicsPath shadowPath;
            RectF shadowRect(
                (float)(1 - i),
                (float)(1 - i),
                (float)(WINDOW_WIDTH - 2 + i * 2),
                (float)(WINDOW_HEIGHT - 2 + i * 2)
            );
            addRoundedRectPath(shadowPath, shadowRect, (float)(CORNER_RADIUS + i));
            SolidBrush shadowBrush(Color((BYTE)(14 - i * 3), 0, 0, 0));
            g.FillPath(&shadowBrush, &shadowPath);
        }

        // 2. Main Window Background: Gray translucent (Frosted Dark Glass)
        // Alpha ~ 222, Deep modern slate/charcoal gray (RGB: 26, 28, 33)
        GraphicsPath mainBgPath;
        RectF mainRect(1.0f, 1.0f, (float)(WINDOW_WIDTH - 2), (float)(WINDOW_HEIGHT - 2));
        addRoundedRectPath(mainBgPath, mainRect, (float)CORNER_RADIUS);

        SolidBrush bgBrush(Color(222, 26, 28, 33));
        g.FillPath(&bgBrush, &mainBgPath);

        // Sleek subtle border: Crisp semi-transparent light stroke
        Pen borderPen(Color(65, 255, 255, 255), 1.0f);
        g.DrawPath(&borderPen, &mainBgPath);

        // Inner rim highlight
        Pen innerRimPen(Color(25, 255, 255, 255), 1.0f);
        RectF innerRimRect(2.0f, 2.0f, (float)(WINDOW_WIDTH - 4), (float)(WINDOW_HEIGHT - 4));
        GraphicsPath innerRimPath;
        addRoundedRectPath(innerRimPath, innerRimRect, (float)(CORNER_RADIUS - 1));
        g.DrawPath(&innerRimPen, &innerRimPath);

        // 3. Sidebar vertical divider line
        Pen dividerPen(Color(32, 255, 255, 255), 1.0f);
        g.DrawLine(&dividerPen, SIDEBAR_WIDTH, HEADER_HEIGHT, SIDEBAR_WIDTH, WINDOW_HEIGHT - 20);

        // Horizontal subtle divider below header
        Pen hDividerPen(Color(26, 255, 255, 255), 1.0f);
        g.DrawLine(&hDividerPen, 18, HEADER_HEIGHT, WINDOW_WIDTH - 18, HEADER_HEIGHT);

        // 4. Header: Top Left Corner Logo
        if (m_logo && m_logo->GetLastStatus() == Ok) {
            float logoH = 34.0f;
            float logoW = logoH * ((float)m_logo->GetWidth() / (float)m_logo->GetHeight());
            float logoX = ((float)SIDEBAR_WIDTH - logoW) / 2.0f;
            float logoY = ((float)HEADER_HEIGHT - logoH) / 2.0f;

            g.DrawImage(m_logo.get(), RectF(logoX, logoY, logoW, logoH),
                        0.0f, 0.0f, (float)m_logo->GetWidth(), (float)m_logo->GetHeight(),
                        UnitPixel);
        }

        // 5. Header: Top Center Name "vrat"
        FontFamily fontFamily(L"Segoe UI");
        Gdiplus::Font titleFont(&fontFamily, 18.0f, FontStyleBold, UnitPoint);

        StringFormat centerFormat;
        centerFormat.SetAlignment(StringAlignmentCenter);
        centerFormat.SetLineAlignment(StringAlignmentCenter);

        // Subtle shadow behind "vrat"
        RectF textShadowRect(0.0f, 2.0f, (float)WINDOW_WIDTH, (float)HEADER_HEIGHT);
        SolidBrush textShadowBrush(Color(120, 0, 0, 0));
        g.DrawString(L"vrat", -1, &titleFont, textShadowRect, &centerFormat, &textShadowBrush);

        // Main text "vrat" in crisp silver-white
        RectF titleRect(0.0f, 0.0f, (float)WINDOW_WIDTH, (float)HEADER_HEIGHT);
        SolidBrush textBrush(Color(250, 245, 246, 252));
        g.DrawString(L"vrat", -1, &titleFont, titleRect, &centerFormat, &textBrush);

        // 6. Window Controls (Top Right: Minimize and Close)
        renderWindowControls(g);

        // 7. Sidebar Menu Icons
        renderSidebarItems(g);

        // 8. Content Area (Overview -> "keine aktiven sessions")
        renderMainContent(g);

        // 9. Push to Layered Window
        POINT ptSrc = { 0, 0 };
        SIZE sizeWnd = { WINDOW_WIDTH, WINDOW_HEIGHT };
        POINT ptDst;
        RECT wndRect;
        GetWindowRect(m_hwnd, &wndRect);
        ptDst.x = wndRect.left;
        ptDst.y = wndRect.top;

        BLENDFUNCTION blend = {};
        blend.BlendOp = AC_SRC_OVER;
        blend.BlendFlags = 0;
        blend.SourceConstantAlpha = 255;
        blend.AlphaFormat = AC_SRC_ALPHA;

        HDC screenDC = GetDC(nullptr);
        UpdateLayeredWindow(m_hwnd, screenDC, &ptDst, &sizeWnd, m_memDC, &ptSrc, 0, &blend, ULW_ALPHA);
        ReleaseDC(nullptr, screenDC);
    }

    void renderWindowControls(Graphics& g) {
        // Minimize Button
        RectF minRect(
            (float)m_minBtnRect.left, (float)m_minBtnRect.top,
            (float)(m_minBtnRect.right - m_minBtnRect.left),
            (float)(m_minBtnRect.bottom - m_minBtnRect.top)
        );
        if (m_hoverMin) {
            GraphicsPath minBgPath;
            addRoundedRectPath(minBgPath, minRect, 6.0f);
            SolidBrush minBgBrush(Color(45, 255, 255, 255));
            g.FillPath(&minBgBrush, &minBgPath);
        }
        Pen minPen(m_hoverMin ? Color(250, 255, 255, 255) : Color(180, 200, 205, 215), 1.6f);
        float minCenterY = minRect.Y + minRect.Height / 2.0f;
        g.DrawLine(&minPen, minRect.X + 8.0f, minCenterY, minRect.X + minRect.Width - 8.0f, minCenterY);

        // Close Button
        RectF closeRect(
            (float)m_closeBtnRect.left, (float)m_closeBtnRect.top,
            (float)(m_closeBtnRect.right - m_closeBtnRect.left),
            (float)(m_closeBtnRect.bottom - m_closeBtnRect.top)
        );
        if (m_hoverClose) {
            GraphicsPath closeBgPath;
            addRoundedRectPath(closeBgPath, closeRect, 6.0f);
            SolidBrush closeBgBrush(Color(210, 230, 55, 65)); // Red hover
            g.FillPath(&closeBgBrush, &closeBgPath);
        }
        Pen closePen(m_hoverClose ? Color(255, 255, 255, 255) : Color(180, 200, 205, 215), 1.6f);
        float offset = 8.5f;
        g.DrawLine(&closePen, closeRect.X + offset, closeRect.Y + offset, closeRect.X + closeRect.Width - offset, closeRect.Y + closeRect.Height - offset);
        g.DrawLine(&closePen, closeRect.X + closeRect.Width - offset, closeRect.Y + offset, closeRect.X + offset, closeRect.Y + closeRect.Height - offset);
    }

    void renderSidebarItems(Graphics& g) {
        for (size_t i = 0; i < m_items.size(); ++i) {
            const auto& item = m_items[i];
            bool isSelected = ((int)i == m_selectedIndex);
            bool isHovered = ((int)i == m_hoveredIndex);

            RectF btnRect(
                (float)item.rect.left, (float)item.rect.top,
                (float)(item.rect.right - item.rect.left),
                (float)(item.rect.bottom - item.rect.top)
            );

            // Item Button Background
            if (isSelected) {
                GraphicsPath btnPath;
                addRoundedRectPath(btnPath, btnRect, 10.0f);
                SolidBrush activeBg(Color(55, 255, 255, 255));
                g.FillPath(&activeBg, &btnPath);

                Pen activeBorder(Color(80, 255, 255, 255), 1.0f);
                g.DrawPath(&activeBorder, &btnPath);

                // Sleek active vertical indicator pill on the far left
                GraphicsPath indicatorPath;
                RectF indicatorRect(5.0f, btnRect.Y + 11.0f, 3.5f, btnRect.Height - 22.0f);
                addRoundedRectPath(indicatorPath, indicatorRect, 1.5f);
                SolidBrush indicatorBrush(Color(250, 255, 255, 255));
                g.FillPath(&indicatorBrush, &indicatorPath);
            } else if (isHovered) {
                GraphicsPath btnPath;
                addRoundedRectPath(btnPath, btnRect, 10.0f);
                SolidBrush hoverBg(Color(32, 255, 255, 255));
                g.FillPath(&hoverBg, &btnPath);
            }

            // Draw Icon centered
            if (item.icon && item.icon->GetLastStatus() == Ok) {
                int iconW = 26;
                int iconH = 26;
                float iconX = btnRect.X + (btnRect.Width - iconW) / 2.0f;
                float iconY = btnRect.Y + (btnRect.Height - iconH) / 2.0f;

                // Subtle icon opacity when inactive and not hovered
                if (!isSelected && !isHovered) {
                    ColorMatrix matrix = {
                        1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                        0.0f, 1.0f, 0.0f, 0.0f, 0.0f,
                        0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
                        0.0f, 0.0f, 0.0f, 0.80f, 0.0f,
                        0.0f, 0.0f, 0.0f, 0.0f, 1.0f
                    };
                    ImageAttributes attr;
                    attr.SetColorMatrix(&matrix, ColorMatrixFlagsDefault, ColorAdjustTypeBitmap);
                    g.DrawImage(item.icon.get(), RectF(iconX, iconY, (float)iconW, (float)iconH),
                                0.0f, 0.0f, (float)item.icon->GetWidth(), (float)item.icon->GetHeight(),
                                UnitPixel, &attr);
                } else {
                    g.DrawImage(item.icon.get(), RectF(iconX, iconY, (float)iconW, (float)iconH),
                                0.0f, 0.0f, (float)item.icon->GetWidth(), (float)item.icon->GetHeight(),
                                UnitPixel);
                }
            }
        }
    }

    void renderMainContent(Graphics& g) {
        // Index 1 corresponds to "overview.png" (Overview)
        if (m_selectedIndex == 1) {
            FontFamily contentFontFamily(L"Segoe UI");
            Gdiplus::Font sessionFont(&contentFontFamily, 14.5f, FontStyleRegular, UnitPoint);

            StringFormat format;
            format.SetAlignment(StringAlignmentCenter);
            format.SetLineAlignment(StringAlignmentCenter);

            float contentX = (float)SIDEBAR_WIDTH;
            float contentY = (float)HEADER_HEIGHT;
            float contentW = (float)(WINDOW_WIDTH - SIDEBAR_WIDTH);
            float contentH = (float)(WINDOW_HEIGHT - HEADER_HEIGHT);

            // Subtle status pill backdrop in the center
            float pillW = 240.0f;
            float pillH = 44.0f;
            float pillX = contentX + (contentW - pillW) / 2.0f;
            float pillY = contentY + (contentH - pillH) / 2.0f;

            GraphicsPath pillPath;
            addRoundedRectPath(pillPath, RectF(pillX, pillY, pillW, pillH), 12.0f);
            SolidBrush pillBrush(Color(25, 255, 255, 255));
            g.FillPath(&pillBrush, &pillPath);
            Pen pillPen(Color(40, 255, 255, 255), 1.0f);
            g.DrawPath(&pillPen, &pillPath);

            // Text "keine aktiven sessions"
            RectF sessRect(pillX, pillY, pillW, pillH);
            RectF shadowRect(pillX, pillY + 1.0f, pillW, pillH);
            SolidBrush shadowBrush(Color(100, 0, 0, 0));
            g.DrawString(L"keine aktiven sessions", -1, &sessionFont, shadowRect, &format, &shadowBrush);

            SolidBrush sessionTextBrush(Color(215, 185, 195, 210));
            g.DrawString(L"keine aktiven sessions", -1, &sessionFont, sessRect, &format, &sessionTextBrush);
        }
    }

    bool isOverClickable(POINT pt) {
        if (PtInRect(&m_closeBtnRect, pt) || PtInRect(&m_minBtnRect, pt)) return true;
        for (const auto& item : m_items) {
            if (PtInRect(&item.rect, pt)) return true;
        }
        return false;
    }

    void handleMouseMove(int x, int y) {
        if (!m_isMouseTracking) {
            TRACKMOUSEEVENT tme = { sizeof(TRACKMOUSEEVENT) };
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = m_hwnd;
            TrackMouseEvent(&tme);
            m_isMouseTracking = true;
        }

        POINT pt = { x, y };
        int newHover = -1;
        for (size_t i = 0; i < m_items.size(); ++i) {
            if (PtInRect(&m_items[i].rect, pt)) {
                newHover = (int)i;
                break;
            }
        }

        bool newHoverClose = PtInRect(&m_closeBtnRect, pt);
        bool newHoverMin = PtInRect(&m_minBtnRect, pt);

        if (newHover != m_hoveredIndex || newHoverClose != m_hoverClose || newHoverMin != m_hoverMin) {
            m_hoveredIndex = newHover;
            m_hoverClose = newHoverClose;
            m_hoverMin = newHoverMin;
            render();
        }

        if (isOverClickable(pt)) {
            SetCursor(LoadCursor(nullptr, IDC_HAND));
        } else {
            SetCursor(LoadCursor(nullptr, IDC_ARROW));
        }
    }

    void handleMouseLeave() {
        m_isMouseTracking = false;
        if (m_hoveredIndex != -1 || m_hoverClose || m_hoverMin) {
            m_hoveredIndex = -1;
            m_hoverClose = false;
            m_hoverMin = false;
            render();
        }
    }

    void handleLButtonDown(int x, int y) {
        POINT pt = { x, y };

        if (PtInRect(&m_closeBtnRect, pt)) {
            PostQuitMessage(0);
            return;
        }

        if (PtInRect(&m_minBtnRect, pt)) {
            ShowWindow(m_hwnd, SW_MINIMIZE);
            return;
        }

        for (size_t i = 0; i < m_items.size(); ++i) {
            if (PtInRect(&m_items[i].rect, pt)) {
                m_selectedIndex = (int)i;
                render();
                return;
            }
        }

        // Click-and-drag from anywhere else to move the window seamlessly
        ReleaseCapture();
        SendMessage(m_hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
    }

    static LRESULT CALLBACK StaticWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
        VratMenuApp* app = nullptr;

        if (uMsg == WM_CREATE) {
            CREATESTRUCTW* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
            app = reinterpret_cast<VratMenuApp*>(cs->lpCreateParams);
            SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
        } else {
            app = reinterpret_cast<VratMenuApp*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
        }

        if (app) {
            return app->WndProc(hwnd, uMsg, wParam, lParam);
        }
        return DefWindowProcW(hwnd, uMsg, wParam, lParam);
    }

    LRESULT WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
        switch (uMsg) {
        case WM_SETCURSOR: {
            POINT pt;
            GetCursorPos(&pt);
            ScreenToClient(hwnd, &pt);
            if (isOverClickable(pt)) {
                SetCursor(LoadCursor(nullptr, IDC_HAND));
                return TRUE;
            }
            SetCursor(LoadCursor(nullptr, IDC_ARROW));
            return TRUE;
        }
        case WM_MOUSEMOVE: {
            int x = (short)LOWORD(lParam);
            int y = (short)HIWORD(lParam);
            handleMouseMove(x, y);
            return 0;
        }
        case WM_MOUSELEAVE:
            handleMouseLeave();
            return 0;

        case WM_LBUTTONDOWN: {
            int x = (short)LOWORD(lParam);
            int y = (short)HIWORD(lParam);
            handleLButtonDown(x, y);
            return 0;
        }

        case WM_NCHITTEST:
            return HTCLIENT;

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        }
        return DefWindowProcW(hwnd, uMsg, wParam, lParam);
    }
};

int main() {
    // 1. First show terminal window with rat ASCII in dark blue to white horizontal gradient for 2 seconds
    ShowStartupTerminal();

    // 2. Start Gdiplus and transparent rounded GUI menu
    GdiplusStartupInput gdiplusStartupInput;
    ULONG_PTR gdiplusToken;
    Status status = GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, nullptr);
    if (status != Ok) return -1;

    VratMenuApp app(GetModuleHandle(nullptr));
    if (app.initialize()) {
        app.run();
    }

    GdiplusShutdown(gdiplusToken);
    return 0;
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int) {
    return main();
}
