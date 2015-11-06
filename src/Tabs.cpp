/* Copyright 2015 the SumatraPDF project authors (see AUTHORS file).
   License: GPLv3 */

// utils
#include "BaseUtil.h"
#include "WinDynCalls.h"
#include "Dpi.h"
#include "FileUtil.h"
#include "GdiPlusUtil.h"
#include "UITask.h"
#include "WinUtil.h"
// rendering engines
#include "BaseEngine.h"
#include "EngineManager.h"
// layout controllers
#include "SettingsStructs.h"
#include "Controller.h"
#include "GlobalPrefs.h"
// ui
#include "SumatraPDF.h"
#include "WindowInfo.h"
#include "TabInfo.h"
#include "resource.h"
#include "Caption.h"
#include "Menu.h"
#include "TableOfContents.h"
#include "Tabs.h"

#include "DebugLog.h"

/*
Notes on properly implementing drag&drop: http://www.mvps.org/user32/setcapture.html

The logic of tab drag&drop is similar to Chrome:
- we can re-arrange tabs within the same tab strip
- we can drag out a tab to create its own window
- we can move tab from one window to another

Our visual behavior during dragging is closer to IE Edge than Chrome,
due to implementation simplicity. Mostly, we don't try to preview the
final state at all costs. When dragged out we only show the tab, not
the content.
*/

#define DEFAULT_CURRENT_BG_COL (COLORREF)-1

#define TAB_COLOR_BG      COLOR_BTNFACE
#define TAB_COLOR_TEXT    COLOR_BTNTEXT

#define TABBAR_HEIGHT    24
#define MIN_TAB_WIDTH   100

static bool g_FirefoxStyle = false;

int GetTabbarHeight(HWND hwnd, float factor)
{
    int dy = DpiScaleY(hwnd, TABBAR_HEIGHT);
    return (int)(dy * factor);
}

static inline SizeI GetTabSize(HWND hwnd)
{
    int dx = DpiScaleX(hwnd, std::max(gGlobalPrefs->prereleaseSettings.tabWidth, MIN_TAB_WIDTH));
    int dy = DpiScaleY(hwnd, TABBAR_HEIGHT);
    return SizeI(dx, dy);
}

static inline Color ToColor(COLORREF c)
{
    return Color(GetRValueSafe(c), GetGValueSafe(c), GetBValueSafe(c));
}

class TabsControl
{
    WStrVec tabTitles;
    PathData *data;
    int width, height;
public:
    HWND hwnd;
    int current, highlighted;
    int nextTab;
    bool inTitlebar;
    COLORREF currBgCol;
    struct {
        COLORREF bg, highlight, current, outline, bar, text, closeHighlight, closeClick, closeLine;
    } colors;

    TabsControl(HWND wnd, SizeI tabSize) :
        hwnd(wnd), data(nullptr), width(0), height(0),
        current(-1), highlighted(-1), nextTab(-1),
        inTitlebar(false), currBgCol(DEFAULT_CURRENT_BG_COL) {
        ZeroMemory(&colors, sizeof(colors));
        Reshape(tabSize.dx, tabSize.dy);
        EvaluateColors(false);
    }

    ~TabsControl() {
        delete data;
        DeleteAll();
    }

    bool Reshape(int dx, int dy);
    int IndexFromPoint(int x, int y, bool *overClose = nullptr);
    void Invalidate(int tabIdx);
    void Paint(HDC hdc, RECT &rc);
    void EvaluateColors(bool force);

    int Count() {
        return (int) tabTitles.Count();
    }

    void Insert(int idx, const WCHAR *t) {
        tabTitles.InsertAt(idx, str::Dup(t));
    }

    bool Set(int idx, const WCHAR *t) {
        if (idx < Count()) {
            str::ReplacePtr(&tabTitles.At(idx), t);
            return true;
        }
        return false;
    }

    bool Delete(int idx) {
        if (idx < Count()) {
            free(tabTitles.PopAt(idx));
            return true;
        }
        return false;
    }

    void DeleteAll() {
        tabTitles.Reset();
    }

    void InvalidateAll() {
        InvalidateRect(hwnd, nullptr, FALSE);
    }
};

