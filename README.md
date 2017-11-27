# redream

[![travis-ci status](https://travis-ci.org/inolen/redream.svg?branch=master)](https://travis-ci.org/inolen/redream)
[![appveyor status](https://ci.appveyor.com/api/projects/status/github/inolen/redream)](https://ci.appveyor.com/project/inolen/redream)
[![slack status](http://slack.redream.io/badge.svg)](http://slack.redream.io)

[redream](http://redream.io) is a work-in-progress SEGA Dreamcast emulator written in C for Mac, Linux and Windows.

redream is licensed under the GPLv3 license (see [LICENSE.txt](LICENSE.txt)) and uses third party libraries that are each distributed under their own terms (see each library's license in [deps/](deps/)).

Ask questions and help answer them on [our Slack group](http://slack.redream.io).

## Downloading

The latest pre-built binaries can be found on the [downloads](http://redream.io/download) page.

## Building

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

## Reporting bugs

Report bugs via the [GitHub issue queue](https://github.com/inolen/redream/issues).
