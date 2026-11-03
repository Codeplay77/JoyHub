#include "vigem.h"

#include <setupapi.h>
#include <winioctl.h>

#include <vector>

namespace swc {
namespace {

// {96E42B22-F5E9-42F8-B043-ED0F932F014F} = GUID_DEVINTERFACE_BUSENUM_VIGEM
const GUID kBusInterfaceGuid = {0x96E42B22, 0xF5E9, 0x42F8, {0xB0, 0x43, 0xED, 0x0F, 0x93, 0x2F, 0x01, 0x4F}};

constexpr ULONG kCommonVersion = 0x0001;  // VIGEM_COMMON_VERSION
constexpr DWORD kIoctlBase = 0x801;

constexpr DWORD busIoctlW(DWORD index) {
    return CTL_CODE(FILE_DEVICE_BUS_EXTENDER, index, METHOD_BUFFERED, FILE_WRITE_DATA);
}
constexpr DWORD busIoctlRW(DWORD index) {
    return CTL_CODE(FILE_DEVICE_BUS_EXTENDER, index, METHOD_BUFFERED, FILE_WRITE_DATA | FILE_READ_DATA);
}

constexpr DWORD kIoctlPluginTarget = busIoctlW(kIoctlBase + 0x000);
constexpr DWORD kIoctlUnplugTarget = busIoctlW(kIoctlBase + 0x001);
constexpr DWORD kIoctlCheckVersion = busIoctlW(kIoctlBase + 0x002);
constexpr DWORD kIoctlWaitDeviceReady = busIoctlW(kIoctlBase + 0x003);
constexpr DWORD kIoctlXusbRequestNotification = busIoctlRW(kIoctlBase + 0x200);
constexpr DWORD kIoctlXusbSubmitReport = busIoctlW(kIoctlBase + 0x201);
constexpr DWORD kIoctlXusbGetUserIndex = busIoctlRW(kIoctlBase + 0x206);

constexpr int kTargetXbox360Wired = 0;  // VIGEM_TARGET_TYPE::Xbox360Wired
constexpr USHORT kXbox360Vid = 0x045E;
constexpr USHORT kXbox360Pid = 0x028E;
constexpr ULONG kMaxSerial = 64;

// Empacotamento padrão (igual ao do driver): o driver valida o campo Size.
struct PluginTarget {
    ULONG Size;
    ULONG SerialNo;
    int TargetType;
    USHORT VendorId;
    USHORT ProductId;
};
struct SerialRequest {  // VIGEM_UNPLUG_TARGET e VIGEM_WAIT_DEVICE_READY
    ULONG Size;
    ULONG SerialNo;
};
struct CheckVersion {
    ULONG Size;
    ULONG Version;
};
struct XusbNotification {
    ULONG Size;
    ULONG SerialNo;
    UCHAR LargeMotor;
    UCHAR SmallMotor;
    UCHAR LedNumber;
};
struct XusbSubmitReport {
    ULONG Size;
    ULONG SerialNo;
    PadState Report;
};
struct XusbGetUserIndex {
    ULONG Size;
    ULONG SerialNo;
    ULONG UserIndex;
};
static_assert(sizeof(PluginTarget) == 16, "layout de VIGEM_PLUGIN_TARGET");
static_assert(sizeof(SerialRequest) == 8, "layout de VIGEM_UNPLUG_TARGET");
static_assert(sizeof(XusbNotification) == 12, "layout de XUSB_REQUEST_NOTIFICATION");
static_assert(sizeof(XusbSubmitReport) == 20, "layout de XUSB_SUBMIT_REPORT");
static_assert(sizeof(XusbGetUserIndex) == 12, "layout de XUSB_GET_USER_INDEX");

// IOCTL em handle sobreposto: espera a conclusão e devolve o erro Win32, se houver.
bool runIoctl(HANDLE h, DWORD code, void* in, DWORD inSize, void* out, DWORD outSize, OVERLAPPED& ov,
              DWORD* error = nullptr) {
    DWORD transferred = 0;
    ResetEvent(ov.hEvent);
    if (DeviceIoControl(h, code, in, inSize, out, outSize, &transferred, &ov)) return true;

    DWORD err = GetLastError();
    if (err == ERROR_IO_PENDING) {
        if (GetOverlappedResult(h, &ov, &transferred, TRUE)) return true;
        err = GetLastError();
    }
    if (error) *error = err;
    return false;
}

struct ScopedEvent {
    OVERLAPPED ov{};
    ScopedEvent() { ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr); }
    ~ScopedEvent() {
        if (ov.hEvent) CloseHandle(ov.hEvent);
    }
};

}  // namespace

