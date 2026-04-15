cmake_minimum_required(VERSION 3.16)
project(ninjamsrv LANGUAGES C CXX)

add_executable(ninjamsrv
    ninjamsrv.cpp
    usercon.cpp
    ../mpb.cpp
    ../netmsg.cpp
    ../../WDL/jnetlib/asyncdns.cpp
    ../../WDL/jnetlib/connection.cpp
    ../../WDL/jnetlib/listen.cpp
    ../../WDL/jnetlib/util.cpp
    ../../WDL/rng.cpp
    ../../WDL/sha.cpp
)

target_include_directories(ninjamsrv PRIVATE
    ../../WDL
    ../../WDL/jnetlib
)

if (MSVC)
    target_compile_definitions(ninjamsrv PRIVATE WIN32 _WINDOWS)
endif()

target_link_libraries(ninjamsrv PRIVATE
    wsock32
    ws2_32
    advapi32
)
