# SDICL
SDICL (Simple Directmedia Integration for OpenCL, pronounced "Scycycle", yeah, yeah I know) is a minimalist wrapper for both OpenCL and SDL2 with the sole intention of merging functionality and best-case performance of both systems without limiting capability.
Perhaps the most notable feature of SDICL is its ability to run OpenCL-based shader kernels on native SDL2 Textures, allowing per-pixel functions to be run with the full power of any CPU/GPU-based OpenCL-supported device.

## Quick Start
The following code is a full C++ example of the most basic workflow:
```c++
// SDL2 setup
SDL_Init(SDL_INIT_EVERYTHING);
const int WIDTH = 800;
const int HEIGHT = 800;
SDL_Window* window = SDL_CreateWindow("SDL2", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, WIDTH, HEIGHT, SDL_WINDOW_SHOWN);
SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);

// Initialize the OCL instance (boilerplate wrapper for OpenCL setup/teardown)
OCL ocl({"cl/shaderFile.ocl"}); // Pass in a bracket-enclosed list of all shader files to initialize
std::cout << "Using: " << ocl.device.getInfo<CL_DEVICE_NAME>() << std::endl; // This line prints the name of the current device

// Create an SDL_ShaderTexture (similar to native SDL_Texture with OpenCL kernel shader support)
SDL_ShaderTexture* stexture = new SDL_ShaderTexture(renderer, &ocl, WIDTH, HEIGHT);
stexture->setBlendMode(SDL_BLENDMODE_BLEND); // We really just want an image texture, not an additive light or light mod or anything fancy
stexture->setShader("shaderFunction"); // Shader kernel function within one of the shader files initialized with the OCL instance

// SDL2 event structure and main loop
SDL_Event event;
bool running = true;
while (running) {
  while (SDL_PollEvent(&event)) {
    if (event.type == SDL_QUIT) {
        running = false;
        break;
    }
  }
  
  // Shading
  // Reset the data to all black with alpha of 0 (Does NOT use device acceleration, a better approach is to implement in your own kernel shaders)
  //stexture->blank();
  stexture->shade(); // Apply the active shader to internal data
  stexture->update(); // Apply final transition from data to renderable SDL_Texture
  
  // Drawing
  SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255); // Fill background with black
  SDL_RenderClear(renderer); // Clear background
  SDL_RenderCopy(renderer, stexture->texture, nullptr, nullptr); // Copy the shader texture over
  SDL_RenderPresent(renderer); // Update the screen
}

delete stexture; // Final cleanup and deallocation
```
### cl/shaderFile.ocl:
``` opencl
kernel void shaderFunction(global unsigned char* array, int width, int height) {
    // This is a SIMPLE example of how an OpenCL shader can act on SDL2 pixel data
    int id = get_global_id(0) * 4; // BGRA byteorder
    unsigned char bColor = array[id]; // Retrieve existing Blue color value from pixel data array
    int x = (id/4) % width; // This will depend on how the memory is laid out in the 2d array, although this is by default for SDL2. (/4 because BGRA again)
    int y = (id/4) / width; // If it's not row-major, then you'll need to flip these two statements.
    /*...*/
    array[id] = x*255.0/width; // B
    array[id + 1] = 255-(y*255.0/height); // G
    array[id + 2] = y*255.0/height; // R
    array[id + 3] = 255; // A
}
```
Remember to build with `-lSDL2main`, `-lSDL2`, and `C:/Windows/System32/OpenCL.dll`, as well as changing the `#include` lines at the top of `SDICL.h` to point to your SDL2 and OpenCL headers.

# Kernel Function Arguments
By default, SDICL calls kernel functions with the following 3 arguments, in order: `shaderFunction(global unsigned char* pixelArray, int width, int height)`, where `pixelArray` is a 1D array of all pixel bytes **IN BGRA ORDER** and width and height are the width and hight of the image in pixels.
Other arguments can be added (and default arguments overwritten) by calling `SDL_ShaderTexture::shaderKernel->setArg(int index, value)` with the desired parameter index (0, 1, and 2 used by default) and data whenever said data changes.
