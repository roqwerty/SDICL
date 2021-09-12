/*
A basic wrapper header for functions of the OpenCL library, aimed to make OpenCL integration into new projects easy
NOTE that most everything in these classes is public, as the purpose of this header is to simply abstract boilerplate code, NOT to limit functionality
*/

#include <iostream>
#include <fstream>
#include <streambuf>
#include <vector>

#include <SDL2/SDL.h>

#define CL_HPP_TARGET_OPENCL_VERSION 300
#include "CL/opencl.hpp"

cl::Device getSemiIdealDevice() {
    // Get all platforms and return "fastest" device for heavy thread use, determined by MaxClockFrequency * MaxComputeUnits
    std::vector<cl::Platform> platforms;
    cl::Platform::get(&platforms);
    std::vector <cl::Device> devices;
    cl::Device bestDevice;
    unsigned int maxClock = 0; // Stores MaxClockFrequency * MaxComputeUnits

    for (const cl::Platform& p : platforms) {
        p.getDevices(CL_DEVICE_TYPE_ALL, &devices);
        for (const cl::Device d : devices) {
            unsigned int thisClock = d.getInfo<CL_DEVICE_MAX_COMPUTE_UNITS>() * d.getInfo<CL_DEVICE_MAX_CLOCK_FREQUENCY>();
            //std::cout << d.getInfo<CL_DEVICE_NAME>() << ": " << thisClock << std::endl; // Output "scores" for all available devices
            if (thisClock > maxClock) {
                bestDevice = d;
                maxClock = thisClock;
            }
        }
    }
    return bestDevice;
}

inline std::string readFile(const std::string& filepath) {
    // Reads in entire contents of file and returns as string
    std::ifstream f(filepath);
    std::string str((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return str;
}

class OCL {
public:
    // Functions
    OCL(const std::vector<std::string>& kernelFiles); // Constructor
    ~OCL(); // Destructor
    // Variables
    cl::Device device; // CPU/GPU computing device
    cl::Program::Sources sources; // Kernel list
    cl::Context* context; // Context runtime link
    cl::CommandQueue* queue; // GPU command queue
    cl::Program* program; // Kernel program struct
private:
    // Functions
    // Variables
};

OCL::OCL(const std::vector<std::string>& kernelFiles) {
    // Sets up variables for the functioning of an OCL class
    device = getSemiIdealDevice(); // Get the "best" device for heavily-parallel computing
    context = new cl::Context({device}); // Context runtime link
    queue = new cl::CommandQueue(*context, device); // GPU command queue

    // Load all kernel files into program
    std::string code = "";
    for (const std::string& filepath : kernelFiles) {
        code = readFile(filepath);
        sources.push_back({code.c_str(), code.length()});
    }

    // Compile kernel
    program = new cl::Program(*context, sources);
    if (program->build({device}) != CL_SUCCESS) {
        std::cout << " Error building: " << program->getBuildInfo<CL_PROGRAM_BUILD_LOG>(device) << "\n";
        exit(1);
    }
}

OCL::~OCL() {
    // Memory cleanup
    delete context;
    delete queue;
    delete program;
}

// Also make a shader-based texture with SDL_TEXTUREACCESS_STREAMING and use SDL_RenderCopy(Ex) to copy from and SDL_RenderReadPixels to copy to this texture (with locking!)
class SDL_ShaderTexture {
public:
    // Functions
    SDL_Texture* texture = nullptr; // The streaming texture through SDL
    SDL_Rect rect = {0, 0, 0, 0}; // Contains width and height, as well as user-changeable x and y for final rendering
    std::vector<unsigned char> pixels; // Contains all pixel data for this texture in BGRA order! (1 byte each for B, G, R, A per pixel in that order)
    cl::Buffer* pixelBuffer = nullptr; // The pixel buffer
    cl::Kernel* shaderKernel = nullptr; // The actual kernel program
    // Variables
    SDL_ShaderTexture(SDL_Renderer* renderer, OCL* ocl, int width, int height); // Constructor
    ~SDL_ShaderTexture(); // Destructor
    void blank(); // Reset the pixel array to all black with 0 alphas
    void shade(); // Updates pixel array from shader function
    void update(); // Updates texture from pixel array
    void setBlendMode(SDL_BlendMode blend); // Set the blend mode of the associated texture. MOD/ADD for lighting, BLEND for rendering, etc.
    void setShader(const std::string& shader); // Sets the active shader function to the passed string
private:
    // Functions
    // Variables
    OCL* ocl = nullptr; // The OpenCL's internal representation
    std::string shader = ""; // The name of the shader function to run when called
};

SDL_ShaderTexture::SDL_ShaderTexture(SDL_Renderer* renderer, OCL* ocl, int width, int height) {
    rect = {0, 0, width, height};
    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, width, height);
    pixels = std::vector<unsigned char>(width * height * 4, 0); // Pixels array is initialized to all black
    /*for (int a = 3; a < pixels.size(); a += 4) {
        pixels[a] = 255; // Set all the alphas to 255, optional if shader needs alphas. NOTE that it's ALWAYS faster to do things like this with a shader
    }*/
    this->ocl = ocl;
    pixelBuffer = new cl::Buffer(*(ocl->context), CL_MEM_READ_WRITE, sizeof(unsigned char)*pixels.size()); // Create the GPU buffer
    update();
}

SDL_ShaderTexture::~SDL_ShaderTexture() {
    SDL_DestroyTexture(texture);
    delete pixelBuffer;
    delete shaderKernel;
}

void SDL_ShaderTexture::blank() {
    // Resets the data vector to all black with alphas of 0
    // NOTE that doing this through a shader or just not bothering is MUCH FASTER. Don't use this function unless performance is not a concern.
    memset(&pixels[0], 0, pixels.size()*sizeof(pixels[0]));
}

void SDL_ShaderTexture::shade() {
    // Updates pixel array from current shader function
    ocl->queue->enqueueWriteBuffer(*pixelBuffer, CL_TRUE, 0, sizeof(unsigned char)*pixels.size(), pixels.data());
    ocl->queue->enqueueNDRangeKernel(*shaderKernel, cl::NullRange, cl::NDRange(rect.w*rect.h), cl::NullRange);
    ocl->queue->finish();
    ocl->queue->enqueueReadBuffer(*pixelBuffer, CL_TRUE, 0, sizeof(unsigned char)*pixels.size(), pixels.data());
}

void SDL_ShaderTexture::update() {
    // Updates the render texture from vector pixel data
    // FUTURE replace this with texture locking/unlocking
    SDL_UpdateTexture(texture, nullptr, pixels.data(), rect.w * 4);
}

void SDL_ShaderTexture::setBlendMode(SDL_BlendMode blend) {
    SDL_SetTextureBlendMode(texture, blend);
}

void SDL_ShaderTexture::setShader(const std::string& shader) {
    this->shader = shader;
    delete shaderKernel;
    shaderKernel = new cl::Kernel(*(ocl->program), shader.c_str());
    // Set up the first 3 kernel variables to be (by default) the pixel color array, image width, and image height.
    // These can be overwritten/added to by using: SDL_ShaderTexture::shaderKernel->setArg(3, 255);
    shaderKernel->setArg(0, *pixelBuffer);
    shaderKernel->setArg(1, rect.w);
    shaderKernel->setArg(2, rect.h);
}