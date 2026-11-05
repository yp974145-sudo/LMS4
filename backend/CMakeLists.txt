cmake_minimum_required(VERSION 3.20)
project(lms_backend VERSION 0.1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(Drogon CONFIG REQUIRED)
add_executable(lms-backend
    src/main.cpp
    src/controllers/health_controller.cpp
    src/services/health_service.cpp)
target_include_directories(lms-backend PRIVATE src)
target_link_libraries(lms-backend PRIVATE Drogon::Drogon)
if(MSVC)
    target_compile_options(lms-backend PRIVATE /utf-8 /W4)
endif()
include(CTest)
if(BUILD_TESTING)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_test(NAME backend_http COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_http.py $<TARGET_FILE:lms-backend>)
endif()
