# OM3D
EPITA course for 5th year students

MOOC: https://moodle.epita.fr/course/view.php?id=2292


### How to build
Requirements: cmake 3.20 minimum, C++20, Vulkan 1.3, and [Slang](https://github.com/shader-slang/slang/releases).

```bash
# At the project root
mkdir -p build/debug
cd build/debug
cmake -DCMAKE_BUILD_TYPE=Debug ../.. # use -DCMAKE_BUILD_TYPE=Release alternatively
make
```

### Contact
If you have a problem, please send a mail to
- alexandre.lamure@epita.fr
- gregoire.angerand@gmail.com
