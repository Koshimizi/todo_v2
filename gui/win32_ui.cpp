// =============================================================================
//  Todo 的图形界面（Win32，纯代码建界面，没有 .rc、没有第三方库）
//
//  这个文件只做三件事：
//    1. 建窗口和控件（CreateWindowExW + 系统公共控件）
//    2. 把用户动作翻译成 TaskManager 的调用
//    3. 把 Result / Task 贴到界面上（文字由 presenter 提供）
//
//  这里没有：业务规则、文件格式、转义、ID 分配 —— 它们全在 todo_core 里。
//  这就是 REFACTOR_NOTES 里说的"加图形界面只需写一个新的 UI 类"。
//
//  阅读顺序建议：
//     编码转换 → EditDialog（小的模态窗口）→ TodoWindow（主窗口）→ wWinMain
// =============================================================================

#include "presenter.h"
#include "task.h"
#include "task_manager.h"
#include "task_store.h"

#include <windows.h>
#include <commctrl.h>

#include <cstddef>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

// 让公共控件走 v6（否则 ListView / 按钮是 Win95 的样子）。
// 用链接指令写在代码里，就不需要单独的 .manifest 文件，也不依赖 mt.exe。
#pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

namespace {

// ---------------------------------------------------------------- 控件 ID --
// 控件 ID 是"控件给窗口的暗号"：按钮被点击时，Windows 用它告诉我们是谁。
constexpr int kIdTable = 1001;
constexpr int kIdInput = 1002;
constexpr int kIdPriority = 1003;
constexpr int kIdAdd = 1004;
constexpr int kIdToggle = 1010;
constexpr int kIdEdit = 1011;
constexpr int kIdDelete = 1012;
constexpr int kIdSave = 1013;
constexpr int kIdReload = 1014;
constexpr int kIdStatus = 1020;

// 编辑窗里的控件。确定/取消故意用系统约定的 IDOK / IDCANCEL，
// 这样 IsDialogMessageW 会自动把 Enter / Esc 变成"点确定 / 点取消"。
constexpr int kIdDialogLabel = 2001;
constexpr int kIdDialogInput = 2002;
constexpr int kIdDialogPriorityLabel = 2003;
constexpr int kIdDialogPriority = 2004;

constexpr UINT_PTR kInputSubclassId = 1;

const wchar_t kMainClass[] = L"TodoV2MainWindow";
const wchar_t kEditClass[] = L"TodoV2EditWindow";

// ---------------------------------------------------------------- 编码转换 --
// 核心层用 std::string 存 UTF-8，Windows 控件用 UTF-16。
// 转换只在这一个地方做，别的地方都不用关心编码。

std::wstring wideFromUtf8(const std::string& text) {
    if (text.empty()) {
        return std::wstring();
    }
    const int length = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()),
        nullptr, 0);
    if (length <= 0) {
        // 万一传进来的不是合法 UTF-8：退化成"一个字节一个字"，至少不丢内容
        std::wstring fallback;
        fallback.reserve(text.size());
        for (char c : text) {
            fallback.push_back(static_cast<wchar_t>(static_cast<unsigned char>(c)));
        }
        return fallback;
    }
    std::wstring wide(static_cast<std::size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                        static_cast<int>(text.size()), wide.data(), length);
    return wide;
}

std::string utf8FromWide(const std::wstring& text) {
    if (text.empty()) {
        return std::string();
    }
    const int length = WideCharToMultiByte(CP_UTF8, 0, text.data(),
                                           static_cast<int>(text.size()), nullptr, 0,
                                           nullptr, nullptr);
    if (length <= 0) {
        return std::string();
    }
    std::string narrow(static_cast<std::size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                        narrow.data(), length, nullptr, nullptr);
    return narrow;
}

// 旧文件可能是按系统代码页（简体中文 Windows 上是 936/GBK）写的。
std::string utf8FromAnsi(const std::string& bytes) {
    if (bytes.empty()) {
        return std::string();
    }
    const int length = MultiByteToWideChar(CP_ACP, 0, bytes.data(),
                                            static_cast<int>(bytes.size()), nullptr, 0);
    if (length <= 0) {
        return std::string();
    }
    std::wstring wide(static_cast<std::size_t>(length), L'\0');
    MultiByteToWideChar(CP_ACP, 0, bytes.data(), static_cast<int>(bytes.size()),
                        wide.data(), length);
    return utf8FromWide(wide);
}

bool isValidUtf8(const std::string& text) {
    if (text.empty()) {
        return true;
    }
    return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                               static_cast<int>(text.size()), nullptr, 0) > 0;
}

// ------------------------------------------------------------------ 小工具 --

std::wstring windowText(HWND control) {
    const int length = GetWindowTextLengthW(control);
    if (length <= 0) {
        return std::wstring();
    }
    std::wstring text(static_cast<std::size_t>(length) + 1u, L'\0');
    const int copied = GetWindowTextW(control, text.data(), length + 1);
    text.resize(copied > 0 ? static_cast<std::size_t>(copied) : 0u);
    return text;
}

std::string trimAscii(const std::string& text) {
    std::size_t begin = 0;
    while (begin < text.size()) {
        const char c = text[begin];
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') {
            break;
        }
        ++begin;
    }
    std::size_t end = text.size();
    while (end > begin) {
        const char c = text[end - 1u];
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') {
            break;
        }
        --end;
    }
    return text.substr(begin, end - begin);
}

int systemDpi() {
    HDC screen = GetDC(nullptr);
    int dpi = 96;
    if (screen != nullptr) {
        dpi = GetDeviceCaps(screen, LOGPIXELSY);
        ReleaseDC(nullptr, screen);
    }
    return dpi > 0 ? dpi : 96;
}

