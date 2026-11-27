# CMakeLists.txt - TWIST (Linux, macOS, Windows/MSVC)
#
# Linux/macOS/MSYS2: the Makefile is the quickest path ("make test").
# Visual Studio 2022: "Open Folder", or
#   cmake -B build -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%/scripts/buildsystems/vcpkg.cmake
#   cmake --build build --config Release
# (FFTW3 from vcpkg: "vcpkg install fftw3" - see docs/WINDOWS.md)

cmake_minimum_required(VERSION 3.15)
project(twist C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# 64-bit file positions on 32-bit POSIX systems (I/Q files over 2 GiB);
# Windows uses _fseeki64/_ftelli64 instead - see core/twfile.h
if(NOT WIN32)
    add_compile_definitions(_FILE_OFFSET_BITS=64)
endif()

# The common signal processing (no protocol), the protocols' own code,
# and the pipeline built once per library (core/twpipe.h, TW_WITH_*):
# each protocol library has its own code and the common part compiled in,
# none needs another (core/twltypes.h).
set(TW_COMMON  core/twbb.c core/twsync.c core/twscan.c core/wavread.c core/iqread.c core/twcall.c)
set(TW_CLASSIC core/twcenc.c core/twcdec.c core/twcdeep.c)
set(TW_TWIST   core/twist.c core/ldpc.c)
set(TW_PI4     core/pi4pipe.c core/pi4sync.c core/pi4demod.c core/pi4dec.c core/pi4snr.c
               core/pi4mettab.c core/pi4resample.c core/pi4enc.c core/fano.c core/tab.c)
set(TW_PIPE    core/twpipe.c core/twdec.c)

find_package(FFTW3 CONFIG QUIET)
if(NOT FFTW3_FOUND)
    find_library(FFTW3_LIBRARY NAMES fftw3 libfftw3-3 libfftw3)
    find_library(FFTW3_THREADS_LIBRARY NAMES fftw3_threads libfftw3_threads-3 libfftw3_threads)
    find_path(FFTW3_INCLUDE_DIR NAMES fftw3.h)
    if(NOT FFTW3_LIBRARY OR NOT FFTW3_INCLUDE_DIR)
        message(FATAL_ERROR
            "FFTW3 not found. See docs/WINDOWS.md (Windows) or install "
            "libfftw3-dev (Linux) / fftw (macOS/Homebrew). A manual install "
            "can be given with -DFFTW3_LIBRARY=... -DFFTW3_INCLUDE_DIR=...")
    endif()
endif()

# Live audio (optional): PortAudio, e.g. "vcpkg install portaudio"
find_path(PORTAUDIO_INCLUDE_DIR NAMES portaudio.h)
find_library(PORTAUDIO_LIBRARY NAMES portaudio portaudio_x64 libportaudio)
if(PORTAUDIO_INCLUDE_DIR AND PORTAUDIO_LIBRARY)
    message(STATUS "Live audio: PortAudio ${PORTAUDIO_LIBRARY}")
else()
    message(STATUS "Live audio: PortAudio not found, twistlive will only replay files")
endif()

# FFTW (with its threads part: the planner made thread-safe, core/twbb.c),
# the maths, the threads
function(tw_link t)
    target_include_directories(${t} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/core)
    set(threads OFF)
    if(FFTW3_FOUND)
        if(TARGET FFTW3::fftw3_threads)
            target_link_libraries(${t} PRIVATE FFTW3::fftw3_threads)
            set(threads ON)
        endif()
        target_link_libraries(${t} PRIVATE FFTW3::fftw3)
    else()
        target_include_directories(${t} PRIVATE ${FFTW3_INCLUDE_DIR})
        if(FFTW3_THREADS_LIBRARY)
            target_link_libraries(${t} PRIVATE ${FFTW3_THREADS_LIBRARY})
            set(threads ON)
        endif()
        target_link_libraries(${t} PRIVATE ${FFTW3_LIBRARY})
    endif()
    if(NOT threads)
        # without libfftw3_threads, each library's planner lock alone: do
        # not decode with two libraries in two threads at once
        target_compile_definitions(${t} PRIVATE TW_NO_FFTW_THREADS)
    endif()
    if(NOT WIN32)
        set(THREADS_PREFER_PTHREAD_FLAG ON)
        find_package(Threads REQUIRED)
        target_link_libraries(${t} PRIVATE m Threads::Threads)
    endif()
    set_target_properties(${t} PROPERTIES C_VISIBILITY_PRESET hidden)
    if(MSVC)
        target_compile_options(${t} PRIVATE /W4)
        target_compile_definitions(${t} PRIVATE _CRT_SECURE_NO_WARNINGS)
    else()
        target_compile_options(${t} PRIVATE -Wall -Wextra)
    endif()
endfunction()

# A library: libtwcore, libtwist, libopera, libpi4 (.dll on Windows too:
# twist.dll would clash with the application's Twist.dll)
function(tw_library t)
    add_library(${t} SHARED ${ARGN})
    tw_link(${t})
    target_compile_definitions(${t} PRIVATE TWL_BUILD)
    set_target_properties(${t} PROPERTIES PREFIX "lib" IMPORT_PREFIX "lib")
endfunction()

tw_library(twcore core/twcore.c core/twtx.c core/twaudio.c core/twscan.c core/twbb.c)
if(PORTAUDIO_INCLUDE_DIR AND PORTAUDIO_LIBRARY)
    target_compile_definitions(twcore PRIVATE TWL_HAVE_PORTAUDIO)
    target_include_directories(twcore PRIVATE ${PORTAUDIO_INCLUDE_DIR})
    target_link_libraries(twcore PRIVATE ${PORTAUDIO_LIBRARY})
endif()
tw_library(twist core/libtwist.c ${TW_PIPE} ${TW_TWIST} ${TW_COMMON})
target_compile_definitions(twist PRIVATE TW_WITH_TWIST=1)
tw_library(opera core/twcl.c ${TW_PIPE} ${TW_CLASSIC} ${TW_COMMON})
target_compile_definitions(opera PRIVATE TW_WITH_CLASSIC=1)
tw_library(pi4 core/libpi4.c ${TW_PIPE} ${TW_PI4} ${TW_COMMON})
target_compile_definitions(pi4 PRIVATE TW_WITH_PI4=1)
set_target_properties(twcore PROPERTIES PUBLIC_HEADER "core/twltypes.h;core/twcore.h")
set_target_properties(twist PROPERTIES PUBLIC_HEADER core/libtwist.h)
set_target_properties(opera PROPERTIES PUBLIC_HEADER core/twcl.h)
set_target_properties(pi4 PROPERTIES PUBLIC_HEADER core/libpi4.h)

# The decoders: twistd and operad from one source (cli/twistd.c, built
# with its protocol), pi4d its own
add_executable(twistd cli/twistd.c ${TW_PIPE} ${TW_TWIST} ${TW_COMMON})
tw_link(twistd)
target_compile_definitions(twistd PRIVATE TW_WITH_TWIST=1)
add_executable(operad cli/twistd.c ${TW_PIPE} ${TW_CLASSIC} ${TW_COMMON})
tw_link(operad)
target_compile_definitions(operad PRIVATE TW_WITH_CLASSIC=1)
add_executable(pi4d cli/pi4d.c core/pi4iqscan.c core/pi4iqmix.c ${TW_PI4} ${TW_COMMON})
tw_link(pi4d)

# twistenc: the encoders and the transmitter; twistlive: through the libraries
add_executable(twistenc cli/twistenc.c core/twtx.c core/twcall.c ${TW_TWIST}
               core/pi4enc.c core/fano.c core/tab.c)
tw_link(twistenc)
add_executable(twistlive cli/twistlive.c core/wavread.c)
tw_link(twistlive)
target_link_libraries(twistlive PRIVATE twcore twist opera pi4)

install(TARGETS twistd operad pi4d twcore twist opera pi4
        RUNTIME DESTINATION bin LIBRARY DESTINATION lib ARCHIVE DESTINATION lib
        PUBLIC_HEADER DESTINATION include)