// Calculates tab's elements, based on its width and height.
// Generates a GraphicsPath, which is used for painting the tab, etc.
bool TabsControl::Reshape(int dx, int dy) {
    dx--;
    if (width == dx && height == dy)
        return false;
    width = dx; height = dy;

    GraphicsPath shape;
    // define tab's body
    int c = int((float) height * 0.6f + 0.5f); // size of bounding square for the arc
    shape.AddArc(0, 0, c, c, 180.0f, 90.0f);
    shape.AddArc(width - c, 0, c, c, 270.0f, 90.0f);
    shape.AddLine(width, height, 0, height);
    shape.CloseFigure();
    shape.SetMarker();

    // define "x"'s circle
    c = int((float) height * 0.78f + 0.5f); // size of bounding square for the circle
    int maxC = DpiScaleX(hwnd, 17);
    if (height > maxC) {
        c = DpiScaleX(hwnd, 17);
    }
    Point p(width - c - DpiScaleX(hwnd, 3), (height - c) / 2); // circle's position
    shape.AddEllipse(p.X, p.Y, c, c);
    shape.SetMarker();
    // define "x"
    int o = int((float) c * 0.286f + 0.5f); // "x"'s offset
    shape.AddLine(p.X + o, p.Y + o, p.X + c - o, p.Y + c - o);
    shape.StartFigure();
    shape.AddLine(p.X + c - o, p.Y + o, p.X + o, p.Y + c - o);
    shape.SetMarker();

    delete data;
    data = new PathData();
    shape.GetPathData(data);
    return true;
}

// Finds the index of the tab, which contains the given point.
int TabsControl::IndexFromPoint(int x, int y, bool *overClose) {
    Point point(x, y);
    Graphics gfx(hwnd);
    GraphicsPath shapes(data->Points, data->Types, data->Count);
    GraphicsPath shape;
    GraphicsPathIterator iter(&shapes);
    iter.NextMarker(&shape);

    ClientRect rClient(hwnd);
    REAL yPosTab = inTitlebar ? 0.0f : REAL(rClient.dy - height - 1);
    gfx.TranslateTransform(1.0f, yPosTab);
    for (int i = 0; i < Count(); i++) {
        Point pt(point);
        gfx.TransformPoints(CoordinateSpaceWorld, CoordinateSpaceDevice, &pt, 1);
        if (shape.IsVisible(pt, &gfx)) {
            iter.NextMarker(&shape);
            if (overClose)
                *overClose = shape.IsVisible(pt, &gfx) ? true : false;
            return i;
        }
        gfx.TranslateTransform(REAL(width + 1), 0.0f);
    }
    if (overClose)
        *overClose = false;
    return -1;
}

// Invalidates the tab's region in the client area.
void TabsControl::Invalidate(int tabIdx) {
    if (tabIdx < 0) return;

    Graphics gfx(hwnd);
    GraphicsPath shapes(data->Points, data->Types, data->Count);
    GraphicsPath shape;
    GraphicsPathIterator iter(&shapes);
    iter.NextMarker(&shape);
    Region region(&shape);

    ClientRect rClient(hwnd);
    REAL yPosTab = inTitlebar ? 0.0f : REAL(rClient.dy - height - 1);
    gfx.TranslateTransform(REAL((width + 1) * tabIdx) + 1.0f, yPosTab);
    HRGN hRgn = region.GetHRGN(&gfx);
    InvalidateRgn(hwnd, hRgn, FALSE);
    DeleteObject(hRgn);
}