todo::Priority priorityAt(int comboIndex) {
    switch (comboIndex) {
        case 0:
            return todo::Priority::High;
        case 2:
            return todo::Priority::Low;
        default:
            return todo::Priority::Medium;
    }
}

int comboIndexFor(todo::Priority priority) {
    switch (priority) {
        case todo::Priority::High:
            return 0;
        case todo::Priority::Low:
            return 2;
        case todo::Priority::Medium:
            break;
    }
    return 1;
}

// ------------------------------------------------- 旧数据文件的一次性迁移 --
// 旧的控制台版本按系统代码页把字节原样写盘（简体中文 Windows 上就是 GBK），
// 而图形界面统一按 UTF-8 读写。启动时检查一次：
//   · 已经是合法 UTF-8  → 什么也不做
//   · 不是              → 备份成 tasks.txt.bak，再按 ANSI 代码页转成 UTF-8 写回
// 这样你以前存的任务不会变成乱码，而文件从此是标准 UTF-8。
void migrateLegacyFileIfNeeded(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return;  // 还没有文件：第一次运行，不用迁移
    }
    std::string bytes;
    bytes.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    input.close();
    if (bytes.empty()) {
        return;
    }

    const bool hasBom = bytes.size() >= 3u &&
                        static_cast<unsigned char>(bytes[0]) == 0xEFu &&
                        static_cast<unsigned char>(bytes[1]) == 0xBBu &&
                        static_cast<unsigned char>(bytes[2]) == 0xBFu;
    if (!hasBom && isValidUtf8(bytes)) {
        return;  // 已经是好的，一个字都不动
    }

    std::string converted;
    if (hasBom) {
        converted = bytes.substr(3);  // 去掉 BOM，TaskStore 按纯文本解析
    } else {
        converted = utf8FromAnsi(bytes);
    }
    if (converted.empty()) {
        return;
    }

    std::ofstream backup(path + ".bak", std::ios::binary | std::ios::trunc);
    backup.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    backup.close();

    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(converted.data(), static_cast<std::streamsize>(converted.size()));
}

// =============================================================================
//  EditDialog：一个小的模态窗口，用来改一条任务的描述和优先级。
//
//  它自己跑一个"内层消息循环"，在关闭前主窗口被禁用 —— 这就是模态窗口的
//  全部秘密。DialogBoxParam 内部做的也是这件事，只是它还需要一个 .rc 模板；
//  我们用普通窗口 + IsDialogMessageW，就省掉了资源文件。
// =============================================================================

class EditDialog {
public:
    EditDialog(HWND owner, std::string description, todo::Priority priority);
    ~EditDialog();

    EditDialog(const EditDialog&) = delete;
    EditDialog& operator=(const EditDialog&) = delete;

    // 返回 true 表示用户按了"确定"。
    bool run();

    const std::string& description() const { return description_; }
    todo::Priority priority() const { return priority_; }

    static LRESULT CALLBACK sWndProc(HWND hwnd, UINT message, WPARAM wParam,
                                     LPARAM lParam);

private:
    LRESULT wndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    bool create();
    void onCreate();
    void layout();
    void centerOnOwner();
    void onOk();

    HWND owner_ = nullptr;
    HWND hwnd_ = nullptr;
    HWND input_ = nullptr;
    HWND priorityCombo_ = nullptr;
    HWND buttonOk_ = nullptr;
    HWND buttonCancel_ = nullptr;
    HFONT font_ = nullptr;
    std::string description_;
    todo::Priority priority_ = todo::Priority::Medium;
    bool accepted_ = false;
    bool finished_ = false;
    int dpi_ = 96;
};

EditDialog::EditDialog(HWND owner, std::string description, todo::Priority priority)
    : owner_(owner), description_(std::move(description)), priority_(priority) {
    dpi_ = systemDpi();
}

EditDialog::~EditDialog() = default;

bool EditDialog::create() {
    // 窗口字体直接跟主窗口借，保证两个窗口的字体一致。
    // WM_GETFONT 返回的是整数（LRESULT），要显式变回句柄。
    font_ = reinterpret_cast<HFONT>(SendMessageW(owner_, WM_GETFONT, 0, 0));

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = static_cast<UINT>(sizeof(windowClass));
    windowClass.lpfnWndProc = &EditDialog::sWndProc;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(static_cast<INT_PTR>(COLOR_BTNFACE + 1));
    windowClass.lpszClassName = kEditClass;
    if (RegisterClassExW(&windowClass) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return false;
    }

    const int width = MulDiv(440, dpi_, 96);
    const int height = MulDiv(210, dpi_, 96);
    hwnd_ = CreateWindowExW(WS_EX_DLGMODALFRAME, kEditClass, L"编辑任务",
                            WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN,
                            CW_USEDEFAULT, CW_USEDEFAULT, width, height, owner_,
                            nullptr, GetModuleHandleW(nullptr), this);
    if (hwnd_ == nullptr) {
        return false;
    }
    centerOnOwner();
    return true;
}

LRESULT CALLBACK EditDialog::sWndProc(HWND hwnd, UINT message, WPARAM wParam,
                                      LPARAM lParam) {
    if (message == WM_NCCREATE) {
        auto* createStruct = reinterpret_cast<CREATESTRUCTW*>(lParam);
        auto* self = static_cast<EditDialog*>(createStruct->lpCreateParams);
        self->hwnd_ = hwnd;
        // 把 this 存进窗口自己的存储空间，后面每条消息都能取回来
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        // 必须交给默认过程：窗口标题就是在 WM_NCCREATE 里被记住的，
        // 直接 return TRUE 会让标题栏变成空白（这个坑踩过一次）。
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }
    auto* self = reinterpret_cast<EditDialog*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self == nullptr) {
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }
    return self->wndProc(hwnd, message, wParam, lParam);
}

