cmake_minimum_required(VERSION 3.15)
project(mpsc-ring CXX)

if (NOT TARGET rusty-cpp)
	find_package(rusty-cpp CONFIG REQUIRED)
endif()

add_library(${PROJECT_NAME} INTERFACE)
target_include_directories(${PROJECT_NAME} INTERFACE include)

file(GLOB_RECURSE HEADERS "include/*")
set_target_properties(${PROJECT_NAME} PROPERTIES PUBLIC_HEADER "${HEADERS}")
target_link_libraries(${PROJECT_NAME}
	INTERFACE
		rusty-cpp
)

install(TARGETS ${PROJECT_NAME} DESTINATION "include/")
