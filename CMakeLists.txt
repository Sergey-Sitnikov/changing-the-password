cmake_minimum_required(VERSION 3.5)
project(SSHPasswordChanger)

set(CMAKE_CXX_STANDARD 11)

# Находим библиотеку Qt5
find_package(Qt5 REQUIRED COMPONENTS Widgets)

# Находим библиотеку libssh
find_package(LibSSH REQUIRED)

# Создаем исполняемый файл
add_executable(SSHPasswordChanger src/main.cpp src/ssh_helper.cpp)

# Включаем автоматическую генерацию moc для Qt
set_target_properties(SSHPasswordChanger PROPERTIES AUTOMOC ON)

# Связываем необходимые библиотеки с исполняемым файлом
target_link_libraries(SSHPasswordChanger Qt5::Widgets ${LIBSSH_LIBRARIES})

# Указываем директории для включаемых файлов
include_directories(${Qt5Widgets_INCLUDE_DIRS} ${LIBSSH_INCLUDE_DIRS})