LRESULT EditDialog::wndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_CREATE:
            onCreate();
            return 0;

        case WM_SIZE:
            layout();
            return 0;

        case WM_COMMAND:
            if (LOWORD(wParam) == IDOK) {
                onOk();
                return 0;
            }
            if (LOWORD(wParam) == IDCANCEL) {
                DestroyWindow(hwnd);
                return 0;
            }
            break;

        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY:
            // 注意：不调用 PostQuitMessage —— 主窗口还活着，
            // 这里只是让内层消息循环退出。
            finished_ = true;
            return 0;

        case WM_GETMINMAXINFO: {
            auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
            info->ptMinTrackSize.x = MulDiv(380, dpi_, 96);
            info->ptMinTrackSize.y = MulDiv(180, dpi_, 96);
            return 0;
        }

        default:
            break;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

void EditDialog::onCreate() {
    HWND label = CreateWindowExW(
        0, L"STATIC", L"任务描述", WS_CHILD | WS_VISIBLE | SS_LEFT,
        0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdDialogLabel)),
        GetModuleHandleW(nullptr), nullptr);

    HWND priorityLabel = CreateWindowExW(
        0, L"STATIC", L"优先级", WS_CHILD | WS_VISIBLE | SS_LEFT,
        0, 0, 0, 0, hwnd_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdDialogPriorityLabel)),
        GetModuleHandleW(nullptr), nullptr);

    input_ = CreateWindowExW(
        WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdDialogInput)),
        GetModuleHandleW(nullptr), nullptr);
    SendMessageW(input_, EM_SETLIMITTEXT, 400, 0);
    SendMessageW(input_, WM_SETTEXT, 0,
                 reinterpret_cast<LPARAM>(wideFromUtf8(description_).c_str()));

    priorityCombo_ = CreateWindowExW(
        0, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
        0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdDialogPriority)),
        GetModuleHandleW(nullptr), nullptr);
    SendMessageW(priorityCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"高"));
    SendMessageW(priorityCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"中"));
    SendMessageW(priorityCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"低"));
    SendMessageW(priorityCombo_, CB_SETCURSEL,
                 static_cast<WPARAM>(comboIndexFor(priority_)), 0);

    buttonOk_ = CreateWindowExW(
        0, L"BUTTON", L"确定",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
        0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDOK)),
        GetModuleHandleW(nullptr), nullptr);

    buttonCancel_ = CreateWindowExW(
        0, L"BUTTON", L"取消",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDCANCEL)),
        GetModuleHandleW(nullptr), nullptr);

    // 对话框里的控件不多，直接把字体统一刷一遍
    const HWND controls[] = {label,       input_,  priorityLabel,
                             priorityCombo_, buttonOk_, buttonCancel_};
    for (HWND control : controls) {
        if (font_ != nullptr) {
            SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
        }
    }

    layout();
}

void EditDialog::layout() {
    RECT client{};
    GetClientRect(hwnd_, &client);
    const int margin = MulDiv(16, dpi_, 96);
    const int gap = MulDiv(10, dpi_, 96);
    const int rowHeight = MulDiv(26, dpi_, 96);
    const int labelWidth = MulDiv(72, dpi_, 96);
    const int buttonWidth = MulDiv(84, dpi_, 96);
    const int contentWidth = client.right - margin * 2;

    const HWND label = GetDlgItem(hwnd_, kIdDialogLabel);
    MoveWindow(label, margin, margin, labelWidth, rowHeight, TRUE);
    MoveWindow(input_, margin + labelWidth + gap, margin,
               contentWidth - labelWidth - gap, rowHeight, TRUE);

    const int secondRow = margin + rowHeight + gap;
    MoveWindow(GetDlgItem(hwnd_, kIdDialogPriorityLabel), margin, secondRow,
               labelWidth, rowHeight, TRUE);
    MoveWindow(priorityCombo_, margin + labelWidth + gap, secondRow,
               MulDiv(120, dpi_, 96), MulDiv(200, dpi_, 96), TRUE);

    const int buttonRow = client.bottom - margin - rowHeight;
    MoveWindow(buttonCancel_, client.right - margin - buttonWidth, buttonRow,
               buttonWidth, rowHeight, TRUE);
    MoveWindow(buttonOk_, client.right - margin - buttonWidth * 2 - gap, buttonRow,
               buttonWidth, rowHeight, TRUE);
}

void EditDialog::centerOnOwner() {
    RECT ownerRect{};
    RECT dialogRect{};
    if (GetWindowRect(owner_, &ownerRect) == FALSE ||
        GetWindowRect(hwnd_, &dialogRect) == FALSE) {
        return;
    }
    const int width = dialogRect.right - dialogRect.left;
    const int height = dialogRect.bottom - dialogRect.top;
    const int x = ownerRect.left + ((ownerRect.right - ownerRect.left) - width) / 2;
    const int y = ownerRect.top + ((ownerRect.bottom - ownerRect.top) - height) / 2;
    SetWindowPos(hwnd_, HWND_TOP, x, y, 0, 0, SWP_NOSIZE);
}

void EditDialog::onOk() {
    const std::string description = trimAscii(utf8FromWide(windowText(input_)));
    if (description.empty()) {
        MessageBoxW(hwnd_, L"任务描述不能为空。", L"提示", MB_OK | MB_ICONWARNING);
        SetFocus(input_);
        return;
    }
    description_ = description;
    priority_ = priorityAt(static_cast<int>(SendMessageW(priorityCombo_, CB_GETCURSEL, 0, 0)));
    accepted_ = true;
    DestroyWindow(hwnd_);
}

