cmake_minimum_required(VERSION 3.20)
project(JoyHub LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# CRT estático: o .exe roda em qualquer PC sem instalar o Visual C++ Redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(JOYHUB_DEFINES UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00)

# Programa principal: janela própria + ícone perto do relógio, sem console (WIN32).
add_executable(JoyHub WIN32
    src/main.cpp
    src/server.cpp
    src/vigem.cpp
    src/system.cpp
    src/icon.cpp
    src/ui.cpp
    src/window.cpp
    res/app.rc
)
target_compile_definitions(JoyHub PRIVATE ${JOYHUB_DEFINES})
target_link_libraries(JoyHub PRIVATE
    ws2_32 setupapi iphlpapi d2d1 dwrite windowscodecs dwmapi ole32 oleaut32 shell32)

# Gerador de res/app.ico a partir do desenho da marca em src/icon.cpp.
add_executable(icongen EXCLUDE_FROM_ALL tools/icongen.cpp src/icon.cpp)
target_compile_definitions(icongen PRIVATE ${JOYHUB_DEFINES})
target_link_libraries(icongen PRIVATE d2d1 windowscodecs ole32)

# O ícone embutido no .exe é regerado sempre que o desenho da marca ou a paleta mudam.
add_custom_command(OUTPUT "${CMAKE_CURRENT_SOURCE_DIR}/res/app.ico"
    COMMAND $<TARGET_FILE:icongen> "${CMAKE_CURRENT_SOURCE_DIR}/res/app.ico"
    DEPENDS icongen src/icon.cpp src/theme.h
    VERBATIM)
add_custom_target(joyhub_icon DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/res/app.ico")
add_dependencies(JoyHub joyhub_icon)
set_source_files_properties(res/app.rc PROPERTIES OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/res/app.ico")

if(MSVC)
    foreach(target JoyHub icongen)
        target_compile_options(${target} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/utf-8 /W4 /permissive->)
    endforeach()
elseif(MINGW)
    target_link_options(JoyHub PRIVATE -municode -static)
endif()
