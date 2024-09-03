cmake_minimum_required(VERSION 3.11)
project(S_P)

set(CMAKE_CXX_STANDARD 17)

find_library(SSH_LIB ssh)
if(NOT SSH_LIB)
    message(FATAL_ERROR "libssh not found")
endif()
add_executable(SP src/main.cpp src/SSHClient.cpp)
#target_link_libraries(web ${X11_LIBRARIES})

target_link_libraries(SP  ${SSH_LIB})