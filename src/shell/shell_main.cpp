#include "shell/shell_repl.hpp"

#include <folly/init/Init.h>

#include <cstdlib>
#include <iostream>
#include <string>

using namespace eugraph::shell;

static ShellConfig parseArgs(int argc, char* argv[]) {
    ShellConfig config;
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if ((arg == "--host" || arg == "-h") && i + 1 < argc) {
            config.host = argv[++i];
        } else if ((arg == "--port" || arg == "-p") && i + 1 < argc) {
            config.port = std::atoi(argv[++i]);
        } else if (arg == "--help") {
            std::cout << "Usage: eugraph-shell [options]\n"
                      << "Options:\n"
                      << "  --host, -h <host>      Server host (default: 127.0.0.1)\n"
                      << "  --port, -p <port>      Server port (default: 9090)\n"
                      << "  --help                 Show this help\n";
            std::exit(0);
        }
    }
    return config;
}

int main(int argc, char* argv[]) {
    auto config = parseArgs(argc, argv);
    int folly_argc = 1;
    folly::Init init(&folly_argc, &argv);
    runRepl(config);
    return 0;
}
