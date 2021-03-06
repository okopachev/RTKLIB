cmake_minimum_required(VERSION 3.0.0)

project(rtklib)
set(PROJECT_VERSION 2.4.2)
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


set(CMAKE_INSTALL_PREFIX /usr)
set(BUILD_SYSTEM_COMMON_PATH "${PROJECT_BINARY_DIR}/.cmake")
set(RELEASE_MAIL_TO "mail@nicetu.spb.ru")
add_definitions(-DDLL)
add_definitions(-DENAGLO)

if(WIN32)
  add_definitions(-DDLL)
endif(WIN32)

file(GLOB SOURCE_LIB src/*.c)
file(GLOB SUB_SOURCE_LIB src/rcv/*.c)

file(GLOB ALL_HEADERS src/*.h)
file(GLOB SUB_ALL_HEADERS  src/rcv/*.h)

include_directories(src)
include_directories(src/rcv)

add_library(${PROJECT_SHARED_LIBRARY} SHARED ${ALL_HEADERS} ${SUB_ALL_HEADERS} ${SOURCE_LIB} ${SUB_SOURCE_LIB})
add_library(${PROJECT_STATIC_LIBRARY} STATIC ${ALL_HEADERS} ${SUB_ALL_HEADERS} ${SOURCE_LIB} ${SUB_SOURCE_LIB})

if(WIN32)
  target_link_libraries(${PROJECT_SHARED_LIBRARY} wsock32 ws2_32 winmm)
  target_link_libraries(${PROJECT_STATIC_LIBRARY} wsock32 ws2_32 winmm)
endif(WIN32)

add_subdirectory(app/rnx2rtkp)

install(TARGETS ${PROJECT_SHARED_LIBRARY} DESTINATION lib EXPORT ${PROJECT_NAME}_targets)
install(TARGETS ${PROJECT_STATIC_LIBRARY} DESTINATION lib EXPORT ${PROJECT_NAME}_targets)
install(EXPORT ${PROJECT_NAME}_targets DESTINATION targets)
install(FILES ${ALL_HEADERS} DESTINATION include)

### FOR PACKAGING #####################################################################################

function(define_rpm_package_arch ARCH_)
  set(CXX_COMPILER_ARGUMENT "-dumpmachine")
  execute_process(COMMAND ${CMAKE_CXX_COMPILER} ${CXX_COMPILER_ARGUMENT} OUTPUT_VARIABLE CXX_COMPILER_TARGET)
  string(REGEX REPLACE "-.*" "" CXX_COMPILER_TARGET ${CXX_COMPILER_TARGET})

  if(CXX_COMPILER_TARGET STREQUAL "")
    message(FATAL_ERROR "Target build architecture cannot be defined")
  else()
    set(${ARCH_} ${CXX_COMPILER_TARGET} PARENT_SCOPE)
  endif()
  
endfunction()

if(WIN32)
  set(CPACK_GENERATOR "NSIS")
else(WIN32)
  set(CPACK_GENERATOR "RPM")
endif(WIN32)  

set(CPACK_PACKAGE_NAME ${PROJECT_NAME})
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_VENDOR "REC-SPb-ETU")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "An Open Source Program Package for GNSS Positioning")
set(CPACK_PACKAGE_DESCRIPTION_FILE "${CMAKE_CURRENT_SOURCE_DIR}/readme.txt")

set(CPACK_PACKAGING_INSTALL_PREFIX ${CMAKE_INSTALL_PREFIX})

set(CPACK_RPM_POST_INSTALL_SCRIPT_FILE "${CMAKE_CURRENT_SOURCE_DIR}/install/post_install_script.sh")
set(CPACK_RPM_POST_UNINSTALL_SCRIPT_FILE "${CMAKE_CURRENT_SOURCE_DIR}/install/post_uninstall_script.sh")

set(CPACK_RPM_PACKAGE_RELEASE 14)
set(CPACK_RPM_PACKAGE_GROUP "System Environment/Libraries")
set(CPACK_RPM_PACKAGE_LICENSE "BSD 2-clause")
set(CPACK_RPM_EXCLUDE_FROM_AUTO_FILELIST_ADDITION ${CPACK_PACKAGING_INSTALL_PREFIX})

define_rpm_package_arch(CPACK_RPM_PACKAGE_ARCHITECTURE)

if(WIN32)
 set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}-${CPACK_RPM_PACKAGE_RELEASE}")
else(WIN32)
 set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}-${CPACK_RPM_PACKAGE_RELEASE}.${CPACK_RPM_PACKAGE_ARCHITECTURE}")
endif(WIN32)

set(CPACK_NSIS_PACKAGE_NAME "${CPACK_PACKAGE_NAME}")
set(CPACK_NSIS_DISPLAY_NAME "${CPACK_PACKAGE_NAME}")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "RTKLIB")
set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)

include(CPack)
