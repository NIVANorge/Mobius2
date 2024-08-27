---
layout: default
title: Linux installation
parent: mobipy
nav_order: 1
---

# Linux installation

On Linux you need to build mobipy from source code. First you need to install the dependencies. 

Note: If you are a NIVA employee on NIVA's jupyterhub, you can skip to step 3. The dependencies are already installed.

## 0. Make sure you have what is needed to build programs

This step may be unnecessary unless you are working on a completely fresh system.

```shell
sudo apt update
sudo apt upgrade
sudo apt install build-essential
sudo apt install cmake
```

## 1. Install OpenXLSX

Clone the git repository https://github.com/troldal/OpenXLSX using e.g.

```shell
git clone https://github.com/troldal/OpenXLSX.git
```

Follow the installation process on their front page, that is run the following shell commands:

```shell
cd OpenXLSX
mkdir build
cd build
cmake ..
cmake --build . --target OpenXLSX --config Release
cmake --install .
```

In some cases `cmake --install .` doesn't copy all the header files correctly, in that case you may also need to run

```shell
cp ../OpenXLSX/external /usr/local/include/OpenXLSX
```

## 2. Install LLVM

Run the following commands that installs LLVM 18 (see the [reference page](https://apt.llvm.org/) if needed).

```shell
wget https://apt.llvm.org/llvm.sh
chmod +x llvm.sh
./llvm.sh 18
rm llvm.sh
```

## 3. Build mobipy

Assuming you have already cloned Mobius2 using

```shell
git clone https://github.com/NIVANorge/Mobius2.git
```

go to `Mobius2/mobipy` and run

```shell
chmod +x compile.sh
./compile.sh
```

You should re-run `compile.sh` every time you pull the Mobius2 repository to get the latest changes and fixes.

## 4. Test it

Try to test mobipy or mobius.jl using one of the [example notebooks](https://github.com/NIVANorge/Mobius2/blob/main/example_notebooks/).