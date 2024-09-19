## Introduction

Build your C++ project using the C++ rules you defined, which are implemented
in C++, so you can use std::string/std::vector/std::map these STL containers
and you can define any variable, function or class. Unbelievable, right?

Define your building rules in 'BUILD.inc' or 'BUILD.cpp' under each dir, and
define global common vars/funcs/rules in '${project_root}/WORKSPACE.h';

See all available C++ APIs in [zmake.h](https://github.com/bacoo/zmake/blob/main/zmake.h) and the demo under [demo](https://github.com/bacoo/zmake/tree/main/demo);

Firstly, use `zmake` to build your project(it'll firstly generate 'BUILD.exe'
under your project root dir and then run `./BUILD.exe` to build), and you can
use `./BUILD.exe` to rebuild if you only modify project's source/header files(no
change for the building rules, i.e.: no any change for any BUILD.inc/BUILD.cpp/
WORKSPACE.h);

All generated files locate under '.zmade/', and if you want to clean, just
`rm -rf .zmade/`, and you can find the files that record original compile or
link commands by `find .zmade/ -name '*.cmd'`;

Using `zmake -h` to see all options.

Here is a [PPT](https://docs.google.com/presentation/d/1OAGkP0JPL35BVcp9hyJFMPcm3qqrBYYGl1hhDyuVBco/edit#slide=id.g30258cc35aa_0_0) for introduction zmake.

Report bugs to [bacoo_zh@163.com](bacoo_zh@163.com)

## Install

Execute following commands:

* git clone https://github.com/bacoo/zmake.git
* cd zmake
* make -j && make clean

`zmake` and its headers and lib will be installed under ~/bin/

## Build [demo](https://github.com/bacoo/zmake/tree/main/demo)

You need to build [project1](https://github.com/bacoo/zmake/tree/main/demo/project1) firstly, since it is depended by [project2](https://github.com/bacoo/zmake/tree/main/demo/project2).

1. build demo/project1

* cd demo/project1
* zmake -e

2. build demo/project2

* cd demo/project2
* zmake -v
