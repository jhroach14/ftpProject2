CC = g++
DEBUG = -g
CFLAGS = -Wall -std=gnu++11 $(DEBUG)
LFLAGS = -Wall $(DEBUG)

all: clean myftp myFtpServer

myftp: myFtp.cpp
	$(CC) $(CFLAGS) -o myftp myFtp.cpp

myFtpServer: myFtpServer.cpp
	$(CC) $(CFLAGS) -o myFtpServer myFtpServer.cpp

run:
	./myftp.out

clean:
	rm -rf myftp
	rm -rf myFtpServer