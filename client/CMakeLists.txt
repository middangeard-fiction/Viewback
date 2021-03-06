if (NOT WIN32)
	include(FindProtobuf)
	find_package(Protobuf REQUIRED)
	include_directories(${PROTOBUF_INCLUDE_DIR})

	find_package (Threads)
endif ()

include_directories (
	${PROJECT_SOURCE_DIR}
	${PROJECT_SOURCE_DIR}/../ext-deps/pthreads-w32-2-8-0-release
	${PROJECT_SOURCE_DIR}/../ext-deps/protobuf-2.5.0/vsprojects/include
)

set (CLIENT_TEST_SOURCES
	viewback_client.cpp
	viewback_data.cpp
	viewback_servers.cpp
	client_test.cpp
	../protobuf/data.pb.cc
)

add_definitions(-D_SCL_SECURE_NO_WARNINGS)
add_definitions(-D_CRT_SECURE_NO_WARNINGS)
add_definitions(-DPTW32_STATIC_LIB)
add_definitions(-DPROTOBUF_USE_EXCEPTIONS=0)

add_executable (client_test ${CLIENT_TEST_SOURCES})

if (NOT WIN32)
	target_link_libraries(client_test ${PROTOBUF_LIBRARY})
	target_link_libraries(client_test ${CMAKE_THREAD_LIBS_INIT})
endif ()

if (WIN32)
	target_link_libraries(client_test debug ${PROJECT_SOURCE_DIR}/../ext-deps/pthreads-w32-2-8-0-release-vs2013/Debug/pthread.lib)
	target_link_libraries(client_test debug ${PROJECT_SOURCE_DIR}/../ext-deps/protobuf-2.5.0-vs2013/vsprojects/Debug/libprotobuf.lib)

	target_link_libraries(client_test optimized ${PROJECT_SOURCE_DIR}/../ext-deps/pthreads-w32-2-8-0-release-vs2013/Release/pthread.lib)
	target_link_libraries(client_test optimized ${PROJECT_SOURCE_DIR}/../ext-deps/protobuf-2.5.0-vs2013/vsprojects/Release/libprotobuf.lib)
endif (WIN32)
