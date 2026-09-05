# nebula
My personal graphics engine.

### How to build
Requirements: cmake 3.20 minimum, C++20, Vulkan 1.3, and [Slang](https://github.com/shader-slang/slang/releases).

```bash
# At the project root
mkdir build
cd build
cmake -DCMAKE_BUILD_TYPE=Debug .. # use -DCMAKE_BUILD_TYPE=Release alternatively
make
```
