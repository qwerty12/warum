project(warumtray)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt5Core REQUIRED)
find_package(Qt5DBus REQUIRED)
find_package(Qt5Widgets REQUIRED)

add_executable(${PROJECT_NAME} "warumtray.cpp" "warumbus.h" "warumbus.cpp")

target_link_libraries(${PROJECT_NAME} Qt5::Core Qt5::DBus Qt5::Widgets)

include(GNUInstallDirs)
install(TARGETS ${PROJECT_NAME}
        DESTINATION ${CMAKE_INSTALL_BINDIR}/
        PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
