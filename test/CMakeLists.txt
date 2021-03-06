# Copyright Takatoshi Kondo 2015
#
# Distributed under the Boost Software License, Version 1.0.
# (See accompanying file LICENSE_1_0.txt or copy at
# http://www.boost.org/LICENSE_1_0.txt)

CMAKE_MINIMUM_REQUIRED (VERSION 2.8.6)

PROJECT (mqtt_client_cpp_test)

LIST (APPEND check_PROGRAMS
     test_main.cpp
     connect.cpp
     pubsub.cpp
     async_pubsub.cpp
     pubsub_no_strand.cpp
     remaining_length.cpp
     multi_sub.cpp
     resend.cpp
     manual_publish.cpp
     retain.cpp
     will.cpp
)

ADD_EXECUTABLE (${PROJECT_NAME} ${check_PROGRAMS})

LIST (APPEND MQTT_LINK_LIBRARIES
    ${Boost_TEST_EXEC_MONITOR_LIBRARY}
    ${Boost_SYSTEM_LIBRARY}
    ${CMAKE_THREAD_LIBS_INIT}
)
IF (NOT MQTT_NO_TLS)
    LIST (APPEND MQTT_LINK_LIBRARIES
        ${OPENSSL_LIBRARIES}
    )
ENDIF ()

LINK_DIRECTORIES(${Boost_LIBRARY_DIRS})
TARGET_LINK_LIBRARIES (${PROJECT_NAME}
    ${MQTT_LINK_LIBRARIES}
)

ADD_TEST (${PROJECT_NAME} ${PROJECT_NAME})

IF ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang" OR "${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
    SET_PROPERTY (TARGET ${PROJECT_NAME} APPEND_STRING PROPERTY COMPILE_FLAGS "-std=c++14 -Wall -Wextra -pthread -g -O0")
ENDIF ()

FILE(COPY ${CMAKE_CURRENT_SOURCE_DIR}/../mosquitto.org.crt DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
