module;

#ifdef DISABLE_IMPORT_STD
#include <cstdint>
#include <iostream>
#include <fstream>
#include <filesystem>
#include <random>
#include <stack>
#endif

#include <cstdint>

export module maze_builder;

import platform;

#ifndef DISABLE_IMPORT_STD
import std;
#endif

export enum class MazeCellType : uint32_t {
    Wall = 0,
    Corridor
};

export struct MazeCell {
    MazeCellType type = MazeCellType::Wall;
};

export struct WalledMaze {
    std::vector<MazeCell> grid;
    uint32_t entrance;
    uint32_t exit;
};

export class MazeBuilder {
    using grid_index = uint32_t;

public:
    explicit MazeBuilder(const uint32_t width = 5, const uint32_t height = 5) : m_Width(width), m_Height(height) {
        std::random_device seeder;
        m_Rng.seed(seeder());
    }

    void setDimensions(const uint32_t width, const uint32_t height) {
        m_Width = width;
        m_Height = height;
    }

    // [0,0] is top left corner
    void generateMaze() {
        m_Grid.clear();
        m_Walls.clear();

        // Start with a grid full of walls
        const uint32_t totalCellCount = m_Width * m_Height;
        for (uint32_t i = 0; i < totalCellCount; ++i) {
            m_Grid.emplace_back(MazeCellType::Wall);
        }

        // Pick a random cell
        std::uniform_int_distribution<uint32_t> xPicker(0, m_Width - 1);
        std::uniform_int_distribution<uint32_t> yPicker(0, m_Height - 1);
        const uint32_t firstX = xPicker(m_Rng), firstY = yPicker(m_Rng);
        // Mark it as part of the maze
        m_Grid.at(gridIndex(firstX, firstY)).type = MazeCellType::Corridor;

        // Add walls of the cell to the wall list
        addNearbyWalls(firstX, firstY);

        std::uniform_int_distribution<uint32_t> wallPicker(0, m_Width * m_Height);
        while (!m_Walls.empty()) {
            // Pick random wall from the list
            const grid_index cellGridIndex = m_Walls.at(wallPicker(m_Rng) % m_Walls.size());

            // Check how many nearby cells are corridors
            const uint32_t x = cellGridIndex % m_Width;
            const uint32_t y = cellGridIndex / m_Width;
            const uint32_t visitedNeighbors = countVisitedNeighbors(x, y);

            // If only one nearby cell is a corridor, make this wall into a corridor as well
            if (visitedNeighbors == 1) {
                m_Grid.at(cellGridIndex).type = MazeCellType::Corridor;
                addNearbyWalls(x, y);
            }

            // Remove the wall from the list
            for (auto it = m_Walls.begin(); it != m_Walls.end();) {
                if (*it == cellGridIndex) {
                    // not breaking after this because a wall could be added multiple times
                    it = m_Walls.erase(it);
                } else {
                    ++it;
                }
            }
        }
    }

    // This also adds walls to the printout (admittedly in a slightly awkward manner)
    void printMazeToFile() const {
        std::ofstream mazeFile { pathFromAssetDir("maze.txt"), std::ios_base::out | std::ios_base::trunc };

        mazeFile << "█";
        for (uint32_t i = 0; i < m_Width; ++i) {
            mazeFile << "█";
        }
        mazeFile << "█\n";

        uint32_t currentX = 0;
        for (const auto & c : m_Grid) {
            if (currentX == 0)
                mazeFile << "█";

            if (c.type == MazeCellType::Wall) {
                mazeFile << "█";
            } else {
                mazeFile << "░";
            }

            currentX++;
            if (currentX == m_Width) {
                mazeFile << "█\n";
                currentX = 0;
            }
        }

        mazeFile << "█";
        for (uint32_t i = 0; i < m_Width; ++i) {
            mazeFile << "█";
        }
        mazeFile << "█";
    }

    void printMazeToConsole() const {
        uint32_t currentX = 0;
        for (const auto & c : m_Grid) {
            if (c.type == MazeCellType::Wall) {
                std::cout << "█";
            } else {
                std::cout << "░";
            }
            currentX++;
            if (currentX == m_Width) {
                std::cout << '\n';
                currentX = 0;
            }
        }

        std::cout << std::endl;
    }

