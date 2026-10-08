cmake_minimum_required(VERSION 3.16)
project(wpe-url-extractor LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Compiler optimizations for low RAM and high speed (Section 27)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(
        -Wall
        -Wextra
        -ffunction-sections
        -fdata-sections
    )
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        add_compile_options(-O3 -DNDEBUG)
    endif()
    add_link_options(-Wl,--gc-sections)
endif()

find_package(PkgConfig REQUIRED)

# Check WPE WebKit 2.0+ and WPE Platform headless APIs
pkg_check_modules(WPE REQUIRED
    wpe-webkit-2.0
    wpe-platform-headless-2.0
    wpe-platform-2.0
    wpe-1.0
    glib-2.0
    gobject-2.0
    gio-2.0
    libsoup-3.0
)

# Set RPATH so executable finds installed libraries
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)
set(CMAKE_BUILD_RPATH "/usr/local/lib/x86_64-linux-gnu;/usr/lib/x86_64-linux-gnu")

add_executable(wpe-url-extractor
    src/main.cpp
    src/browser.cpp
    src/extractor.cpp
    src/filters.cpp
)

target_include_directories(wpe-url-extractor PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${WPE_INCLUDE_DIRS}
)

target_compile_options(wpe-url-extractor PRIVATE
    ${WPE_CFLAGS_OTHER}
)

target_link_directories(wpe-url-extractor PRIVATE
    ${WPE_LIBRARY_DIRS}
    /usr/local/lib/x86_64-linux-gnu
    /usr/lib/x86_64-linux-gnu
)

target_link_libraries(wpe-url-extractor PRIVATE
    ${WPE_LIBRARIES}
    pthread
)
