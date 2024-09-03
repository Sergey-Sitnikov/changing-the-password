cmake_minimum_required(VERSION 3.5)
project(SSHPasswordChanger)

set(CMAKE_CXX_STANDARD 11)

# Находим библиотеку Qt5
find_package(Qt5 REQUIRED COMPONENTS Widgets)

# Находим библиотеку libssh с помощью PkgConfig
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBSSH REQUIRED IMPORTED_TARGET libssh)

# Создаем исполняемый файл
add_executable(SSHPasswordChanger src/main.cpp src/SSHClient.cpp)

# Включаем автоматическую генерацию moc для Qt
set_target_properties(SSHPasswordChanger PROPERTIES AUTOMOC ON)

# Связываем необходимые библиотеки с исполняемым файлом
target_link_libraries(SSHPasswordChanger Qt5::Widgets PkgConfig::LIBSSH)

# Указываем директории для включаемых файлов
include_directories(${Qt5Widgets_INCLUDE_DIRS} ${LIBSSH_INCLUDE_DIRS})