bool EditDialog::run() {
    if (!create()) {
        return false;
    }
    EnableWindow(owner_, FALSE);  // 主窗口先禁用 —— 模态
    ShowWindow(hwnd_, SW_SHOW);
    SetFocus(input_);
    SendMessageW(input_, EM_SETSEL, 0, -1);  // 全选，方便直接改写

    MSG message{};
    while (!finished_) {
        const BOOL got = GetMessageW(&message, nullptr, 0, 0);
        if (got <= 0) {
            if (got == 0) {
                PostQuitMessage(static_cast<int>(message.wParam));
            }
            break;
        }
        // IsDialogMessageW 负责 Tab 切换、Enter 点默认按钮、Esc 点取消
        if (IsDialogMessageW(hwnd_, &message) != 0) {
            continue;
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    EnableWindow(owner_, TRUE);
    SetActiveWindow(owner_);
    return accepted_;
}

// =============================================================================
//  TodoWindow：主窗口
// =============================================================================

class TodoWindow {
public:
    explicit TodoWindow(std::string path);
    ~TodoWindow();

    TodoWindow(const TodoWindow&) = delete;
    TodoWindow& operator=(const TodoWindow&) = delete;

    bool create();
    int run();

    static LRESULT CALLBACK sWndProc(HWND hwnd, UINT message, WPARAM wParam,
                                     LPARAM lParam);
    static LRESULT CALLBACK sInputSubclassProc(HWND hwnd, UINT message, WPARAM wParam,
                                               LPARAM lParam, UINT_PTR id,
                                               DWORD_PTR refData);

private:
    LRESULT wndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

    int scaled(int pixels) const;
    void createFonts();
    void createControls();
    HWND createButton(int id, const wchar_t* text, DWORD buttonStyle);
    void addColumn(int index, const wchar_t* title, int width);
    void layout();
    void layoutColumns();

    void refreshList();
    void setSubItem(int row, int column, const std::wstring& text);
    void setSummary();
    void updateTitle();
    void updateSelectionButtons();
    void setStatus(bool ok, const std::string& message);
    void setResult(const todo::Result& result);

    int selectedId() const;
    void selectTask(int id);

    void addTask();
    void toggleSelected();
    void editSelected();
    void deleteSelected();
    void saveToDisk();
    void reloadFromDisk(bool confirmDiscard);
    bool confirmClose();

    HINSTANCE instance_ = nullptr;
    HWND hwnd_ = nullptr;
    HWND table_ = nullptr;
    HWND input_ = nullptr;
    HWND priority_ = nullptr;
    HWND buttonAdd_ = nullptr;
    HWND buttonToggle_ = nullptr;
    HWND buttonEdit_ = nullptr;
    HWND buttonDelete_ = nullptr;
    HWND buttonSave_ = nullptr;
    HWND buttonReload_ = nullptr;
    HWND status_ = nullptr;
    HFONT uiFont_ = nullptr;
    HFONT strikeFont_ = nullptr;
    bool ownsUiFont_ = false;
    bool ownsStrikeFont_ = false;
    HACCEL accelerator_ = nullptr;
    int dpi_ = 96;
    int minWidth_ = 0;
    int minHeight_ = 0;
    bool dirty_ = false;
    todo::TaskManager manager_;
};

TodoWindow::TodoWindow(std::string path)
    : manager_{todo::TaskStore(std::move(path))} {
    dpi_ = systemDpi();
}

TodoWindow::~TodoWindow() = default;

int TodoWindow::scaled(int pixels) const { return MulDiv(pixels, dpi_, 96); }

bool TodoWindow::create() {
    instance_ = GetModuleHandleW(nullptr);
    createFonts();

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = static_cast<UINT>(sizeof(windowClass));
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = &TodoWindow::sWndProc;
    windowClass.hInstance = instance_;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(static_cast<INT_PTR>(COLOR_WINDOW + 1));
    windowClass.lpszClassName = kMainClass;
    if (RegisterClassExW(&windowClass) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return false;
    }

    // 窗口按 DPI 缩放后可能比屏幕还大（1366x768 的笔记本 + 150% 缩放就会），
    // 那样 Windows 会把它压小，而按压缩前的尺寸算出来的列宽就全错了。
    // 所以先读工作区，把初始尺寸夹到工作区以内。
    RECT workArea{};
    if (SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0) == FALSE) {
        workArea.left = 0;
        workArea.top = 0;
        workArea.right = scaled(900);
        workArea.bottom = scaled(640);
    }
    int width = scaled(900);
    int height = scaled(640);
    const int roomWidth = workArea.right - workArea.left - scaled(30);
    const int roomHeight = workArea.bottom - workArea.top - scaled(30);
    if (width > roomWidth) {
        width = roomWidth;
    }
    if (height > roomHeight) {
        height = roomHeight;
    }
    if (width < scaled(600)) {
        width = scaled(600);
    }
    if (height < scaled(420)) {
        height = scaled(420);
    }
    // 最小尺寸不能大于初始尺寸，否则用户拖不动窗口
    minWidth_ = scaled(780) < width ? scaled(780) : width;
    minHeight_ = scaled(520) < height ? scaled(520) : height;

    hwnd_ = CreateWindowExW(0, kMainClass, L"待办清单",
                            WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT,
                            CW_USEDEFAULT, width, height, nullptr, nullptr,
                            instance_, this);
    if (hwnd_ == nullptr) {
        return false;
    }

    // 快捷键：F5 重新加载、Ctrl+S 保存。表格里按 Del 删除在 LVN_KEYDOWN 里处理。
    // 注意这里不能用 const：CreateAcceleratorTableW 要的是可写指针（LPACCEL）。
    ACCEL accelerators[] = {
        {FVIRTKEY, VK_F5, static_cast<WORD>(kIdReload)},
        {static_cast<BYTE>(FVIRTKEY | FCONTROL), static_cast<WORD>('S'),
         static_cast<WORD>(kIdSave)},
    };
    accelerator_ = CreateAcceleratorTableW(
        accelerators, static_cast<int>(sizeof(accelerators) / sizeof(accelerators[0])));
    return true;
}

LRESULT CALLBACK TodoWindow::sWndProc(HWND hwnd, UINT message, WPARAM wParam,
                                      LPARAM lParam) {
    if (message == WM_NCCREATE) {
        auto* createStruct = reinterpret_cast<CREATESTRUCTW*>(lParam);
        auto* self = static_cast<TodoWindow*>(createStruct->lpCreateParams);
        self->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        // 同样要交给默认过程，否则初始窗口标题不会被记住
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }
    auto* self = reinterpret_cast<TodoWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self == nullptr) {
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }
    return self->wndProc(hwnd, message, wParam, lParam);
}

