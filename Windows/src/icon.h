// Marca JoyHub (controle com J + conexões), a mesma do Android, desenhada com Direct2D.
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d2d1.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <cstdint>
#include <string>
#include <vector>

namespace swc {

using Microsoft::WRL::ComPtr;

constexpr D2D1_COLOR_F rgb(uint32_t hex, float alpha = 1.0f) {
    return {float((hex >> 16) & 0xFF) / 255.0f, float((hex >> 8) & 0xFF) / 255.0f, float(hex & 0xFF) / 255.0f, alpha};
}

// Ladrilho escuro com o controle na cor `accent` (a da marca ou a do estado da bandeja).
// detailed=false desenha só o controle, maior: legível em 16-24 px.
void drawAppIcon(ID2D1RenderTarget* rt, ID2D1Factory* factory, ID2D1SolidColorBrush* brush, D2D1_RECT_F box,
                 D2D1_COLOR_F accent, bool detailed);

// Renderiza o ícone num bitmap 32bpp PBGRA de sizePx x sizePx.
bool renderIconBitmap(ID2D1Factory* factory, IWICImagingFactory* wic, int sizePx, D2D1_COLOR_F accent,
                      ComPtr<IWICBitmap>* out);

// HICON com alfa (bandeja do sistema).
HICON createIcon(ID2D1Factory* factory, IWICImagingFactory* wic, int sizePx, D2D1_COLOR_F accent);

std::vector<uint8_t> encodePng(IWICImagingFactory* wic, IWICBitmap* bitmap);
bool savePng(IWICImagingFactory* wic, IWICBitmap* bitmap, const std::wstring& path);

}  // namespace swc
