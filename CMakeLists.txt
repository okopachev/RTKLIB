cmake_minimum_required(VERSION 3.0.0)

project(rtklib)
set(PROJECT_VERSION 1.0.0)
set(PROJECT_SHARED_LIBRARY ${PROJECT_NAME})
set(PROJECT_STATIC_LIBRARY ${PROJECT_NAME}_static)

set(ARCHS i586-vniins-linux-4.1.3
          i686-redhat-linux-4.8.3
          x86_64-redhat-linux-4.8.3
          i586-vniins-linux-4.1.2
          i486-pc-nto-qnx6.5.0-4.4.2
          x86_64-vniins-linux-4.1.2
          x86_64-vniins-linux6E-4.4.7
          i686-w64-mingw32-4.9.1)


set(BUILD_SYSTEM_COMMON_PATH "${PROJECT_BINARY_DIR}/.cmake")
set(RELEASE_MAIL_TO "mail@nicetu.spb.ru")
add_definitions(-DDLL)
add_definitions(-DTRACE)
add_definitions(-DENAGLO)

file(GLOB SOURCE_LIB src/*.c)
file(GLOB SUB_SOURCE_LIB src/rcv/*.c)

file(GLOB ALL_HEADERS src/rcv/*.h)
file(GLOB SUB_ALL_HEADERS src/*.h)

include_directories(src)
include_directories(src/rcv)

add_library(${PROJECT_SHARED_LIBRARY} SHARED ${ALL_HEADERS} ${SUB_ALL_HEADERS} ${SOURCE_LIB} ${SUB_SOURCE_LIB})
add_library(${PROJECT_STATIC_LIBRARY} STATIC ${ALL_HEADERS} ${SUB_ALL_HEADERS} ${SOURCE_LIB} ${SUB_SOURCE_LIB})

if(WIN32)
  add_library(${PROJECT_SHARED_LIBRARY} wsock32 ws2_32 winmm)
  add_library(${PROJECT_STATIC_LIBRARY} wsock32 ws2_32 winmm)
endif()

install(TARGETS ${PROJECT_SHARED_LIBRARY} DESTINATION lib EXPORT ${PROJECT_NAME}_targets)
install(TARGETS ${PROJECT_STATIC_LIBRARY} DESTINATION lib EXPORT ${PROJECT_NAME}_targets)
install(EXPORT ${PROJECT_NAME}_targets DESTINATION targets)
install(FILES ${ALL_HEADERS} DESTINATION inc/${PROJECT_NAME})

### FOR PACKAGING #####################################################################################
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_NAME ${PROJECT_NAME})

include(CPack)
