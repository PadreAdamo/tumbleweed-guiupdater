#include <iostream>

int main(int argc, char *argv[])
{
    if (argc > 1 && std::string(argv[1]) == "status") {
        std::cout << "Tumbleweed Updater status: OK\n";
        return 0;
    }

    std::cout << "Usage: twu-ctl status\n";
    return 1;
}
