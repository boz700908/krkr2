cmake_minimum_required(VERSION 3.19)
project(core_kag LANGUAGES CXX)

set(KAG_PATH ${CMAKE_CURRENT_LIST_DIR})
set(CORE_KAG_SOURCE_FILES
    ${KAG_PATH}/KAGParser.cpp
)

add_library(${PROJECT_NAME} STATIC ${CORE_KAG_SOURCE_FILES})
target_include_directories(${PROJECT_NAME} PUBLIC ${KAG_PATH})
target_link_libraries(${PROJECT_NAME} PUBLIC
    tjs2
    core_msg
    core_event
    core_script
)
krkr2_target_include_base_headers(${PROJECT_NAME})
target_include_directories(${PROJECT_NAME} PRIVATE
    # KAGParser.cpp 以 "utils/DebugIntf.h" 形式引用 core/utils 下的头，
    # 需要 cpp/core 本身在搜索路径里（archive/xp3 同样处理）
    ${KRKR2_CORE_DIR}
    ${KRKR2_CORE_DIR}/visual
    ${KRKR2_CORE_DIR}/visual/impl
)
