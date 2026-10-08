# To-Do list

## Current priority

- Cooler shading models (BRDF perhaps?)
- Explore various other rendering techniques
- Look into improving performance
- Better logging and tracking what took how much time
- Skybox

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
- Placeholder models (for when something fails to load)
- Resource hot reloading

### Rendering
- Implement culling

## Completed

- Shader hot reloading
- Movable camera
- Basic asset loading
- flecs and fastgltf integration
- glTF loading
- imgui integration for tracking application state (variables, FPS, etc)
- Extracting code out of main.cpp and into separate modules
- Bindless textures using descriptor indexing and buffer device address extensions
- Bindless transform, material and light access
- Placeholder textures