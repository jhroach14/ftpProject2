cmake_minimum_required(VERSION 3.3)
project(ftpProject2)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")

set(SERVER_SOURCE myFtp.cpp)
add_executable(Server ${SERVER_SOURCE})

set(CLIENT_SOURCE myFtpServer.cpp)
add_executable(Client ${CLIENT_SOURCE})

