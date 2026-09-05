#include "program/shell/shell_repl.hpp"

#include <args.hxx>

#include <folly/init/Init.h>

#include <cstdlib>
#include <iostream>
#include <string>

using namespace eugraph::shell;

static ShellConfig parseArgs(int argc, char* argv[]) {
    ShellConfig config;
    args::ArgumentParser parser("EuGraph shell.");
    args::HelpFlag help(parser, "help", "Show this help menu", {"help"});
    args::ValueFlag<std::string> host(parser, "host", "Server host (default: 127.0.0.1)", {'h', "host"}, config.host);
    args::ValueFlag<int> port(parser, "port", "Server port (default: 9090)", {'p', "port"}, config.port);

    try {
        parser.ParseCLI(argc, argv);
    } catch (const args::Help&) {
        std::cout << parser;
        std::exit(0);
    } catch (const args::ParseError& e) {
        std::cerr << e.what() << '\n';
        std::cerr << parser;
        std::exit(1);
    } catch (const args::ValidationError& e) {
        std::cerr << e.what() << '\n';
        std::cerr << parser;
        std::exit(1);
    }

    config.host = args::get(host);
    config.port = args::get(port);
    return config;
}

int main(int argc, char* argv[]) {
    auto config = parseArgs(argc, argv);
    int folly_argc = 1;
    folly::Init init(&folly_argc, &argv);
    runRepl(config);
    return 0;
}
