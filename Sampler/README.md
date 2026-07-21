# BVH Viewer Sampler

The sampler is a reference integration for Real-Time BVH Viewer. It is intended for black-box inspection of an existing D3D12 or Vulkan process without directly modifying the target application.

The sampler hosts the viewer configuration and communication state, then attaches API-specific modules to the target process:

- `BvhvSampler.exe` is the host process.
- `BvhvSamplerD3D12.dll` provides the D3D12 interception module.
- `BvhvSamplerVulkan.dll` provides the Vulkan layer module.

## Requirements

- Windows.
- CMake 3.16 or newer.
- A C++20-capable MSVC toolchain.
- Vulkan SDK, used for Vulkan headers/libraries and shader compilation tools.
- `glslangValidator` and `dxc` on `PATH`, or available through `VULKAN_SDK`.
- A GPU and driver stack with ray tracing support for the API you intend to inspect.

The sampler build also fetches or uses these dependencies through CMake:

- The core `BvhViewer` project.
- jsonxx.
- MinHook.

## Building

Configure and build from the repository root:

```powershell
cmake -S Sampler -B Sampler/_Build -A x64
cmake --build Sampler/_Build --config Release
```

Build outputs are written to `Sampler/_Bin/<Config>`, for example `Sampler/_Bin/Release`.

For Visual Studio users, the [Smart Command Line Arguments 2022](https://marketplace.visualstudio.com/items?itemName=MBulli.SmartCommandlineArguments2022) extension is recommended. The repository includes a `.arguments` file with useful sampler launch presets that can be managed through this extension.

## Usage

Run the sampler from `Sampler/_Bin/<Config>` after building:

```powershell
.\BvhvSampler.exe --target=MyGame.exe -w 1280 -h 720
```

Useful command line options:

- `--target=<process.exe>` selects a target process. Use `--target=auto` to enable automatic attachment subject to filtering.
- `--disableD3D12` skips the D3D12 module.
- `--disableVK` skips the Vulkan layer.
- `--inlineRendering` appends viewer rendering to the target command stream instead of using the viewer's own path.
- `--forcePositionFetchExt` forces the Vulkan position-fetch visualization path.
- `-w <width>` and `-h <height>` set the initial viewer window size.
- `--help` prints the full command line usage.

The executable is linked with a `requireAdministrator` manifest. Launch it from an elevated shell when attaching to another process.

## Process Filtering

`.processFilter` is copied to the output directory as `processFilter.txt`. The sampler uses it to avoid attaching to common system, desktop, editor, and tool processes when automatic attachment is enabled.

## Integration Notes

The Vulkan path installs a Vulkan layer from `BvhvSamplerVulkan.dll`.

The D3D12 path loads `BvhvSamplerD3D12.dll`, initializes DXGI interception, and installs a Windows hook used to trigger D3D12 DLL injection into active processes.

For inline rendering, BvhvSampler tracks compute pipeline state and restores it after rendering occurs. Expect some overhead from this state tracking.
