#include "ircp/app.h"
#include "ircp/logger.h"

#include <exception>
#include <iostream>

int main() {
    try {
        ircp::App app(ircp::Config::fromEnvironment());
        return app.run();
    } catch (const std::exception& ex) {
        ircp::Logger::error(ex.what());
        std::cerr << "irc-parser: " << ex.what() << '\n';
        return 1;
    }
}
