---
title: Building
---

Start by cloning the repository and setting up a build directory.

```
git clone https://github.com/inolen/redream.git
mkdir redream_build
cd redream_build
```

Next, generate a makefile or project file for your IDE of choice. For more info on the supported IDEs, checkout the [CMake documentation](http://www.cmake.org/cmake/help/latest/manual/cmake-generators.7.html).

```
# Makefile
cmake -DCMAKE_BUILD_TYPE=RELEASE ../redream

# Xcode project
cmake -G "Xcode" ../redream

# Visual Studio project
cmake -G "Visual Studio 14 Win64" ../redream
```

Finally, you can either run `make` from the command line if you've generated a Makefile or load up the project file and compile the code from inside of your IDE.

The build has been tested on OSX 10.10 with clang 3.6, Ubuntu 14.04 with GCC 4.9 and Windows 8.1 with Visual Studio 2015.

