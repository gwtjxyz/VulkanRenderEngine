# To-Do list

## Current priority

- Cooler shading models (BRDF perhaps?)
- Explore various other rendering techniques
- Look into improving performance
- Better logging and performance metrics
- Skybox
- GPU Memory debug window
- Adding arbitrary lights at runtime

## Backlog

### General

- Linux support
- Quaternion rotations/splines?
- Handle errors more gracefully than throwing runtime exceptions and aborting

### Resource system
- Async resource manager
- Resource streaming
- Placeholder models (for when something fails to load)
- Resource hot reloading
- Skeletal meshes

### Rendering
- Implement culling
- Drawing scene graph objects with fewer draw calls
- Indirect/GPU-driven rendering
- Multiple VkQueues

## Completed

### General

- Shader hot reloading
- Movable camera
- flecs and fastgltf integration
- imgui integration for tracking application state (variables, FPS, etc)
- Visual representation of light locations for debugging
- HLSL support

### Resource system
- Basic OBJ and glTF asset loading
- Asset instancing

### Rendering
- Basic lighting models (Phong, Gooch)
- Bindless textures using descriptor indexing and buffer device address extensions
- Bindless transform, material and light access
- Placeholder textures
