#include "server.h"

int main(int argc, char *argv[]) {
    if (argc < 2) {
        std::cout << "Usage: " << argv[0] << " <PORT>" << std::endl;
        return -1;
    }

    signalling_server s;
    s.start(std::stoi(argv[1]));
    return 0;
}
