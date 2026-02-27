cmake_minimum_required(VERSION 3.5)
project(SSHPasswordChanger)

set(CMAKE_CXX_STANDARD 17)

# --- Настройки для статической сборки под Windows ---
if(WIN32)
    if(MSVC)
        # Используем статический runtime библиотеки C (/MT для MSVC)
        set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")
    elseif(MINGW)
        # Для MXE (MinGW) добавляем флаг полной статической линковки
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -static")
    endif()
endif()

# --- Поиск Qt ---
find_package(Qt6 REQUIRED COMPONENTS Widgets)

# --- Поиск LibSSH ---
# Пытаемся найти через PkgConfig (Linux/MSYS2)
if(UNIX)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBSSH REQUIRED IMPORTED_TARGET libssh)
else()
    # Для Windows ищем вручную (LIBSSH_INCLUDE_DIR и LIBSSH_LIBRARY)
    find_path(LIBSSH_INCLUDE_DIR
        NAMES libssh/libssh.h
        PATHS
            ${LIBSSH_ROOT}/include
            ${CMAKE_PREFIX_PATH}/include
            "$ENV{PROGRAMFILES}/libssh/include"
    )

    find_library(LIBSSH_LIBRARY
        NAMES ssh libssh
        PATHS
            ${LIBSSH_ROOT}/lib
            ${CMAKE_PREFIX_PATH}/lib
            "$ENV{PROGRAMFILES}/libssh/lib"
    )

    if(LIBSSH_INCLUDE_DIR AND LIBSSH_LIBRARY)
        set(LIBSSH_INCLUDE_DIRS ${LIBSSH_INCLUDE_DIR})
        set(LIBSSH_LIBRARIES ${LIBSSH_LIBRARY})
        message(STATUS "Found LibSSH: ${LIBSSH_LIBRARY}")
    else()
        message(FATAL_ERROR "LibSSH not found. Please install libssh or set LIBSSH_ROOT.")
    endif()
endif()

# Находим библиотеку потоков
find_package(Threads REQUIRED)

# Находим Zlib и OpenSSL (зависимости LibSSH)
find_package(ZLIB REQUIRED)
find_package(OpenSSL REQUIRED)

# --- Поиск LibGcrypt и GpgError (зависимости LibSSH) ---
find_library(GCRYPT_LIBRARY
    NAMES gcrypt libgcrypt
    PATHS
        ${LIBSSH_ROOT}/lib
        ${CMAKE_PREFIX_PATH}/lib
        "$ENV{PROGRAMFILES}/libgcrypt/lib"
)

find_library(GPG_ERROR_LIBRARY
    NAMES gpg-error libgpg-error
    PATHS
        ${LIBSSH_ROOT}/lib
        ${CMAKE_PREFIX_PATH}/lib
        "$ENV{PROGRAMFILES}/gpg-error/lib"
)

if(GCRYPT_LIBRARY AND GPG_ERROR_LIBRARY)
    message(STATUS "Found LibGcrypt: ${GCRYPT_LIBRARY}")
else()
    message(FATAL_ERROR "LibGcrypt or GpgError not found. Please install libgcrypt.")
endif()

add_executable(SSHPasswordChanger
    src/main.cpp
    src/SSHClient.cpp
    src/ThreadPool.h
)

set_target_properties(SSHPasswordChanger PROPERTIES AUTOMOC ON)

# --- Исправление ошибок линковки LibSSH ---
# Указываем, что используется статическая LibSSH
if(WIN32)
    target_compile_definitions(SSHPasswordChanger PRIVATE LIBSSH_STATIC)
endif()

if(WIN32 AND MSVC)
    # Явное отключение динамической линковки CRT для MSVC
    set_property(TARGET SSHPasswordChanger APPEND PROPERTY LINK_FLAGS "/NODEFAULTLIB:msvcrt.lib")
endif()

target_link_libraries(SSHPasswordChanger
    Qt6::Widgets
    Threads::Threads
)

# --- Линкуем LibSSH и её зависимости ---
if(TARGET PkgConfig::LIBSSH)
    # Если найден через PkgConfig (обычная Linux сборка)
    target_link_libraries(SSHPasswordChanger PkgConfig::LIBSSH)
elseif(LIBSSH_LIBRARY)
    # Если найден вручную (Windows/MXE сборка)
    # Добавлены iphlpapi и ws2_32 для сетевых функций (if_nametoindex)
    target_link_libraries(SSHPasswordChanger 
        ${LIBSSH_LIBRARY}
        ${GCRYPT_LIBRARY}
        ${GPG_ERROR_LIBRARY}
        ZLIB::ZLIB
        OpenSSL::Crypto
        OpenSSL::SSL
        iphlpapi
        ws2_32
    )
endif()

# Оставляем только include для LibSSH
include_directories(${LIBSSH_INCLUDE_DIRS})