bool VigemBus::open() {
    if (isOpen()) return true;
    lastError_ = "driver ViGEmBus não encontrado (não está instalado?)";

    HDEVINFO set = SetupDiGetClassDevsW(&kBusInterfaceGuid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (set == INVALID_HANDLE_VALUE) return false;

    SP_DEVICE_INTERFACE_DATA ifData{};
    ifData.cbSize = sizeof(ifData);
    for (DWORD i = 0; SetupDiEnumDeviceInterfaces(set, nullptr, &kBusInterfaceGuid, i, &ifData); ++i) {
        DWORD required = 0;
        SetupDiGetDeviceInterfaceDetailW(set, &ifData, nullptr, 0, &required, nullptr);
        if (required == 0) continue;

        std::vector<DWORD> storage((required + sizeof(DWORD) - 1) / sizeof(DWORD));
        auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(storage.data());
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        if (!SetupDiGetDeviceInterfaceDetailW(set, &ifData, detail, required, &required, nullptr)) continue;

        HANDLE h = CreateFileW(detail->DevicePath, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                               nullptr, OPEN_EXISTING,
                               FILE_ATTRIBUTE_NORMAL | FILE_FLAG_NO_BUFFERING | FILE_FLAG_WRITE_THROUGH |
                                   FILE_FLAG_OVERLAPPED,
                               nullptr);
        if (h == INVALID_HANDLE_VALUE) {
            lastError_ = "sem acesso ao driver ViGEmBus";
            continue;
        }

        ScopedEvent ev;
        CheckVersion version{sizeof(CheckVersion), kCommonVersion};
        if (runIoctl(h, kIoctlCheckVersion, &version, sizeof(version), nullptr, 0, ev.ov)) {
            handle_ = h;
            break;
        }
        lastError_ = "versão do ViGEmBus incompatível";
        CloseHandle(h);
    }

    SetupDiDestroyDeviceInfoList(set);
    return isOpen();
}

void VigemBus::close() {
    if (handle_ != INVALID_HANDLE_VALUE) {
        CloseHandle(handle_);
        handle_ = INVALID_HANDLE_VALUE;
    }
}

bool X360Pad::plug(VigemBus& bus, FeedbackFn onFeedback) {
    if (!bus.isOpen() || serial_ != 0) return false;
    bus_ = bus.handle();

    ScopedEvent ev;
    if (!ev.ov.hEvent) return false;

    // Mesmo procedimento do ViGEmClient: procura um serial livre, conecta e espera
    // o dispositivo ficar operacional (IOCTL inexistente antes da v1.17 = sucesso).
    ULONG plugged = 0;
    for (ULONG serial = 1; serial <= kMaxSerial; ++serial) {
        PluginTarget plugin{sizeof(PluginTarget), serial, kTargetXbox360Wired, kXbox360Vid, kXbox360Pid};
        if (!runIoctl(bus_, kIoctlPluginTarget, &plugin, sizeof(plugin), nullptr, 0, ev.ov)) continue;

        SerialRequest ready{sizeof(SerialRequest), serial};
        DWORD err = 0;
        if (runIoctl(bus_, kIoctlWaitDeviceReady, &ready, sizeof(ready), nullptr, 0, ev.ov, &err) ||
            err == ERROR_INVALID_PARAMETER) {
            plugged = serial;
        } else {
            SerialRequest unplugReq{sizeof(SerialRequest), serial};
            runIoctl(bus_, kIoctlUnplugTarget, &unplugReq, sizeof(unplugReq), nullptr, 0, ev.ov);
        }
        break;
    }
    if (plugged == 0) return false;

    serial_ = plugged;
    submitEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    onFeedback_ = std::move(onFeedback);
    stopping_ = false;
    notifyOverlapped_ = {};
    notifyOverlapped_.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    notifyThread_ = std::thread(&X360Pad::notificationLoop, this);
    return true;
}

void X360Pad::unplug() {
    if (serial_ == 0) return;
    stopping_ = true;

    {
        ScopedEvent ev;
        SerialRequest req{sizeof(SerialRequest), serial_};
        runIoctl(bus_, kIoctlUnplugTarget, &req, sizeof(req), nullptr, 0, ev.ov);
    }

    // A thread de notificação fica presa num IOCTL pendente; cancela até ela sair.
    if (notifyThread_.joinable()) {
        HANDLE thread = notifyThread_.native_handle();
        do {
            CancelIoEx(bus_, &notifyOverlapped_);
        } while (WaitForSingleObject(thread, 50) == WAIT_TIMEOUT);
        notifyThread_.join();
    }

    CloseHandle(notifyOverlapped_.hEvent);
    notifyOverlapped_.hEvent = nullptr;
    CloseHandle(submitEvent_);
    submitEvent_ = nullptr;
    onFeedback_ = nullptr;
    serial_ = 0;
}

bool X360Pad::submit(const PadState& state) {
    if (serial_ == 0) return false;
    XusbSubmitReport report{sizeof(XusbSubmitReport), serial_, state};
    OVERLAPPED ov{};
    ov.hEvent = submitEvent_;
    return runIoctl(bus_, kIoctlXusbSubmitReport, &report, sizeof(report), nullptr, 0, ov);
}

int X360Pad::userIndex() {
    if (serial_ == 0) return -1;
    ScopedEvent ev;
    XusbGetUserIndex req{sizeof(XusbGetUserIndex), serial_, 0};
    if (!runIoctl(bus_, kIoctlXusbGetUserIndex, &req, sizeof(req), &req, sizeof(req), ev.ov)) return -1;
    return int(req.UserIndex);
}

void X360Pad::notificationLoop() {
    // Vibração e LED chegam quando o jogo escreve no controle virtual.
    while (!stopping_) {
        XusbNotification n{sizeof(XusbNotification), serial_, 0, 0, 0};
        DWORD err = 0;
        if (runIoctl(bus_, kIoctlXusbRequestNotification, &n, sizeof(n), &n, sizeof(n), notifyOverlapped_, &err)) {
            if (!stopping_ && onFeedback_) onFeedback_(n.LargeMotor, n.SmallMotor, n.LedNumber);
            continue;
        }
        if (stopping_ || err == ERROR_ACCESS_DENIED || err == ERROR_OPERATION_ABORTED) break;
        Sleep(20);  // erro inesperado: evita laço ocupado
    }
}

}  // namespace swc
