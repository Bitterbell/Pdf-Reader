/* Copyright 2015 the SumatraPDF project authors (see AUTHORS file).
   License: GPLv3 */

int GetTabbarHeight(HWND, float factor=1.f);

void SaveCurrentTabInfo(WindowInfo *win);
void LoadModelIntoTab(WindowInfo *win, TabInfo *tdata);

void CreateTabbar(WindowInfo *win);
TabInfo *CreateNewTab(WindowInfo *win, const WCHAR *filePath);
void CloseAndRemoveDocInCurrentTab(WindowInfo *win);
void DestroyTabs(WindowInfo *win);
void TabsOnChangedDoc(WindowInfo *win);
LRESULT TabsOnNotify(WindowInfo *win, LPARAM lparam);
void TabsSelect(WindowInfo *win, int tabIndex);
void TabsOnCtrlTab(WindowInfo *win, bool reverse);
// also shows/hides the tabbar when necessary
void UpdateTabWidth(WindowInfo *win);
void SetTabsInTitlebar(WindowInfo *win, bool set);
void UpdateCurrentTabBgColor(WindowInfo *win);

COLORREF AdjustLightness2(COLORREF c, float units);