// Paints the tabs that intersect the window's update rectangle.
void TabsControl::Paint(HDC hdc, RECT &rc) {
    int hoverTabIdx = -1; // tab over which the cursor is
    bool mouseOverClose = false;
    PointI p;
    if (GetCursorPosInHwnd(hwnd, p)) {
        hoverTabIdx = IndexFromPoint(p.x, p.y, &mouseOverClose);
    }

    IntersectClipRect(hdc, rc.left, rc.top, rc.right, rc.bottom);

    // paint the background
    bool isTranslucentMode = inTitlebar && dwm::IsCompositionEnabled();
    if (isTranslucentMode)
        PaintParentBackground(hwnd, hdc);
    else {
        HBRUSH brush = CreateSolidBrush(colors.bar);
        FillRect(hdc, &rc, brush);
        DeleteObject(brush);
    }

    // TODO: GDI+ doesn't seem to cope well with SetWorldTransform
    XFORM ctm = { 1.0, 0, 0, 1.0, 0, 0 };
    SetWorldTransform(hdc, &ctm);

    Graphics gfx(hdc);
    gfx.SetCompositingMode(CompositingModeSourceCopy);
    gfx.SetCompositingQuality(CompositingQualityHighQuality);
    gfx.SetSmoothingMode(SmoothingModeHighQuality);
    gfx.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
    gfx.SetPageUnit(UnitPixel);
    GraphicsPath shapes(data->Points, data->Types, data->Count);
    GraphicsPath shape;
    GraphicsPathIterator iter(&shapes);

    SolidBrush br(Color(0, 0, 0));
    Pen pen(&br, 2.0f);

    Font f(hdc, GetDefaultGuiFont());
    // TODO: adjust these constant values for DPI?
    RectF layout((REAL) DpiScaleX(hwnd, 3), 1.0f, REAL(width - DpiScaleX(hwnd, 20)), (REAL) height);
    StringFormat sf(StringFormat::GenericDefault());
    sf.SetFormatFlags(StringFormatFlagsNoWrap);
    sf.SetLineAlignment(StringAlignmentCenter);
    sf.SetTrimming(StringTrimmingEllipsisCharacter);

    REAL yPosTab = inTitlebar ? 0.0f : REAL(ClientRect(hwnd).dy - height - 1);
    const WCHAR *tabText;
    for (int i = 0; i < Count(); i++) {
        tabText = tabTitles.At(i);
        gfx.ResetTransform();
        gfx.TranslateTransform(1.f + (REAL) (width + 1) * i - (REAL) rc.left, yPosTab - (REAL) rc.top);

        if (!gfx.IsVisible(0, 0, width + 1, height + 1))
            continue;

        // in firefox style we only paint current and highlighed tabs
        // all other tabs only show
        bool onlyText = g_FirefoxStyle && !((current == i) || (highlighted == i));
        if (onlyText) {
#if 0
            // we need to first paint the background with the same color as caption,
            // otherwise the text looks funny (because is transparent?)
            // TODO: what is the damn bg color of caption? bar is too light, outline is too dark
            Color bgColTmp;
            bgColTmp.SetFromCOLORREF(colors.bar);
            {
                SolidBrush bgBr(bgColTmp);
                gfx.FillRectangle(&bgBr, layout);
            }
            bgColTmp.SetFromCOLORREF(colors.outline);
            {
                SolidBrush bgBr(bgColTmp);
                gfx.FillRectangle(&bgBr, layout);
            }
#endif
            // TODO: this is a hack. If I use no background and cleartype, the
            // text looks funny (is bold).
            // CompositingModeSourceCopy doesn't work with clear type
            // another option is to draw background before drawing text, but
            // I can't figure out what is the actual color of caption
            gfx.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
            gfx.SetCompositingMode(CompositingModeSourceCopy);
            //gfx.SetCompositingMode(CompositingModeSourceOver);
            br.SetColor(ToColor(colors.text));
            gfx.DrawString(tabText, -1, &f, layout, &sf, &br);
            gfx.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
            continue;
        }

        COLORREF bgCol = colors.bg;;
        if (current == i) {
            bgCol = colors.current;
        } else if (highlighted == i) {
            bgCol = colors.highlight;
        }

        // ensure contrast between text and background color
        // TODO: adjust threshold (and try adjusting both current/background tabs)
        COLORREF textCol = colors.text;
        float bgLight = GetLightness(bgCol), textLight = GetLightness(textCol);
        if (textLight < bgLight ? bgLight < 0x70 : bgLight > 0x90)
            textCol = textLight ? AdjustLightness(textCol, 255.0f / textLight - 1.0f) : RGB(255, 255, 255);
        if (fabs(textLight - bgLight) < 0x40)
            textCol = bgLight < 0x80 ? RGB(255, 255, 255) : RGB(0, 0, 0);

        // paint tab's body
        gfx.SetCompositingMode(CompositingModeSourceCopy);
        iter.NextMarker(&shape);
        br.SetColor(ToColor(bgCol));
        gfx.FillPath(&br, &shape);

        // draw tab's text
        gfx.SetCompositingMode(CompositingModeSourceOver);
        br.SetColor(ToColor(textCol));
        gfx.DrawString(tabText, -1, &f, layout, &sf, &br);

        // paint "x"'s circle
        iter.NextMarker(&shape);
        if (hoverTabIdx == i && mouseOverClose) {
            // TODO: change color depending on wether left mouse button is down
            br.SetColor(ToColor(mouseOverClose ? colors.closeHighlight : colors.closeClick));
            gfx.FillPath(&br, &shape);
        }

        // paint "x"
        iter.NextMarker(&shape);
        if (hoverTabIdx == i && mouseOverClose) {
            pen.SetColor(ToColor(colors.closeLine));
        } else {
            pen.SetColor(ToColor(colors.outline));
        }
        gfx.DrawPath(&pen, &shape);
        iter.Rewind();
    }
}

