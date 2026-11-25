#include <cstdlib>

#ifndef DISABLE_IMPORT_STD
import std;
#else
#include <iostream>
#include <stdexcept>
#endif

import render_engine;

int main(int argc, char * argv[]) {
    RenderEngine app;

    try {
        int mazeWidth = 0, mazeHeight = 0;
        if (argc == 3) {
            std::stringstream ss;
            ss << argv[1];
            ss >> mazeWidth;
            ss.clear();
            ss << argv[2];
            ss >> mazeHeight;
        }

        app.run("Vulkan 1.4 Engine", mazeWidth, mazeHeight);
    } catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