LRESULT TodoWindow::wndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_CREATE:
            createControls();
            reloadFromDisk(false);
            updateTitle();
            return 0;

        case WM_SIZE:
            if (table_ != nullptr) {
                layout();
            }
            return 0;

        case WM_GETMINMAXINFO: {
            auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
            info->ptMinTrackSize.x = minWidth_;
            info->ptMinTrackSize.y = minHeight_;
            return 0;
        }

        case WM_COMMAND: {
            switch (LOWORD(wParam)) {
                case kIdAdd:
                    addTask();
                    return 0;
                case kIdToggle:
                    toggleSelected();
                    return 0;
                case kIdEdit:
                    editSelected();
                    return 0;
                case kIdDelete:
                    deleteSelected();
                    return 0;
                case kIdSave:
                    saveToDisk();
                    return 0;
                case kIdReload:
                    reloadFromDisk(true);
                    return 0;
                default:
                    break;
            }
            break;
        }

        case WM_NOTIFY: {
            auto* header = reinterpret_cast<NMHDR*>(lParam);
            if (header->idFrom != static_cast<UINT_PTR>(kIdTable)) {
                break;
            }
            switch (header->code) {
                case NM_DBLCLK: {
                    auto* activate = reinterpret_cast<NMITEMACTIVATE*>(lParam);
                    if (activate->iItem >= 0) {
                        ListView_SetItemState(table_, activate->iItem,
                                              LVIS_SELECTED | LVIS_FOCUSED,
                                              LVIS_SELECTED | LVIS_FOCUSED);
                        toggleSelected();
                    }
                    return 0;
                }
                case LVN_ITEMCHANGED: {
                    const auto* changed = reinterpret_cast<NMLISTVIEW*>(lParam);
                    if ((changed->uChanged & LVIF_STATE) != 0u) {
                        updateSelectionButtons();
                    }
                    return 0;
                }
                case LVN_KEYDOWN: {
                    const auto* keyDown = reinterpret_cast<NMLVKEYDOWN*>(lParam);
                    if (keyDown->wVKey == VK_DELETE) {
                        deleteSelected();
                    }
                    return 0;
                }
                case NM_CUSTOMDRAW: {
                    // 已完成的任务画成灰色 + 删除线，一眼就能分清
                    auto* draw = reinterpret_cast<NMLVCUSTOMDRAW*>(lParam);
                    if (draw->nmcd.dwDrawStage == static_cast<DWORD>(CDDS_PREPAINT)) {
                        return CDRF_NOTIFYITEMDRAW;
                    }
                    if (draw->nmcd.dwDrawStage == static_cast<DWORD>(CDDS_ITEMPREPAINT)) {
                        const todo::Task* task =
                            manager_.find(static_cast<int>(draw->nmcd.lItemlParam));
                        if (task != nullptr && task->completed()) {
                            draw->clrText = RGB(150, 150, 150);
                            SelectObject(draw->nmcd.hdc, strikeFont_);
                            return CDRF_NEWFONT;
                        }
                        return CDRF_DODEFAULT;
                    }
                    return CDRF_DODEFAULT;
                }
                default:
                    break;
            }
            break;
        }

        case WM_CLOSE:
            if (!confirmClose()) {
                return 0;  // 用户按了"取消"，窗口不关
            }
            DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY:
            if (ownsStrikeFont_ && strikeFont_ != nullptr) {
                DeleteObject(strikeFont_);
            }
            if (ownsUiFont_ && uiFont_ != nullptr) {
                DeleteObject(uiFont_);
            }
            strikeFont_ = nullptr;
            uiFont_ = nullptr;
            if (accelerator_ != nullptr) {
                DestroyAcceleratorTable(accelerator_);
                accelerator_ = nullptr;
            }
            PostQuitMessage(0);
            return 0;

        default:
            break;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

void TodoWindow::createFonts() {
    // 用系统"消息字体"，大小和 DPI 由 Windows 给，省得自己猜字号
    NONCLIENTMETRICSW metrics{};
    metrics.cbSize = static_cast<UINT>(sizeof(metrics));
    if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0) != FALSE) {
        uiFont_ = CreateFontIndirectW(&metrics.lfMessageFont);
        ownsUiFont_ = uiFont_ != nullptr;
        if (uiFont_ != nullptr) {
            metrics.lfMessageFont.lfStrikeOut = TRUE;
            strikeFont_ = CreateFontIndirectW(&metrics.lfMessageFont);
            ownsStrikeFont_ = strikeFont_ != nullptr;
        }
    }
    if (uiFont_ == nullptr) {
        uiFont_ = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    }
    if (strikeFont_ == nullptr) {
        strikeFont_ = uiFont_;  // 退化成普通字体，至少不会崩
    }
}