// Evaluates the colors for the tab's elements.
void TabsControl::EvaluateColors(bool force) {
    COLORREF bg, txt;
    if (inTitlebar) {
        WindowInfo *win = FindWindowInfoByHwnd(hwnd);
        bg = win->caption->bgColor;
        txt = win->caption->textColor;
    } else {
        bg = GetSysColor(TAB_COLOR_BG);
        txt = GetSysColor(TAB_COLOR_TEXT);
    }
    if (!force && bg == colors.bar && txt == colors.text)
        return;

    colors.bar = bg;
    colors.text = txt;

    int sign = GetLightness(colors.text) > GetLightness(colors.bar) ? -1 : 1;

    colors.current = AdjustLightness2(colors.bar, sign * 25.0f);
    colors.highlight = AdjustLightness2(colors.bar, sign * 15.0f);
    colors.bg = AdjustLightness2(colors.bar, -sign * 15.0f);
    colors.outline = AdjustLightness2(colors.bar, -sign * 60.0f);
    colors.closeLine = COL_CLOSE_X_HOVER;
    colors.closeHighlight = COL_CLOSE_HOVER_BG;
    colors.closeClick = AdjustLightness2(colors.closeHighlight, -10.0f);
    if (currBgCol != DEFAULT_CURRENT_BG_COL) {
        colors.current = currBgCol;
    }
}

static void RemoveTab(WindowInfo *win, int idx) {
    TabInfo *tab = win->tabs.At(idx);
    UpdateTabFileDisplayStateForWin(win, tab);
    win->tabSelectionHistory->Remove(tab);
    win->tabs.Remove(tab);
    if (tab == win->currentTab) {
        win->ctrl = nullptr;
        win->currentTab = nullptr;
    }
    delete tab;
    TabCtrl_DeleteItem(win->hwndTabBar, idx);
    UpdateTabWidth(win);
}

static void CloseOrRemoveTab(WindowInfo *win, int tabIdx) {
    int currIdx = TabCtrl_GetCurSel(win->hwndTabBar);
    if (tabIdx == currIdx)
        CloseTab(win);
    else
        RemoveTab(win, tabIdx);
}

static void NotifyTab(WindowInfo *win, UINT code) {
    if (!WindowInfoStillValid(win)) {
        return;
    }
    NMHDR nmhdr = { nullptr, 0, code };
    if (TabsOnNotify(win, (LPARAM)&nmhdr)) {
        return;
    }
    TabsControl *tabs = (TabsControl *)GetWindowLongPtr(win->hwndTabBar, GWLP_USERDATA);
    if (TCN_SELCHANGING == code) {
        // if we have permission to select the tab
        tabs->Invalidate(tabs->current);
        tabs->Invalidate(tabs->nextTab);
        tabs->current = tabs->nextTab;
        // send notification that the tab is selected
        nmhdr.code = TCN_SELCHANGE;
        TabsOnNotify(win, (LPARAM)&nmhdr);
    }
}

static void SetTabTitle(WindowInfo *win, TabInfo *tabs) {
    TCITEM tcs;
    tcs.mask = TCIF_TEXT;
    tcs.pszText = (WCHAR *) tabs->GetTabTitle();
    TabCtrl_SetItem(win->hwndTabBar, win->tabs.Find(tabs), &tcs);
}

static void SwapTabs(WindowInfo *win, int tab1, int tab2) {
    if (tab1 == tab2 || tab1 < 0 || tab2 < 0)
        return;

    std::swap(win->tabs.At(tab1), win->tabs.At(tab2));
    SetTabTitle(win, win->tabs.At(tab1));
    SetTabTitle(win, win->tabs.At(tab2));

    int current = TabCtrl_GetCurSel(win->hwndTabBar);
    if (tab1 == current)
        TabCtrl_SetCurSel(win->hwndTabBar, tab2);
    else if (tab2 == current)
        TabCtrl_SetCurSel(win->hwndTabBar, tab1);
}

static bool IsDragging(HWND hwnd) {
    return hwnd == GetCapture();
}

static void OnWmPaint(TabsControl *tabs) {
    PAINTSTRUCT ps;
    RECT rc;

    HWND hwnd = tabs->hwnd;
    GetUpdateRect(hwnd, &rc, FALSE);
    HDC hdc = BeginPaint(hwnd, &ps);
    DoubleBuffer buffer(hwnd, RectI::FromRECT(rc));
    tabs->EvaluateColors(false);
    tabs->Paint(buffer.GetDC(), rc);
    buffer.Flush(hdc);
    ValidateRect(hwnd, nullptr);
    EndPaint(hwnd, &ps);
}