    WalledMaze toWalledMaze() const {
        WalledMaze maze {};
        std::vector<MazeCell> grid {};

        uint32_t widthWithWall = m_Width + 2;

        for (uint32_t i = 0; i < widthWithWall; ++i) {
            grid.emplace_back(MazeCellType::Wall);
        }

        uint32_t currentX = 0;
        for (const auto & c : m_Grid) {
            if (currentX == 0) {
                grid.emplace_back(MazeCellType::Wall);
            }

            grid.emplace_back(c.type);

            currentX++;
            if (currentX == m_Width) {
                grid.emplace_back(MazeCellType::Wall);
                currentX = 0;
            }
        }

        for (uint32_t i = 0; i < widthWithWall; ++i) {
            grid.emplace_back(MazeCellType::Wall);
        }

        // Add entrance and exit
        uint32_t heightWithWall = m_Height + 2;
        for (uint32_t i = 1; i < heightWithWall; ++i) {
            uint32_t outerWall = i * widthWithWall;
            uint32_t outerWallNeighbor = outerWall + 1;
            if (grid.at(outerWallNeighbor).type == MazeCellType::Corridor) {
                grid.at(outerWall).type = MazeCellType::Corridor;
                maze.entrance = outerWall;
                break;
            }
        }

        for (uint32_t i = heightWithWall - 1; i > 1; --i) {
            uint32_t outerWall = i * widthWithWall + widthWithWall - 1;
            uint32_t outerWallNeighbor = outerWall - 1;
            if (grid.at(outerWallNeighbor).type == MazeCellType::Corridor) {
                grid.at(outerWall).type = MazeCellType::Corridor;
                maze.exit = outerWall;
                break;
            }
        }

        maze.grid = std::move(grid);
        return maze;
    }

private:
    [[nodiscard]]
    uint32_t gridIndex(const uint32_t x, const uint32_t y) const {
        return x + m_Width * y;
    }

    void addNearbyWalls(const uint32_t x, const uint32_t y) {
        // Up
        if (y > 0 && m_Grid.at(gridIndex(x, y - 1)).type == MazeCellType::Wall) {
            m_Walls.emplace_back(gridIndex(x, y - 1));
        }
        // Down
        if (y < m_Height - 1 && m_Grid.at(gridIndex(x, y + 1)).type == MazeCellType::Wall) {
            m_Walls.emplace_back(gridIndex(x, y + 1));
        }
        // Left
        if (x > 0 && m_Grid.at(gridIndex(x - 1, y)).type == MazeCellType::Wall) {
            m_Walls.emplace_back(gridIndex(x - 1, y));
        }
        // Right
        if (x < m_Width - 1 && m_Grid.at(gridIndex(x + 1, y)).type == MazeCellType::Wall) {
            m_Walls.emplace_back(gridIndex(x + 1, y));
        }
    }

    [[nodiscard]]
    uint32_t countVisitedNeighbors(const uint32_t x, const uint32_t y) const {
        uint32_t visitedCount = 0;

        // Up
        if (y > 0 && m_Grid.at(gridIndex(x, y - 1)).type == MazeCellType::Corridor) {
            visitedCount++;
        }
        // Down
        if (y < m_Height - 1 && m_Grid.at(gridIndex(x, y + 1)).type == MazeCellType::Corridor) {
            visitedCount++;
        }
        // Left
        if (x > 0 && m_Grid.at(gridIndex(x - 1, y)).type == MazeCellType::Corridor) {
            visitedCount++;
        }
        // Right
        if (x < m_Width - 1 && m_Grid.at(gridIndex(x + 1, y)).type == MazeCellType::Corridor) {
            visitedCount++;
        }

        return visitedCount;
    }

    uint32_t m_Width;
    uint32_t m_Height;

    std::mt19937 m_Rng;

    // Data structure to hold maze info
    std::vector<MazeCell> m_Grid {};
    std::vector<grid_index> m_Walls {};
};
