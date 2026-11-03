// Gera o .ico do executável a partir do mesmo desenho usado na bandeja (src/icon.cpp).
// O build roda isto sozinho (alvo joyhub_icon). Uso manual: icongen res\app.ico
#include <objbase.h>

#include <cstdio>
#include <iterator>
#include <vector>

#include "../src/icon.h"
#include "../src/theme.h"

using namespace swc;

namespace {

// Os objetos COM vivem só aqui dentro: precisam ser liberados antes do CoUninitialize.
int generate(const wchar_t* path) {
    ComPtr<ID2D1Factory> d2d;
    ComPtr<IWICImagingFactory> wic;
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2d.GetAddressOf())) ||
        FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic)))) {
        return 1;
    }

    // Cada tamanho como PNG dentro do .ico (suportado desde o Windows Vista).
    const int sizes[] = {16, 20, 24, 32, 40, 48, 64, 256};
    std::vector<std::vector<uint8_t>> images;
    for (int s : sizes) {
        ComPtr<IWICBitmap> bmp;
        if (!renderIconBitmap(d2d.Get(), wic.Get(), s, theme::Cyan, &bmp)) return 1;
        images.push_back(encodePng(wic.Get(), bmp.Get()));
        if (images.back().empty()) return 1;
    }

    FILE* f = nullptr;
    if (_wfopen_s(&f, path, L"wb") != 0 || !f) return 1;
    const auto u8 = [f](uint8_t v) { std::fwrite(&v, 1, 1, f); };
    const auto u16 = [f](uint16_t v) { std::fwrite(&v, 2, 1, f); };
    const auto u32 = [f](uint32_t v) { std::fwrite(&v, 4, 1, f); };
    const uint16_t count = uint16_t(std::size(sizes));
    u16(0);  // reservado
    u16(1);  // tipo: ícone
    u16(count);
    uint32_t offset = 6 + 16u * count;
    for (size_t i = 0; i < std::size(sizes); ++i) {
        const uint8_t dim = sizes[i] >= 256 ? 0 : uint8_t(sizes[i]);
        u8(dim);
        u8(dim);
        u8(0);   // paleta
        u8(0);   // reservado
        u16(1);  // planos
        u16(32); // bits por pixel
        u32(uint32_t(images[i].size()));
        u32(offset);
        offset += uint32_t(images[i].size());
    }
    for (const auto& img : images) std::fwrite(img.data(), 1, img.size(), f);
    return std::fclose(f) == 0 ? 0 : 1;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) {
        std::fwprintf(stderr, L"uso: icongen saida.ico\n");
        return 2;
    }
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const int code = generate(argv[1]);
    CoUninitialize();
    std::printf(code == 0 ? "icone gerado\n" : "falha ao gerar o icone\n");
    return code;
}