HWND TodoWindow::createButton(int id, const wchar_t* text, DWORD buttonStyle) {
    return CreateWindowExW(0, L"BUTTON", text,
                           WS_CHILD | WS_VISIBLE | WS_TABSTOP | buttonStyle, 0, 0, 0,
                           0, hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                           instance_, nullptr);
}

void TodoWindow::addColumn(int index, const wchar_t* title, int width) {
    LVCOLUMNW column{};
    column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
    column.fmt = LVCFMT_LEFT;
    column.cx = width;
    column.pszText = const_cast<LPWSTR>(title);
    SendMessageW(table_, LVM_INSERTCOLUMNW, static_cast<WPARAM>(index),
                 reinterpret_cast<LPARAM>(&column));
}

void TodoWindow::createControls() {
    table_ = CreateWindowExW(
        WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
        0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdTable)),
        instance_, nullptr);
    ListView_SetExtendedListViewStyle(
        table_, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    addColumn(0, L"状态", scaled(90));
    addColumn(1, L"任务", scaled(420));
    addColumn(2, L"优先级", scaled(90));
    addColumn(3, L"ID", scaled(60));

    input_ = CreateWindowExW(
        WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 0, 0, hwnd_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdInput)), instance_, nullptr);
    SendMessageW(input_, EM_SETLIMITTEXT, 400, 0);
    // 输入框自己不认识"回车 = 添加"，得用子类化接管它的按键（见 sInputSubclassProc）
    SetWindowSubclass(input_, &TodoWindow::sInputSubclassProc, kInputSubclassId,
                      reinterpret_cast<DWORD_PTR>(this));

    priority_ = CreateWindowExW(
        0, L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL, 0, 0, 0, 0,
        hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdPriority)), instance_,
        nullptr);
    SendMessageW(priority_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"高"));
    SendMessageW(priority_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"中"));
    SendMessageW(priority_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"低"));
    SendMessageW(priority_, CB_SETCURSEL, 1, 0);

    buttonAdd_ = createButton(kIdAdd, L"添加", BS_DEFPUSHBUTTON);
    buttonToggle_ = createButton(kIdToggle, L"切换完成", BS_PUSHBUTTON);
    buttonEdit_ = createButton(kIdEdit, L"编辑", BS_PUSHBUTTON);
    buttonDelete_ = createButton(kIdDelete, L"删除", BS_PUSHBUTTON);
    buttonSave_ = createButton(kIdSave, L"保存", BS_PUSHBUTTON);
    buttonReload_ = createButton(kIdReload, L"重新加载", BS_PUSHBUTTON);

    status_ = CreateWindowExW(
        0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_LEFTNOWORDWRAP, 0, 0, 0, 0,
        hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdStatus)), instance_,
        nullptr);

    const HWND controls[] = {table_,      input_,        priority_,   buttonAdd_,
                             buttonToggle_, buttonEdit_, buttonDelete_, buttonSave_,
                             buttonReload_, status_};
    for (HWND control : controls) {
        if (uiFont_ != nullptr) {
            SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont_), TRUE);
        }
    }

    layout();
}

void TodoWindow::layout() {
    RECT client{};
    GetClientRect(hwnd_, &client);
    const int margin = scaled(14);
    const int gap = scaled(8);
    const int rowHeight = scaled(30);
    const int statusHeight = scaled(22);
    const int contentWidth = client.right - margin * 2;

    int tableHeight = client.bottom - margin * 2 - statusHeight - gap * 3 -
                      rowHeight * 2;
    if (tableHeight < scaled(80)) {
        tableHeight = scaled(80);
    }

    int y = margin;
    MoveWindow(table_, margin, y, contentWidth, tableHeight, TRUE);
    y += tableHeight + gap;

    // 输入行：输入框吃掉剩余宽度，右边是优先级下拉和"添加"
    const int priorityWidth = scaled(96);
    const int addWidth = scaled(92);
    int inputWidth = contentWidth - priorityWidth - addWidth - gap * 2;
    if (inputWidth < 0) {
        inputWidth = 0;
    }
    MoveWindow(input_, margin, y, inputWidth, rowHeight, TRUE);
    int x = margin + inputWidth + gap;
    MoveWindow(priority_, x, y, priorityWidth, scaled(200), TRUE);  // 高度留给下拉列表
    x += priorityWidth + gap;
    MoveWindow(buttonAdd_, x, y, addWidth, rowHeight, TRUE);
    y += rowHeight + gap;

    // 动作行：左边是"针对选中行"的操作，右边是"针对文件"的操作
    int left = margin;
    MoveWindow(buttonToggle_, left, y, scaled(104), rowHeight, TRUE);
    left += scaled(104) + gap;
    MoveWindow(buttonEdit_, left, y, scaled(76), rowHeight, TRUE);
    left += scaled(76) + gap;
    MoveWindow(buttonDelete_, left, y, scaled(76), rowHeight, TRUE);

    int right = margin + contentWidth;
    right -= scaled(104);
    MoveWindow(buttonReload_, right, y, scaled(104), rowHeight, TRUE);
    right -= gap + scaled(72);
    MoveWindow(buttonSave_, right, y, scaled(72), rowHeight, TRUE);

    y += rowHeight + gap;
    MoveWindow(status_, margin, y, contentWidth, statusHeight, TRUE);

    layoutColumns();
}

void TodoWindow::layoutColumns() {
    if (table_ == nullptr) {
        return;
    }
    RECT client{};
    GetClientRect(table_, &client);
    const int statusWidth = scaled(90);
    const int priorityWidth = scaled(90);
    const int idWidth = scaled(60);
    const int scrollbar = scaled(20);  // 给竖滚动条留位置
    int descriptionWidth = client.right - statusWidth - priorityWidth - idWidth - scrollbar;
    if (descriptionWidth < scaled(160)) {
        descriptionWidth = scaled(160);
    }
    ListView_SetColumnWidth(table_, 0, statusWidth);
    ListView_SetColumnWidth(table_, 1, descriptionWidth);
    ListView_SetColumnWidth(table_, 2, priorityWidth);
    ListView_SetColumnWidth(table_, 3, idWidth);
}