static void OnLButtonDown(TabsControl *tabs, int x, int y) {
    HWND hwnd = tabs->hwnd;
    bool overClose;
    tabs->nextTab = tabs->IndexFromPoint(x, y, &overClose);
    if (tabs->nextTab == -1) {
        return ;
    }
    if (overClose) {
        tabs->Invalidate(tabs->nextTab);
        return;
    }

    if (tabs->nextTab != tabs->current) {
        WindowInfo *win = FindWindowInfoByHwnd(hwnd);
        NotifyTab(win, TCN_SELCHANGING);
    }
    SetCapture(hwnd);
}

static void OnLButtonUp(TabsControl* tabs, int x, int y) {
    HWND hwnd = tabs->hwnd;

    bool overClose;
    int tabIdx = tabs->IndexFromPoint(x, y, &overClose);

    bool isDragging = IsDragging(hwnd);
    if (isDragging) {
        ReleaseCapture();
    }
    plogf("WM_LBUTTONUP %d %d isDrag=%d, overClose=%d", x, y, (int) isDragging, (int)overClose);
    if (!overClose) {
        return;
    }

    WindowInfo *win = FindWindowInfoByHwnd(hwnd);
    CloseOrRemoveTab(win, tabIdx);
    //tabs->Invalidate(tabIdx);
}

static void OnMouseMove(TabsControl *tabs, int x, int y) {
    HWND hwnd = tabs->hwnd;
    bool isDragging = IsDragging(tabs->hwnd);
    if (!isDragging) {
        tabs->InvalidateAll();
        return;
    }

#if 0
    if (!tabs->isMouseInClientArea) {
        // Track the mouse for leaving the client area.
        TRACKMOUSEEVENT tme = { 0 };
        tme.cbSize = sizeof(TRACKMOUSEEVENT);
        tme.dwFlags = TME_LEAVE;
        tme.hwndTrack = tabs->hwnd;
        if (TrackMouseEvent(&tme))
            tabs->isMouseInClientArea = true;
    }
#endif


    bool overClose = false;
    int hl = tabs->IndexFromPoint(x, y, &overClose);
    if (isDragging && hl == -1) {
        // preserve the highlighted tab if it's dragged outside the tabs' area
        hl = tabs->highlighted;
    }
    if (tabs->highlighted != hl && false) {
        if (isDragging) {
            // send notification if the highlighted tab is dragged over another
            WindowInfo *win = FindWindowInfoByHwnd(hwnd);
            int tabNo = tabs->highlighted;
            SwapTabs(win, tabNo, hl);
            //uitask::Post([=] { SwapTabs(win, tabNo, hl); });
            plogf("swapping tabs tabNo=%d, hl=%d", (int) tabNo, (int) hl);
        }

        tabs->Invalidate(hl);
        tabs->Invalidate(tabs->highlighted);
        tabs->highlighted = hl;
    }

#if 0
    int xHl = overClose && !isDragging ? hl : -1;
    if (tabs->xHighlighted != xHl) {
        tabs->Invalidate(xHl);
        tabs->Invalidate(tabs->xHighlighted);
        tabs->xHighlighted = xHl;
    }
    if (!overClose)
        tabs->xClicked = -1;
#endif
    plogf("WM_MOUSEMOVE %d %d, isDrag=%d, inX=%d", x, y, (int) isDragging, (int) overClose);
}

