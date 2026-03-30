CXX = g++
CXXFLAGS = -Wall -Wextra -Wpedantic -std=c++17 -pthread

all: server client

server: server.cpp
	$(CXX) $(CXXFLAGS) server.cpp -o server

client: client.cpp
	$(CXX) $(CXXFLAGS) client.cpp -o client

clean:
	rm -f server client