void TodoWindow::setSubItem(int row, int column, const std::wstring& text) {
    LVITEMW item{};
    item.mask = LVIF_TEXT;
    item.iItem = row;
    item.iSubItem = column;
    item.pszText = const_cast<LPWSTR>(text.c_str());
    ListView_SetItem(table_, &item);
}

void TodoWindow::refreshList() {
    ListView_DeleteAllItems(table_);
    int row = 0;
    for (const todo::Task& task : manager_.tasks()) {
        const std::wstring status = wideFromUtf8(todo::statusLabel(task.completed()));
        const std::wstring description = wideFromUtf8(todo::singleLine(task.description()));
        const std::wstring priority = wideFromUtf8(todo::priorityLabel(task.priority()));
        const std::wstring id = std::to_wstring(task.id());

        LVITEMW item{};
        item.mask = LVIF_TEXT | LVIF_PARAM;
        item.iItem = row;
        // 把 ID 塞进这一行的 lParam：以后从选中行取回"是哪条任务"靠它，
        // 而不是去解析界面上的文字。
        item.lParam = static_cast<LPARAM>(task.id());
        item.pszText = const_cast<LPWSTR>(status.c_str());
        if (ListView_InsertItem(table_, &item) < 0) {
            break;
        }
        setSubItem(row, 1, description);
        setSubItem(row, 2, priority);
        setSubItem(row, 3, id);
        ++row;
    }
    layoutColumns();
    updateSelectionButtons();
    setSummary();
}

void TodoWindow::setSummary() {
    std::size_t completed = 0;
    for (const todo::Task& task : manager_.tasks()) {
        if (task.completed()) {
            ++completed;
        }
    }
    setStatus(true, todo::summaryLabel(manager_.tasks().size(), completed));
}

void TodoWindow::updateTitle() {
    const std::wstring title = wideFromUtf8(todo::windowTitle(manager_.path(), dirty_));
    SetWindowTextW(hwnd_, title.c_str());
}

void TodoWindow::setStatus(bool ok, const std::string& message) {
    const std::wstring text = wideFromUtf8((ok ? "[OK] " : "[X] ") + message);
    SetWindowTextW(status_, text.c_str());
}

void TodoWindow::setResult(const todo::Result& result) {
    setStatus(result.ok, todo::resultLabel(result));
}

void TodoWindow::updateSelectionButtons() {
    const BOOL hasSelection = selectedId() != 0 ? TRUE : FALSE;
    EnableWindow(buttonToggle_, hasSelection);
    EnableWindow(buttonEdit_, hasSelection);
    EnableWindow(buttonDelete_, hasSelection);
}

int TodoWindow::selectedId() const {
    const int row = ListView_GetNextItem(table_, -1, LVNI_SELECTED);
    if (row < 0) {
        return 0;
    }
    LVITEMW item{};
    item.mask = LVIF_PARAM;
    item.iItem = row;
    if (ListView_GetItem(table_, &item) == FALSE) {
        return 0;
    }
    return static_cast<int>(item.lParam);
}

void TodoWindow::selectTask(int id) {
    const int count = ListView_GetItemCount(table_);
    for (int row = 0; row < count; ++row) {
        LVITEMW item{};
        item.mask = LVIF_PARAM;
        item.iItem = row;
        if (ListView_GetItem(table_, &item) == FALSE) {
            continue;
        }
        if (static_cast<int>(item.lParam) == id) {
            ListView_SetItemState(table_, row, LVIS_SELECTED | LVIS_FOCUSED,
                                  LVIS_SELECTED | LVIS_FOCUSED);
            ListView_EnsureVisible(table_, row, FALSE);
            return;
        }
    }
}

void TodoWindow::addTask() {
    const std::string description = trimAscii(utf8FromWide(windowText(input_)));
    const todo::Priority priority =
        priorityAt(static_cast<int>(SendMessageW(priority_, CB_GETCURSEL, 0, 0)));
    const todo::Result result = manager_.add(description, priority);
    if (result.ok) {
        dirty_ = true;
        SetWindowTextW(input_, L"");
        refreshList();
        updateTitle();
        SetFocus(input_);
    }
    setResult(result);
}

void TodoWindow::toggleSelected() {
    const int id = selectedId();
    if (id == 0) {
        setStatus(false, "请先在列表里选中一条任务");
        return;
    }
    const todo::Task* task = manager_.find(id);  // 借用指针：只在本次调用里用
    if (task == nullptr) {
        setStatus(false, "这条任务已经不存在了");
        return;
    }
    const bool completed = !task->completed();
    const todo::Result result = manager_.setCompleted(id, completed);
    if (result.ok) {
        dirty_ = true;
        refreshList();
        selectTask(id);
        updateTitle();
    }
    setResult(result);
}

void TodoWindow::editSelected() {
    const int id = selectedId();
    if (id == 0) {
        setStatus(false, "请先在列表里选中一条任务");
        return;
    }
    const todo::Task* task = manager_.find(id);
    if (task == nullptr) {
        setStatus(false, "这条任务已经不存在了");
        return;
    }

    // 编辑窗的初始优先级来自"这条任务原本的值"，而不是下面那个新增用的下拉框
    EditDialog dialog(hwnd_, task->description(), task->priority());
    if (!dialog.run()) {
        return;  // 用户取消了
    }

    const todo::Result result = manager_.edit(id, dialog.description(), dialog.priority());
    if (result.ok) {
        dirty_ = true;
        refreshList();
        selectTask(id);
        updateTitle();
    }
    setResult(result);
}

