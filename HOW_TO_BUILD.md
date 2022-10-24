
# Building Mobius 2 (preliminary, before proper system)

## Get LLVM

LLVM is the library that is used to compile intermediate representations of code into target specific machine code.

You can install and build LLVM on Windows using this guide:
https://llvm.org/docs/GettingStartedVS.html

If you have made it to step 13., you will have an llvm.sln file in your build folder that you can open in Visual Studio Community edition.
Inside Visual Studio, select the Release build and build it (you can also build the Debug build, but both will take a couple of hours to build, and I haven't set up a debug build for Mobius2 yet).

Depending on how you chose to set things up you will have a path to the clone of the llvm repository and a path to your build of LLVM. The build of Mobius2 will need to link to files in both of these paths.

My setup ( for instance Mobius2/test/compile.bat , which is the one you should start with ) assumes specific paths to these:
- C:\Data\llvm
- C:\Data\build     -- this one is particularly badly named.

but we should definitely have a better system for it, and that is possible with Cmake.

To run visual studio from the command line (which is what you do in compile.bat ), you also need to run vcvars64 , which is what I do in Mobius2/test/setupvc.bat , but your path to vcvars64 may be different.

## Build Mobius2

To build the preliminary test, you only need to run Mobius2/test/compile.bat (if all the paths in it are correct)

It will produce prelim_test.exe, which you can run using

	prelim_test.exe ../models/models/simplyq_model.txt ../models/models/data/simplyq_tarland.dat

It produces a results.dat file.

## Building MobiView
**Don't worry about this for now**

MobiView is the GUI. It is a separate repository
https://github.com/NIVANorge/MobiView2
and requires you to install ultimatepp
https://www.ultimatepp.org/app$ide$install_win32_en-us.html

You should clone MobiView2 to
upp/MyApps/MobiView2

unfortunately the build system I set up for MobiView2 also assumes specific paths to Mobius2 and LLVM for now :(
