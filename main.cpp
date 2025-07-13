#include "server.hpp"
#include <pangomm/init.h>

int main(int argc, char** argv) {
    Pango::init();

    const auto root = std::filesystem::path(argv[0]).parent_path();

    Server server(root);

    return 0;
}
