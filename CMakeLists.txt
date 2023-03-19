cmake_minimum_required(VERSION 3.20)

set(CMAKE_C_COMPILER /usr/bin/gcc-9)
set(CMAKE_CXX_COMPILER /usr/bin/g++-9)

project(sylar)

set(CMAKE_CXX_FLAGS
        "$ENV{CXXFLAGS} \
        -g \
        -rdynamic \
        -O0 \
        -fPIC \
        -ggdb \
        -std=c++11 \
        -Wall \
        -Wno-deprecated \
        -Werror \
        -Wno-unused-function \
        -Wno-builtin-macro-redefined \
        -Wno-deprecated-declarations")

include_directories(sylar/include)

aux_source_directory(sylar/src LIB_SRC)

set(LIBRARY_OUTPUT_PATH ${CMAKE_CURRENT_SOURCE_DIR}/lib)

link_directories(/usr/local/lib)

add_library(sylar SHARED ${LIB_SRC})

target_link_libraries(${PROJECT_NAME}
                                    yaml-cpp
                                    pthread
                                    dl
                                    )

# 开启调试模式
set(CMAKE_BUILD_TYPE Debug)

# 配置生成 compile_commands.json 文件
set(CMAKE_EXPORT_COMPILE_COMMANDS on)

SET(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin)

set(LIBS
        sylar
        pthread
        yaml-cpp
        )

include_directories(tests/include)

add_executable(test_main tests/test_main.cc)
target_link_libraries(test_main ${LIBS})