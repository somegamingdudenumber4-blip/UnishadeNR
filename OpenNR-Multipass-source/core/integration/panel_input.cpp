// A host-only input adapter. No Roblox hooks or graphics pipeline changes.
#include <windows.h>
#include <commctrl.h>
#include <reshade.hpp>
#include <cstdio>
#include <cwchar>

namespace {
constexpr UINT_PTR kSubclass = 0x4F4E5250;
constexpr int kHotkey = 0x6A11;
HWND overlay = nullptr, game = nullptr;
bool open = false, pinned = false, registered = false;
LONG_PTR editStyle = 0;
FILE *logFile = nullptr;

void Log(const char *text) {
    if (logFile) { std::fprintf(logFile, "%s\n", text); std::fflush(logFile); }
}

BOOL CALLBACK FindRoblox(HWND window, LPARAM arg) {
    if (!IsWindowVisible(window) || IsIconic(window) || GetWindow(window, GW_OWNER)) return TRUE;
    DWORD pid = 0; GetWindowThreadProcessId(window, &pid);
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) return TRUE;
    wchar_t path[32768]; DWORD size = 32768;
    const BOOL got = QueryFullProcessImageNameW(process, 0, path, &size);
    CloseHandle(process);
    if (!got) return TRUE;
    const wchar_t *name = std::wcsrchr(path, L'\\');
    if (_wcsicmp(name ? name + 1 : path, L"RobloxPlayerBeta.exe") != 0) return TRUE;
    auto *found = reinterpret_cast<HWND *>(arg);
    if (*found) { *found = nullptr; return FALSE; } // Ambiguous: do not focus another game.
    *found = window;
    return TRUE;
}

void TogglePin() {
    if (!open || !overlay) { Log("Open the ReShade/OpenNR panel first, then press Ctrl+F11."); return; }
    if (pinned) {
        pinned = false;
        SetWindowLongPtrW(overlay, GWL_EXSTYLE, editStyle);
        SetWindowPos(overlay, nullptr, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        SetForegroundWindow(overlay);
        Log("EDIT: panel receives input again.");
        return;
    }
    game = nullptr; EnumWindows(FindRoblox, reinterpret_cast<LPARAM>(&game));
    if (!game) { Log("Cannot pin: expected one visible Roblox game window."); return; }
    editStyle = GetWindowLongPtrW(overlay, GWL_EXSTYLE);
    pinned = true;
    SetWindowLongPtrW(overlay, GWL_EXSTYLE,
        editStyle | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE);
    SetWindowPos(overlay, nullptr, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    if (!SetForegroundWindow(game) || GetForegroundWindow() != game) {
        pinned = false;
        SetWindowLongPtrW(overlay, GWL_EXSTYLE, editStyle);
        SetWindowPos(overlay, nullptr, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        Log("Could not return focus to Roblox; edit mode retained.");
        return;
    }
    Log("PLAY: OpenNR panel stays visible; Roblox owns keyboard/mouse focus. Ctrl+F11 returns to editing.");
}

LRESULT CALLBACK Proc(HWND window, UINT message, WPARAM wp, LPARAM lp,
                      UINT_PTR, DWORD_PTR) {
    if (message == WM_HOTKEY && wp == kHotkey) { TogglePin(); return 0; }
    if (message == WM_ACTIVATE && LOWORD(wp) == WA_INACTIVE && pinned) {
        if (reinterpret_cast<HWND>(lp) == game) return 0;
        pinned = false;
        Log("Panel play mode ended on switching to another application.");
        // The host closes its menu and restores its normal pass-through styles.
    }
    if (message == WM_NCDESTROY) {
        if (registered) UnregisterHotKey(window, kHotkey);
        RemoveWindowSubclass(window, Proc, kSubclass);
        overlay = nullptr; game = nullptr; open = pinned = registered = false;
    }
    return DefSubclassProc(window, message, wp, lp);
}

void Init(reshade::api::effect_runtime *runtime) {
    if (overlay) return;
    const HWND window = static_cast<HWND>(runtime->get_hwnd());
    if (!window || !SetWindowSubclass(window, Proc, kSubclass, 0)) {
        Log("Could not attach host input adapter."); return;
    }
    overlay = window;
    Log("Host input adapter attached. Open ReShade/OpenNR, then Ctrl+F11 to play with the panel visible.");
}

bool OnOpen(reshade::api::effect_runtime *runtime, bool visible, reshade::api::input_source) {
    if (runtime->get_hwnd() != overlay) return false;
    open = visible;
    if (visible && !registered) {
        registered = RegisterHotKey(overlay, kHotkey, MOD_CONTROL | MOD_NOREPEAT, VK_F11) != FALSE;
        Log(registered ? "Ctrl+F11 registered for the open panel." : "Ctrl+F11 is already in use; pinning unavailable.");
    }
    if (!visible) {
        if (registered) UnregisterHotKey(overlay, kHotkey);
        registered = false; pinned = false;
    }
    return false;
}

void Destroy(reshade::api::effect_runtime *runtime) {
    if (runtime->get_hwnd() != overlay) return;
    if (registered) UnregisterHotKey(overlay, kHotkey);
    RemoveWindowSubclass(overlay, Proc, kSubclass);
    overlay = nullptr; game = nullptr; open = pinned = registered = false;
}
}

extern "C" __declspec(dllexport) const char *NAME = "OpenNR Panel Input";
extern "C" __declspec(dllexport) const char *DESCRIPTION =
    "Ctrl+F11: keep the ReShade/OpenNR panel visible while Roblox receives gameplay input. Press again to edit.";

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID reserved) {
    if (reason == DLL_PROCESS_ATTACH) {
        wchar_t path[32768]; GetModuleFileNameW(nullptr, path, 32768);
        const wchar_t *leaf = std::wcsrchr(path, L'\\');
        if (!leaf || (_wcsicmp(leaf + 1, L"Unishade.exe") != 0 && _wcsicmp(leaf + 1, L"UnishadeNR.exe") != 0)) return FALSE;
        if (!reshade::register_addon(module)) return FALSE;
        wchar_t *last = std::wcsrchr(path, L'\\');
        if (last) { *(last + 1) = 0; wcscat_s(path, L"panel-input.log"); logFile = _wfopen(path, L"w"); }
        reshade::register_event<reshade::addon_event::init_effect_runtime>(Init);
        reshade::register_event<reshade::addon_event::destroy_effect_runtime>(Destroy);
        reshade::register_event<reshade::addon_event::reshade_open_overlay>(OnOpen);
        Log("Panel input adapter loaded (host only).");
    } else if (reason == DLL_PROCESS_DETACH) {
        if (!reserved && overlay) {
            if (registered) UnregisterHotKey(overlay, kHotkey);
            RemoveWindowSubclass(overlay, Proc, kSubclass);
            if (pinned) {
                SetWindowLongPtrW(overlay, GWL_EXSTYLE, editStyle);
                SetForegroundWindow(overlay);
            }
        }
        reshade::unregister_addon(module);
        if (logFile) std::fclose(logFile);
    }
    return TRUE;
}
