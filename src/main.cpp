#include <cstdlib>

#ifndef DISABLE_IMPORT_STD
import std;
#else
#include <iostream>
#include <stdexcept>
#endif

import render_engine;

int main() {
    RenderEngine app;

    try {
        app.run();
    } catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