static WNDPROC DefWndProcTabBar = nullptr;
static LRESULT CALLBACK WndProcTabBar(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    LPTCITEM tcs;

    int tabIdx = (int)wParam;
    TabsControl *tabs = (TabsControl *)GetWindowLongPtr(hwnd, GWLP_USERDATA);

    switch (msg) {
    case WM_DESTROY:
        delete tabs;
        SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)0);
        break;

    case TCM_INSERTITEM:
        tcs = (LPTCITEM)lParam;
        CrashIf(!(TCIF_TEXT & tcs->mask));
        tabs->Insert(tabIdx, tcs->pszText);
        if (tabIdx <= tabs->current)
            tabs->current++;
        InvalidateRgn(hwnd, nullptr, FALSE);
        UpdateWindow(hwnd);
        break;

    case TCM_SETITEM:
        tcs = (LPTCITEM)lParam;
        if (TCIF_TEXT & tcs->mask) {
            if (tabs->Set(tabIdx, tcs->pszText))
                tabs->Invalidate(tabIdx);
        }
        break;

    case TCM_DELETEITEM:
        if (tabs->Delete(tabIdx)) {
            if (tabIdx < tabs->current)
                tabs->current--;
            else if (tabIdx == tabs->current)
                tabs->current = -1;
            if (tabs->Count()) {
                InvalidateRect(hwnd, nullptr, FALSE);
                UpdateWindow(hwnd);
            }
        }
        break;

    case TCM_DELETEALLITEMS:
        tabs->DeleteAll();
        tabs->current = tabs->highlighted = -1;
        break;

    case TCM_SETITEMSIZE:
        if (tabs->Reshape(LOWORD(lParam), HIWORD(lParam))) {
            if (tabs->Count()) {
                tabs->InvalidateAll();
                UpdateWindow(hwnd);
            }
        }
        break;

    case TCM_GETCURSEL:
        return tabs->current;

    case TCM_SETCURSEL:
        {
            if (tabIdx >= tabs->Count()) {
                return -1;
            }
            int previous = tabs->current;
            if (tabIdx != tabs->current) {
                tabs->Invalidate(tabs->current);
                tabs->Invalidate(tabIdx);
                tabs->current = tabIdx;
                UpdateWindow(hwnd);
            }
            return previous;
        }

    case WM_NCHITTEST:
        {
            if (!tabs->inTitlebar || hwnd == GetCapture())
                return HTCLIENT;
            POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            ScreenToClient(hwnd, &pt);
            if (-1 != tabs->IndexFromPoint(pt.x, pt.y))
                return HTCLIENT;
        }
        return HTTRANSPARENT;

    case WM_MOUSELEAVE:
        PostMessage(hwnd, WM_MOUSEMOVE, 0xFF, 0);
        return 0;

    case WM_MOUSEMOVE:
        OnMouseMove(tabs, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        return 0;

    case WM_LBUTTONDOWN:
        OnLButtonDown(tabs, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        return 0;

    case WM_LBUTTONUP:
        OnLButtonUp(tabs, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        return 0;


#if 0 // Note: I don't even know how to trigger this with my mouse-----------------
    case WM_MBUTTONDOWN:
        // middle-clicking unconditionally closes the tab
        {
            tabs->nextTab = tabs->IndexFromPoint(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            // send request to close the tab
            WindowInfo *win = FindWindowInfoByHwnd(hwnd);
            int next = tabs->nextTab;
            NotifyTabWillClose(tabs);
        }
        return 0;

    case WM_MBUTTONUP:
        if (tabs->xClicked != -1) {
            WindowInfo *win = FindWindowInfoByHwnd(hwnd);
            int clicked = tabs->xClicked;
            uitask::Post([=] { CloseOrRemoveTab(win, clicked); });
            tabs->Invalidate(clicked);
            tabs->xClicked = -1;
        }
        return 0;
#endif

    case WM_ERASEBKGND:
        return TRUE;

    case WM_PAINT:
        OnWmPaint(tabs);
        return 0;

    case WM_SIZE:
        {
            WindowInfo *win = FindWindowInfoByHwnd(hwnd);
            if (win)
                UpdateTabWidth(win);
        }
        break;
    }

    return CallWindowProc(DefWndProcTabBar, hwnd, msg, wParam, lParam);
}

void CreateTabbar(WindowInfo *win)
{
    HWND hwndTabBar = CreateWindow(WC_TABCONTROL, L"",
        WS_CHILD | WS_CLIPSIBLINGS /*| WS_VISIBLE*/ |
        TCS_FOCUSNEVER | TCS_FIXEDWIDTH | TCS_FORCELABELLEFT,
        0, 0, 0, 0,
        win->hwndFrame, (HMENU)IDC_TABBAR, GetModuleHandle(nullptr), nullptr);

    if (!DefWndProcTabBar)
        DefWndProcTabBar = (WNDPROC)GetWindowLongPtr(hwndTabBar, GWLP_WNDPROC);
    SetWindowLongPtr(hwndTabBar, GWLP_WNDPROC, (LONG_PTR)WndProcTabBar);

    SizeI tabSize = GetTabSize(win->hwndFrame);
    TabsControl *tabs = new TabsControl(hwndTabBar, tabSize);
    SetWindowLongPtr(hwndTabBar, GWLP_USERDATA, (LONG_PTR)tabs);

    SetWindowFont(hwndTabBar, GetDefaultGuiFont(), FALSE);
    TabCtrl_SetItemSize(hwndTabBar, tabSize.dx, tabSize.dy);

    win->hwndTabBar = hwndTabBar;

    win->tabSelectionHistory = new Vec<TabInfo *>();
}

// verifies that TabInfo state is consistent with WindowInfo state
static NO_INLINE void VerifyTabInfo(WindowInfo *win, TabInfo *tdata) {
    CrashIf(tdata->ctrl != win->ctrl);
    ScopedMem<WCHAR> winTitle(win::GetText(win->hwndFrame));
    CrashIf(!str::Eq(winTitle.Get(), tdata->frameTitle));
    bool expectedTocVisibility = tdata->showToc; // if not in presentation mode
    if (PM_DISABLED != win->presentation) {
        expectedTocVisibility = false; // PM_BLACK_SCREEN, PM_WHITE_SCREEN
        if (PM_ENABLED == win->presentation) {
            expectedTocVisibility = tdata->showTocPresentation;
        }
    }
    CrashIf(win->tocVisible != expectedTocVisibility);
    CrashIf(tdata->canvasRc != win->canvasRc);
}

// Must be called when the active tab is losing selection.
// This happens when a new document is loaded or when another tab is selected.
void SaveCurrentTabInfo(WindowInfo *win)
{
    if (!win)
        return;

    int current = TabCtrl_GetCurSel(win->hwndTabBar);
    if (-1 == current)
        return;
    CrashIf(win->currentTab != win->tabs.At(current));

    TabInfo *tdata = win->currentTab;
    CrashIf(!tdata);
    if (win->tocLoaded) {
        tdata->tocState.Reset();
        HTREEITEM hRoot = TreeView_GetRoot(win->hwndTocTree);
        if (hRoot)
            UpdateTocExpansionState(tdata, win->hwndTocTree, hRoot);
    }
    VerifyTabInfo(win, tdata);

    // update the selection history
    win->tabSelectionHistory->Remove(tdata);
    win->tabSelectionHistory->Push(tdata);
}

void UpdateCurrentTabBgColor(WindowInfo *win)
{
    TabsControl *tabs = (TabsControl *)GetWindowLongPtr(win->hwndTabBar, GWLP_USERDATA);
    if (win->AsEbook()) {
        COLORREF txtCol;
        GetEbookUiColors(txtCol, tabs->currBgCol);
    } else {
        // TODO: match either the toolbar (if shown) or background
        tabs->currBgCol = DEFAULT_CURRENT_BG_COL;
    }
    tabs->EvaluateColors(true);
    RepaintNow(win->hwndTabBar);
}

// On load of a new document we insert a new tab item in the tab bar.
TabInfo *CreateNewTab(WindowInfo *win, const WCHAR *filePath)
{
    CrashIf(!win);
    if (!win)
        return nullptr;

    TabInfo *tabs = new TabInfo(filePath);
    win->tabs.Append(tabs);
    tabs->canvasRc = win->canvasRc;

    TCITEM tcs;
    tcs.mask = TCIF_TEXT;
    tcs.pszText = (WCHAR *) tabs->GetTabTitle();

    int index = (int)win->tabs.Count() - 1;
    if (-1 != TabCtrl_InsertItem(win->hwndTabBar, index, &tcs)) {
        TabCtrl_SetCurSel(win->hwndTabBar, index);
        UpdateTabWidth(win);
    }
    else {
        // TODO: what now?
        CrashIf(true);
    }

    return tabs;
}

// Refresh the tab's title
void TabsOnChangedDoc(WindowInfo *win)
{
    TabInfo *tab = win->currentTab;
    CrashIf(!tab != !win->tabs.Count());
    if (!tab)
        return;

    CrashIf(win->tabs.Find(tab) != TabCtrl_GetCurSel(win->hwndTabBar));
    VerifyTabInfo(win, tab);
    SetTabTitle(win, tab);
}

// Called when we're closing a document
void TabsOnCloseDoc(WindowInfo *win)
{
    if (win->tabs.Count() == 0)
        return;

    /* if (win->AsFixed() && win->AsFixed()->userAnnots && win->AsFixed()->userAnnotsModified) {
        // TODO: warn about unsaved changes
    } */

    int current = TabCtrl_GetCurSel(win->hwndTabBar);
    RemoveTab(win, current);

    if (win->tabs.Count() > 0) {
        TabInfo *tab = win->tabSelectionHistory->Pop();
        TabCtrl_SetCurSel(win->hwndTabBar, win->tabs.Find(tab));
        LoadModelIntoTab(win, tab);
    }
}

// Called when we're closing an entire window (quitting)
void TabsOnCloseWindow(WindowInfo *win)
{
    TabCtrl_DeleteAllItems(win->hwndTabBar);
    win->tabSelectionHistory->Reset();
    win->currentTab = nullptr;
    win->ctrl = nullptr;
    DeleteVecMembers(win->tabs);
}

// On tab selection, we save the data for the tab which is losing selection and
// load the data of the selected tab into the WindowInfo.
LRESULT TabsOnNotify(WindowInfo *win, LPARAM lparam)
{
    LPNMHDR data = (LPNMHDR)lparam;
    int current;

    switch(data->code) {
    case TCN_SELCHANGING:
        // TODO: Should we allow the switch of the tab if we are in process of printing?
        SaveCurrentTabInfo(win);
        return FALSE;

    case TCN_SELCHANGE:
        current = TabCtrl_GetCurSel(win->hwndTabBar);
        LoadModelIntoTab(win, win->tabs.At(current));
        break;
    }
    return TRUE;
}

static void ShowTabBar(WindowInfo *win, bool show)
{
    if (show == win->tabsVisible)
        return;
    win->tabsVisible = show;
    win::SetVisibility(win->hwndTabBar, show);
    RelayoutWindow(win);
}

void UpdateTabWidth(WindowInfo *win)
{
    int count = (int)win->tabs.Count();
    bool showSingleTab = gGlobalPrefs->useTabs || win->tabsInTitlebar;
    if (count > (showSingleTab ? 0 : 1)) {
        ShowTabBar(win, true);
        ClientRect rect(win->hwndTabBar);
        SizeI tabSize = GetTabSize(win->hwndFrame);
        if (tabSize.dx > (rect.dx - 3) / count)
            tabSize.dx = (rect.dx - 3) / count;
        TabCtrl_SetItemSize(win->hwndTabBar, tabSize.dx, tabSize.dy);
    }
    else {
        ShowTabBar(win, false);
    }
}

void SetTabsInTitlebar(WindowInfo *win, bool set)
{
    if (set == win->tabsInTitlebar)
        return;
    win->tabsInTitlebar = set;
    TabsControl *tabs = (TabsControl *)GetWindowLongPtr(win->hwndTabBar, GWLP_USERDATA);
    tabs->inTitlebar = set;
    SetParent(win->hwndTabBar, set ? win->hwndCaption : win->hwndFrame);
    ShowWindow(win->hwndCaption, set ? SW_SHOW : SW_HIDE);
    if (set != win->isMenuHidden)
        ShowHideMenuBar(win);
    if (set) {
        win->caption->UpdateTheme();
        win->caption->UpdateColors(win->hwndFrame == GetForegroundWindow());
        win->caption->UpdateBackgroundAlpha();
        RelayoutCaption(win);
    }
    else if (dwm::IsCompositionEnabled()) {
        // remove the extended frame
        MARGINS margins = { 0 };
        dwm::ExtendFrameIntoClientArea(win->hwndFrame, &margins);
        win->extendedFrameHeight = 0;
    }
    SetWindowPos(win->hwndFrame, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOZORDER | SWP_NOSIZE | SWP_NOMOVE);
}

// Selects the given tab (0-based index).
void TabsSelect(WindowInfo *win, int tabIndex)
{
    int count = (int)win->tabs.Count();
    if (count < 2 || tabIndex < 0 || tabIndex >= count)
        return;
    NMHDR ntd = { nullptr, 0, TCN_SELCHANGING };
    if (TabsOnNotify(win, (LPARAM)&ntd))
        return;
    win->currentTab = win->tabs.At(tabIndex);
    int prevIndex = TabCtrl_SetCurSel(win->hwndTabBar, tabIndex);
    if (prevIndex != -1) {
        ntd.code = TCN_SELCHANGE;
        TabsOnNotify(win, (LPARAM)&ntd);
    }
}

// Selects the next (or previous) tab.
void TabsOnCtrlTab(WindowInfo *win, bool reverse)
{
    int count = (int)win->tabs.Count();
    if (count < 2)
        return;

    int next = (TabCtrl_GetCurSel(win->hwndTabBar) + (reverse ? -1 : 1) + count) % count;
    TabsSelect(win, next);
}

// Adjusts lightness by 1/255 units.
COLORREF AdjustLightness2(COLORREF c, float units)
{
    float lightness = GetLightness(c);
    units = limitValue(units, -lightness, 255.0f - lightness);
    if (0.0f == lightness)
        return RGB(BYTE(units + 0.5f), BYTE(units + 0.5f), BYTE(units + 0.5f));
    return AdjustLightness(c, 1.0f + units / lightness);
}
