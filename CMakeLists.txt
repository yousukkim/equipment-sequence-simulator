cmake_minimum_required(VERSION 3.20)
project(equipment_sequence_simulator LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    # /utf-8 : 소스와 실행 문자셋을 UTF-8로 고정 (주석과 메시지가 한글이다)
    # /Zc:__cplusplus : __cplusplus가 실제 표준 버전을 보고하게 한다
    add_compile_options(/W4 /permissive- /utf-8 /Zc:__cplusplus)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# 외부 의존성은 단일 헤더뿐이다 (nlohmann/json, doctest).
add_library(ess_third_party INTERFACE)
target_include_directories(ess_third_party INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/third_party)

add_library(ess_core STATIC
    src/model/types.cpp
    src/model/violation.cpp
    src/io/scenario_loader.cpp
    src/validate/schedule_view.cpp
    src/validate/rules.cpp
    src/validate/validator.cpp
)
target_include_directories(ess_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(ess_core PUBLIC ess_third_party)

add_executable(ess src/main.cpp)
target_link_libraries(ess PRIVATE ess_core)

enable_testing()

add_executable(ess_tests
    tests/test_main.cpp
    tests/test_loader.cpp
    tests/test_validator.cpp
)
target_link_libraries(ess_tests PRIVATE ess_core)

add_test(NAME ess_tests COMMAND ess_tests)
