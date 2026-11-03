// Cliente mínimo do driver ViGEmBus, apenas para controles Xbox 360 (XUSB).
// Fala direto com o driver via DeviceIoControl; as definições de IOCTL e structs
// espelham include/ViGEm/km/BusShared.h do ViGEmClient (MIT, Nefarius Software Solutions).
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <thread>

#include "protocol.h"

namespace swc {

class VigemBus {
public:
    VigemBus() = default;
    ~VigemBus() { close(); }
    VigemBus(const VigemBus&) = delete;
    VigemBus& operator=(const VigemBus&) = delete;

    // Abre o barramento e confere a versão do driver. Retorna false se o
    // ViGEmBus não estiver instalado (lastError() traz o detalhe).
    bool open();
    void close();
    bool isOpen() const { return handle_ != INVALID_HANDLE_VALUE; }
    HANDLE handle() const { return handle_; }
    const char* lastError() const { return lastError_; }

private:
    HANDLE handle_ = INVALID_HANDLE_VALUE;
    const char* lastError_ = "";
};

// Um controle Xbox 360 virtual. submit() deve ser chamado sempre da mesma thread.
class X360Pad {
public:
    // largeMotor, smallMotor (0-255), ledNumber (slot XInput atribuído pelo Windows)
    using FeedbackFn = std::function<void(uint8_t, uint8_t, uint8_t)>;

    X360Pad() = default;
    ~X360Pad() { unplug(); }
    X360Pad(const X360Pad&) = delete;
    X360Pad& operator=(const X360Pad&) = delete;

    // Conecta o controle virtual e espera o Windows deixá-lo pronto.
    bool plug(VigemBus& bus, FeedbackFn onFeedback);
    void unplug();
    bool isPlugged() const { return serial_ != 0; }

    // Envia o estado para o driver. Leva microssegundos; não aloca nada.
    bool submit(const PadState& state);

    // Slot XInput (0-3) ou -1 se ainda não atribuído.
    int userIndex();

private:
    void notificationLoop();

    HANDLE bus_ = INVALID_HANDLE_VALUE;
    ULONG serial_ = 0;
    HANDLE submitEvent_ = nullptr;
    OVERLAPPED notifyOverlapped_{};
    std::thread notifyThread_;
    std::atomic<bool> stopping_{false};
    FeedbackFn onFeedback_;
};

}  // namespace swc
