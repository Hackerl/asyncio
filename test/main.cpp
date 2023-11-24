#include <catch2/catch_session.hpp>
#include <winsock2.h>

int main(const int argc, char *argv[]) {
    WSADATA wsaData;

    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
        return -1;

    const int result = Catch::Session().run(argc, argv);

    WSACleanup();

    return result;
}
