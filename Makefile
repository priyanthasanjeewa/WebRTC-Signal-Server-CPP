
signalling_server : src/main.cpp src/server.h
	g++ -std=c++11 src/main.cpp thirdparty/json11/json11.cpp -Ithirdparty -lpthread -o signalling_server
