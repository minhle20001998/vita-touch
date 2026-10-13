cmake_minimum_required(VERSION 3.10)
project(VitaTouchPad C CXX)

# Official vitasdk toolchain: pass via -DCMAKE_TOOLCHAIN_FILE=$VITASDK/share/vita.toolchain.cmake
# (sets -Wl,-q --emit-relocs so the fself gets its relocation table)

set(VITA_TITLE_ID "VTPAD0001")
set(VITA_APP_TITLE "VitaTouchPad")

# The promoter rejects installs (VitaShell 0xF0030000) unless TITLE_ID is
# exactly 9 characters. Fail the configure step instead of producing a VPK
# that installs on Vita3K but not on hardware.
string(LENGTH "${VITA_TITLE_ID}" VITA_TITLE_ID_LEN)
if(NOT VITA_TITLE_ID_LEN EQUAL 9)
    message(FATAL_ERROR "VITA_TITLE_ID must be exactly 9 chars (got ${VITA_TITLE_ID_LEN}): ${VITA_TITLE_ID}")
endif()

set(SOURCES
    src/main.cpp
    src/config.cpp
    src/udp.cpp
    src/screen.cpp
    src/ime.cpp
    src/touchpad.cpp
    src/measure.cpp
    src/ui/touchpad.cpp
    src/ui/settings.cpp
)

add_definitions(-D__vita__)

# Compute the build number that version_vpk.sh will assign to this build so the
# app can display its own version on screen (matches VitaTouchPad_vN.vpk).
set(VERSION_COUNT_FILE ${CMAKE_CURRENT_BINARY_DIR}/.vpk_count)
set(NEXT_VERSION 1)
if(EXISTS ${VERSION_COUNT_FILE})
    file(READ ${VERSION_COUNT_FILE} VERSION_COUNT)
    string(STRIP ${VERSION_COUNT} VERSION_COUNT)
    math(EXPR NEXT_VERSION "${VERSION_COUNT} + 1")
endif()
add_definitions(-DVITATOUCH_VERSION="${NEXT_VERSION}")
# version_vpk.sh bumps the count file after every build, so re-read it on each
# build (not just each configure) to keep the on-screen version in sync.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${VERSION_COUNT_FILE})

add_compile_options(-O2 -Wall)

add_executable(VitaTouchPad ${SOURCES})

target_include_directories(VitaTouchPad PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui
    /usr/local/vitasdk/arm-vita-eabi/include/imgui_vita2d
)

# Landscape-only app: the SDK's stock imgui_vita2d backend is sufficient
# (vita-reader vendors the backend sources only to hack portrait projection).
target_link_libraries(VitaTouchPad
    imgui_vita2d
    vita2d
    freetype
    png16
    jpeg
    bz2
    SceDisplay_stub
    SceGxm_stub
    SceCtrl_stub
    SceTouch_stub
    SceAudio_stub
    SceNet_stub
    SceNetCtl_stub
    SceSysmodule_stub
    ScePgf_stub
    SceCommonDialog_stub
    SceAppMgr_stub
    SceAppUtil_stub
    SceRtc_stub
    SceSysmem_stub
    SceKernelThreadMgr_stub
    SceKernelModulemgr_stub
    SceIofilemgr_stub
    SceProcessmgr_stub
    SceLibKernel_stub
    SceAVConfig_stub
    pthread
    z
    m
)

# Automated cross-compilation packaging pipeline
add_custom_command(TARGET VitaTouchPad POST_BUILD
    # 1. Create the system folder automatically
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/sce_sys
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/sce_sys/livearea/contents

    # 1b. Stage the LiveArea assets (icon, background, startup, template)
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/assets/livearea/icon0.png ${CMAKE_CURRENT_BINARY_DIR}/sce_sys/icon0.png
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/assets/livearea/template.xml ${CMAKE_CURRENT_BINARY_DIR}/sce_sys/livearea/contents/template.xml
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/assets/livearea/bg.png ${CMAKE_CURRENT_BINARY_DIR}/sce_sys/livearea/contents/bg.png
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/assets/livearea/startup.png ${CMAKE_CURRENT_BINARY_DIR}/sce_sys/livearea/contents/startup.png

    # 2. Build the parameter file layout schema
    COMMAND vita-mksfoex -s TITLE_ID="${VITA_TITLE_ID}" "${VITA_APP_TITLE}" ${CMAKE_CURRENT_BINARY_DIR}/sce_sys/param.sfo

    # 3. Create the elf target format
    COMMAND vita-elf-create $<TARGET_FILE:VitaTouchPad> ${CMAKE_CURRENT_BINARY_DIR}/VitaTouchPad.elf

    # 4. Strip execution headers into eboot.bin
    COMMAND vita-make-fself ${CMAKE_CURRENT_BINARY_DIR}/VitaTouchPad.elf ${CMAKE_CURRENT_BINARY_DIR}/eboot.bin

    # 5. Pack everything securely into the target package bundle file
    COMMAND vita-pack-vpk -s ${CMAKE_CURRENT_BINARY_DIR}/sce_sys/param.sfo -b ${CMAKE_CURRENT_BINARY_DIR}/eboot.bin -a ${CMAKE_CURRENT_SOURCE_DIR}/vendor/fonts/Literata.ttf=Literata.ttf -a ${CMAKE_CURRENT_BINARY_DIR}/sce_sys/icon0.png=sce_sys/icon0.png -a ${CMAKE_CURRENT_BINARY_DIR}/sce_sys/livearea/contents/template.xml=sce_sys/livearea/contents/template.xml -a ${CMAKE_CURRENT_BINARY_DIR}/sce_sys/livearea/contents/bg.png=sce_sys/livearea/contents/bg.png -a ${CMAKE_CURRENT_BINARY_DIR}/sce_sys/livearea/contents/startup.png=sce_sys/livearea/contents/startup.png ${CMAKE_CURRENT_BINARY_DIR}/VitaTouchPad.vpk

    # 6. Make a versioned copy (build/vpk/VitaTouchPad_vN.vpk) so hardware testing can bisect builds
    COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/scripts/version_vpk.sh ${CMAKE_CURRENT_BINARY_DIR} ${CMAKE_CURRENT_BINARY_DIR}/vpk

    COMMENT "Executing automated PS Vita VPK layout packager pipeline..."
)
