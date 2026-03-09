CXX=g++
CXXFLAGS=-std=c++11 -Wall -Wextra
LDFLAGS=-pthread

all: server client

server: server.cpp
	$(CXX) $(CXXFLAGS) server.cpp -o server

client: client.cpp
	$(CXX) $(CXXFLAGS) client.cpp -o client $(LDFLAGS)

clean:
	rm -f server client

.PHONY: all clean