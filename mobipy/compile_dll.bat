cl /MD /LD ../src/c_abi.cpp ../src/llvm_jit.cpp ../src/resolve_identifier.cpp ../src/model_compilation.cpp  ../src/model_codegen.cpp ../src/tree_pruning.cpp ../src/spreadsheet_inputs_openxlsx.cpp ../src/process_series_data.cpp ../src/data_set.cpp ../src/model_application.cpp ../src/model_composition.cpp ../src/run_model.cpp ../src/lexer.cpp ../src/ast.cpp ../src/model_declaration.cpp ../src/function_tree.cpp  ../src/emulate.cpp ../src/units.cpp ../src/ode_solvers.cpp ../src/file_utils.cpp ../src/connection_regex.cpp ../src/index_data.cpp ../src/catalog.cpp ../src/model_specific/nivafjord_special.cpp OpenXLSX.lib LLVMX86TargetMCA.lib LLVMMCA.lib LLVMX86Disassembler.lib LLVMX86AsmParser.lib LLVMX86CodeGen.lib LLVMCFGuard.lib LLVMGlobalISel.lib LLVMX86Desc.lib LLVMX86Info.lib LLVMSelectionDAG.lib LLVMAsmPrinter.lib LLVMCodeGen.lib LLVMOrcJIT.lib LLVMPasses.lib LLVMObjCARCOpts.lib LLVMCoroutines.lib LLVMipo.lib LLVMInstrumentation.lib LLVMVectorize.lib LLVMLinker.lib LLVMIRReader.lib LLVMAsmParser.lib LLVMFrontendOpenMP.lib LLVMScalarOpts.lib LLVMInstCombine.lib LLVMBitWriter.lib LLVMAggressiveInstCombine.lib LLVMTransformUtils.lib LLVMMCDisassembler.lib LLVMJITLink.lib LLVMExecutionEngine.lib LLVMTarget.lib LLVMAnalysis.lib LLVMProfileData.lib LLVMSymbolize.lib LLVMDebugInfoPDB.lib LLVMDebugInfoMSF.lib LLVMDebugInfoDWARF.lib LLVMRuntimeDyld.lib LLVMOrcTargetProcess.lib LLVMOrcShared.lib LLVMObject.lib LLVMTextAPI.lib LLVMMCParser.lib LLVMBitReader.lib LLVMMC.lib LLVMDebugInfoCodeView.lib LLVMCore.lib LLVMRemarks.lib LLVMBitstreamReader.lib LLVMBinaryFormat.lib LLVMSupport.lib LLVMDemangle.lib psapi.lib shell32.lib  uuid.lib advapi32.lib /IC:\Data\llvm\llvm\include /IC:\Data\build\include /IC:/Data/OpenXLSX/OpenXLSX/ /w /std:c++17 /EHsc /GR- -D_CRT_SECURE_NO_DEPRECATE -D_CRT_SECURE_NO_WARNINGS -D_CRT_NONSTDC_NO_DEPRECATE -D_CRT_NONSTDC_NO_WARNINGS -D_SCL_SECURE_NO_DEPRECATE -D_SCL_SECURE_NO_WARNINGS -DUNICODE -D_UNICODE -D__STDC_CONSTANT_MACROS -D__STDC_FORMAT_MACROS -D__STDC_LIMIT_MACROS -DMOBIUS_ERROR_STREAMS /O2 /link /LIBPATH:C:\Data\build\Release\lib /LIBPATH:C:\Data\OpenXLSX\output\Release

REM llvm-config --libs all