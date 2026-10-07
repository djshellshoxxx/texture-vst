# Cross-compiles the Windows x64 build from Linux with clang-cl and lld-link.
#
# JUCE 8 rejects MinGW, so this targets the MSVC ABI the same way Chromium does:
# the Windows SDK (NuGet: Microsoft.Windows.SDK.CPP[.x64]), the VC runtime
# libraries (vcruntime/libcmt), and LLVM's libc++ as the C++ standard library,
# all with the static runtime (/MT) so nothing needs a redistributable.
#
#   -DTEXTURE_WIN_SYSROOT=<dir>   vc/{include,lib/x64}, sdk/include/{ucrt,um,shared,winrt},
#                                 sdk/lib/{ucrt,um}/x64 (lower-case symlinks added)
#   -DTEXTURE_WIN_LIBCXX=<dir>    an installed libc++ (include/c++/v1, lib/libc++.lib);
#                                 leave empty while building libc++ itself
#
# A native MSVC build remains the reference; this exists for machines without one.
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_VERSION 10.0)
set(CMAKE_SYSTEM_PROCESSOR AMD64)

set(TEXTURE_WIN_SYSROOT "$ENV{TEXTURE_WIN_SYSROOT}" CACHE PATH "Windows SDK + VC runtime sysroot")
set(TEXTURE_WIN_LIBCXX  "$ENV{TEXTURE_WIN_LIBCXX}"  CACHE PATH "libc++ built for x86_64-pc-windows-msvc")
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES TEXTURE_WIN_SYSROOT TEXTURE_WIN_LIBCXX)

find_program(CMAKE_C_COMPILER   NAMES clang-cl clang-cl-18 PATHS /usr/lib/llvm-18/bin REQUIRED)
set(CMAKE_CXX_COMPILER ${CMAKE_C_COMPILER})
find_program(CMAKE_LINKER NAMES lld-link   PATHS /usr/lib/llvm-18/bin REQUIRED)
find_program(CMAKE_AR     NAMES llvm-lib   PATHS /usr/lib/llvm-18/bin REQUIRED)
find_program(CMAKE_RC_COMPILER NAMES llvm-rc PATHS /usr/lib/llvm-18/bin REQUIRED)
find_program(CMAKE_MT     NAMES llvm-mt    PATHS /usr/lib/llvm-18/bin REQUIRED)

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded" CACHE STRING "" FORCE)

set(_sr "${TEXTURE_WIN_SYSROOT}")
set(_inc "")
if(TEXTURE_WIN_LIBCXX)
    # -isystem, not /imsvc: libc++ must come before clang's own builtin headers.
    string(APPEND _inc " /clang:-isystem\"${TEXTURE_WIN_LIBCXX}/include/c++/v1\"")
endif()
foreach(_dir vc/include sdk/include/ucrt sdk/include/um sdk/include/shared sdk/include/winrt)
    string(APPEND _inc " /imsvc \"${_sr}/${_dir}\"")
endforeach()

# - aligned allocation is off: the VC 14.11 runtime predates aligned operator new.
# - _CRT_STDIO_ISO_WIDE_SPECIFIERS must match libc++, which is built with it.
# - malloc.h is forced in because JUCE relies on MSVC's STL pulling in alloca.
set(_common "--target=x86_64-pc-windows-msvc -fms-compatibility-version=19.29 /clang:-fno-aligned-allocation /D_CRT_DECLARE_NONSTDC_NAMES=1 /D_CRT_STDIO_ISO_WIDE_SPECIFIERS /FImalloc.h -Wno-unused-command-line-argument${_inc}")
set(CMAKE_C_FLAGS_INIT   "${_common}")
set(CMAKE_CXX_FLAGS_INIT "${_common} /EHsc")
set(CMAKE_RC_FLAGS_INIT  "-I \"${_sr}/sdk/include/um\" -I \"${_sr}/sdk/include/shared\"")

set(_libpaths "/libpath:\"${_sr}/vc/lib/x64\" /libpath:\"${_sr}/sdk/lib/ucrt/x64\" /libpath:\"${_sr}/sdk/lib/um/x64\"")
if(TEXTURE_WIN_LIBCXX)
    string(APPEND _libpaths " /libpath:\"${TEXTURE_WIN_LIBCXX}/lib\"")
    # CMake's usual Win32 set, plus libc++ and libcpmt (for exception_ptr support).
    set(CMAKE_CXX_STANDARD_LIBRARIES
        "libc++.lib libcpmt.lib kernel32.lib user32.lib gdi32.lib winspool.lib shell32.lib ole32.lib oleaut32.lib uuid.lib comdlg32.lib advapi32.lib"
        CACHE STRING "" FORCE)
endif()
foreach(_kind EXE SHARED MODULE)
    set(CMAKE_${_kind}_LINKER_FLAGS_INIT "${_libpaths}")
endforeach()

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
