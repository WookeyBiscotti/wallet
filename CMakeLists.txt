cmake_minimum_required(VERSION 3.16)

include(FetchContent)

project(Wallet LANGUAGES CXX)

## curl
find_package(CURL REQUIRED)
if (CURL_FOUND)
    include_directories(${CURL_INCLUDE_DIRS})
    add_definitions(-DHAVE_CURL)
endif()


set(CMAKE_CXX_STANDARD 17)

FetchContent_Declare(SQLiteCpp
    GIT_REPOSITORY https://github.com/SRombauts/SQLiteCpp.git
    GIT_TAG master
    GIT_SHALLOW ON
)

FetchContent_Declare(tgbot-cpp
    GIT_REPOSITORY https://github.com/reo7sp/tgbot-cpp.git
    GIT_TAG master
    GIT_SHALLOW ON
)

FetchContent_Declare(abseil-cpp
    GIT_REPOSITORY https://github.com/abseil/abseil-cpp.git
    GIT_TAG master
    GIT_SHALLOW ON
)

FetchContent_Declare(fmt
    GIT_REPOSITORY https://github.com/fmtlib/fmt.git
    GIT_TAG master
    GIT_SHALLOW ON
)

FetchContent_MakeAvailable(SQLiteCpp tgbot-cpp abseil-cpp fmt)

add_executable(wallet_bot main.cpp)

target_link_libraries(wallet_bot PUBLIC SQLiteCpp absl::time TgBot fmt::fmt)

file(GLOB_RECURSE MIGRATION_SOURCES "${CMAKE_CURRENT_LIST_DIR}/migration/*")

add_custom_command(
    OUTPUT "${CMAKE_BINARY_DIR}/.migration_copied"
    COMMAND ${CMAKE_COMMAND} -E rm -R "${CMAKE_BINARY_DIR}/migration/"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_LIST_DIR}/migration" "${CMAKE_BINARY_DIR}/migration"
    COMMAND ${CMAKE_COMMAND} -E touch "${CMAKE_BINARY_DIR}/.migration_copied"
    DEPENDS "${CMAKE_CURRENT_LIST_DIR}/migration/" "${MIGRATION_SOURCES}"
)

add_custom_target(copy_migration DEPENDS "${CMAKE_BINARY_DIR}/.migration_copied")
add_dependencies(wallet_bot copy_migration)
