cmake_minimum_required(VERSION 3.10)
project(warum C)
set(CMAKE_C_STANDARD 99)

include(CMakeDependentOption)
option(DBUS_ENABLE "Enable D-Bus support" ON)
option(SYSTEMD "Include systemd unit" ON)
CMAKE_DEPENDENT_OPTION(WARUMTRAY "Build Qt5 systray icon frontend" OFF "DBUS_ENABLE" OFF)

set(WARNINGS "-Wall -Wextra")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${WARNINGS}")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${WARNINGS}")
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    set(OPTS "-march=native -O2 -fstack-protector-strong -fno-plt")
    if(CMAKE_C_COMPILER_ID MATCHES "Clang")
        set(OPTS "${OPTS} -Wno-unused-function")
    endif()
    string(REPLACE "-O3" "" CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE}")
    string(REPLACE "-O3" "" CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE}")
    set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} ${OPTS}")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} ${OPTS}")
    set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} -s")
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(GLIB glib-2.0 REQUIRED)
pkg_check_modules(NF libnetfilter_queue REQUIRED)

if(DBUS_ENABLE)
    pkg_check_modules(GOBJECT gobject-2.0 REQUIRED)
    pkg_check_modules(GIO gio-unix-2.0 REQUIRED)
    find_program(GDBUS_CODEGEN gdbus-codegen)
    if (NOT GDBUS_CODEGEN)
        message(FATAL_ERROR "gdbus-codegen not found")
    endif(NOT GDBUS_CODEGEN)

    set(WC_DBUS_GEN_CODE pk.qwerty12.warum.h pk.qwerty12.warum.c)
    set(INTROSPECTION_XML ${CMAKE_SOURCE_DIR}/pk.qwerty12.warum.xml)
    add_custom_command(OUTPUT ${WC_DBUS_GEN_CODE}
                       COMMAND ${GDBUS_CODEGEN} --c-namespace Bus --interface-prefix pk.qwerty12. --generate-c-code pk.qwerty12.warum ${INTROSPECTION_XML}
                       DEPENDS ${INTROSPECTION_XML})
    if (WARUMTRAY)
        add_subdirectory(warumtray)
    endif()
else()
    pkg_check_modules(GIO gio-2.0 REQUIRED)
endif()

configure_file(config.h.in config.h)

add_executable(${PROJECT_NAME} main.c ${WC_DBUS_GEN_CODE})
target_include_directories(${PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}" ${NF_INCLUDE_DIRS} ${GLIB_INCLUDE_DIRS} ${GOBJECT_INCLUDE_DIRS} ${GIO_INCLUDE_DIRS})
target_link_libraries(${PROJECT_NAME} ${NF_LIBRARIES} ${GLIB_LIBRARIES} ${GOBJECT_LIBARIES} ${GIO_LIBRARIES})

include(GNUInstallDirs)
if(SYSTEMD)
    if(DBUS_ENABLE)
        set(SYSTEMD_DBUS_ARGS "--dbus -d")
        set(SYSTEMD_DBUS_SERVICE "dbus.service")
    endif()
    configure_file(etc/warum.service.in etc/warum.service @ONLY)
    install(FILES ${CMAKE_BINARY_DIR}/etc/warum.service
            DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/systemd/system/
            PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ)
endif()

install(TARGETS ${PROJECT_NAME}
        DESTINATION ${CMAKE_INSTALL_SBINDIR}/
        PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
install(FILES etc/iptables_rules.sh
        DESTINATION ${CMAKE_INSTALL_LIBEXECDIR}/${PROJECT_NAME}/
        PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
if(DBUS_ENABLE)
    install(FILES etc/pk.qwerty12.warum.conf
            DESTINATION ${CMAKE_INSTALL_FULL_SYSCONFDIR}/dbus-1/system.d/
            PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ)
endif()

# uninstall target
if(NOT TARGET uninstall)
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake_uninstall.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake"
        IMMEDIATE @ONLY)

    add_custom_target(uninstall
        COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake)
endif()
