
This is about how you can build Mobius2 yourself. If you just want to download the Windows binaries, go to
ftp://mobiserver.niva.no/Mobius2


# Building Mobius 2 (preliminary, before proper system)

## Get LLVM

LLVM is the library that is used to compile intermediate representations of code into target specific machine code.

You can install and build LLVM on Windows using this guide:
https://llvm.org/docs/GettingStartedVS.html

If you have made it to step 13., you will have an llvm.sln file in your build folder that you can open in Visual Studio Community edition.
Inside Visual Studio, select the Release build and build it (you can also build the Debug build, but both will take a couple of hours to build, and I haven't set up a debug build for Mobius2 yet).

Depending on how you chose to set things up you will have a path to the clone of the llvm repository and a path to your build of LLVM. The build of Mobius2 will need to link to files in both of these paths.

My setup ( for instance Mobius2/mobipy/compile_dll.bat , which is the one you should start with ) assumes specific paths to these:
- C:\Data\llvm
- C:\Data\build     -- this one is particularly badly named.

but we should definitely have a better system for it, and that is possible with Cmake.

To run visual studio from the command line (which is what you do in compile.bat ), you also need to run vcvars64 , which is what I do in Mobius2/mobipy/setupvc.bat , but your path to vcvars64 may be different.

## Get OpenXLSX
Clone https://github.com/troldal/OpenXLSX

Next use cmake to run their CMakeLists.txt

Next you must make sure that your compilation script points to the right folders for include and lib files for OpenXLSX (see mobipy/compile_dll.bat).

## Build Mobius2

To build mobipy run mobipy/compile_dll.bat

You can then use mobipy to run Mobius2 models, see for instance test/python_test.ipynb


## Building MobiView
This is a bit more difficult for now.

MobiView is the GUI. It is a separate repository
https://github.com/NIVANorge/MobiView2
and requires you to install ultimatepp
https://www.ultimatepp.org/app$ide$install_win32_en-us.html

You should clone MobiView2 to
upp/MyApps/MobiView2

unfortunately the build system I set up for MobiView2 also assumes specific paths to Mobius2 and LLVM for now :(
It also has a few other dependencies like Dlib and Graphviz.