void TodoWindow::deleteSelected() {
    const int id = selectedId();
    if (id == 0) {
        setStatus(false, "请先在列表里选中一条任务");
        return;
    }
    const todo::Task* task = manager_.find(id);
    if (task == nullptr) {
        setStatus(false, "这条任务已经不存在了");
        return;
    }

    const std::wstring question =
        L"确定删除「" + wideFromUtf8(todo::singleLine(task->description())) + L"」吗？";
    if (MessageBoxW(hwnd_, question.c_str(), L"删除任务",
                    MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES) {
        return;
    }

    const todo::Result result = manager_.remove(id);
    if (result.ok) {
        dirty_ = true;
        refreshList();
        updateTitle();
    }
    setResult(result);
}

void TodoWindow::saveToDisk() {
    std::string error;
    if (!manager_.save(error)) {
        setStatus(false, "保存失败：" + error);
        return;
    }
    dirty_ = false;
    updateTitle();
    setStatus(true, "已保存 " + std::to_string(manager_.tasks().size()) + " 条任务到 " +
                        manager_.path());
}

void TodoWindow::reloadFromDisk(bool confirmDiscard) {
    if (confirmDiscard && dirty_) {
        if (MessageBoxW(hwnd_, L"重新加载会丢掉还没保存的改动，继续吗？", L"重新加载",
                        MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) {
            return;
        }
    }
    std::string warning;
    manager_.load(warning);
    dirty_ = false;
    refreshList();
    updateTitle();
    if (!warning.empty()) {
        setStatus(false, "加载时有行被跳过：" + warning);
    }
}

bool TodoWindow::confirmClose() {
    if (!dirty_) {
        return true;
    }
    const int answer = MessageBoxW(hwnd_, L"还有没保存的改动，要先保存吗？", L"退出",
                                   MB_YESNOCANCEL | MB_ICONQUESTION);
    if (answer == IDCANCEL) {
        return false;
    }
    if (answer == IDYES) {
        std::string error;
        if (!manager_.save(error)) {
            const std::wstring text =
                L"保存失败：" + wideFromUtf8(error) + L"\n还要直接退出、丢掉这些改动吗？";
            return MessageBoxW(hwnd_, text.c_str(), L"保存失败",
                               MB_YESNO | MB_ICONERROR) == IDYES;
        }
        dirty_ = false;
    }
    return true;
}

// 输入框的子类化过程：只接管两个键，其余全部交回系统默认处理。
// 子类化的意思是"在控件原有的窗口过程前面挂一层"，是 Win32 里非常常用的手法。
LRESULT CALLBACK TodoWindow::sInputSubclassProc(HWND hwnd, UINT message, WPARAM wParam,
                                                LPARAM lParam, UINT_PTR id,
                                                DWORD_PTR refData) {
    auto* self = reinterpret_cast<TodoWindow*>(refData);
    if (self != nullptr && message == WM_KEYDOWN) {
        if (wParam == static_cast<WPARAM>(VK_RETURN)) {
            self->addTask();
            return 0;
        }
        if (wParam == static_cast<WPARAM>(VK_ESCAPE)) {
            SetWindowTextW(hwnd, L"");
            return 0;
        }
    }
    // 吃掉回车产生的"叮"一声（系统会把它转成 WM_CHAR）
    if (message == WM_CHAR &&
        (wParam == static_cast<WPARAM>(L'\r') || wParam == static_cast<WPARAM>(L'\n'))) {
        return 0;
    }
    if (message == WM_NCDESTROY) {
        RemoveWindowSubclass(hwnd, &TodoWindow::sInputSubclassProc, id);
    }
    return DefSubclassProc(hwnd, message, wParam, lParam);
}

int TodoWindow::run() {
    ShowWindow(hwnd_, SW_SHOW);
    UpdateWindow(hwnd_);

    MSG message{};
    int exitCode = 0;
    BOOL got = 1;
    while ((got = GetMessageW(&message, nullptr, 0, 0)) > 0) {
        if (accelerator_ != nullptr &&
            TranslateAcceleratorW(hwnd_, accelerator_, &message) != 0) {
            continue;
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    if (got == 0) {
        exitCode = static_cast<int>(message.wParam);
    }
    return exitCode;
}

}  // namespace

// =============================================================================
//  入口。和 main.cpp 一样，这里只负责"把东西接起来"。
//  WIN32 子系统 + UNICODE，所以入口是 wWinMain（不会弹黑色控制台窗口）。
// =============================================================================
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    // 告诉 Windows"界面缩放我自己按 DPI 算"，否则 125% / 150% 下会被位图拉伸变糊
    SetProcessDPIAware();

    INITCOMMONCONTROLSEX controls{};
    controls.dwSize = static_cast<DWORD>(sizeof(controls));
    controls.dwICC = ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES;
    if (InitCommonControlsEx(&controls) == FALSE) {
        MessageBoxW(nullptr, L"公共控件初始化失败。", L"待办清单", MB_ICONERROR | MB_OK);
        return 1;
    }

    // 数据文件和布局都在当前工作目录，和 console 版共用同一个 tasks.txt
    const std::string path = "tasks.txt";

    // 旧版控制台把数据按 GBK 写盘，这里一次性转成 UTF-8（原文件留成 .bak）
    migrateLegacyFileIfNeeded(path);

    TodoWindow window(path);
    if (!window.create()) {
        MessageBoxW(nullptr, L"窗口创建失败。", L"待办清单", MB_ICONERROR | MB_OK);
        return 1;
    }
    return window.run();
}
