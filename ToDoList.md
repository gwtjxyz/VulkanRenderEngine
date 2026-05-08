# To-Do list

## Current priority

- imgui integration for tracking application state (variables, FPS, etc)
- Splitting code into different application layers
- Extracting code out of main.cpp and into separate modules

## Backlog

### General

- Linux support
- Lighting models
- Quaternion rotations/splines?
- Handle errors more gracefully than throwing runtime exceptions and aborting
- HLSL/DXC support

### Resource system
- Async resource manager
- Resource streaming
- Placeholder textures/models (for when something fails to load)
- Resource hot reloading

### Entity Component system
- Rewrite camera as an Entity with a Camera component
- EntityManager

### Rendering
- Implement culling
- 

## Completed

- Shader hot reloading
- Movable camera
- Basic asset loading
