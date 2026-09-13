IF(NOT EXISTS "C:/HAL/HAL_Program/DX22/DX22_01_plane - コピー/DX22_01_plane/packaging_work/assimp-build-staticcrt/install_manifest.txt")
  MESSAGE(FATAL_ERROR "Cannot find install manifest: \"C:/HAL/HAL_Program/DX22/DX22_01_plane - コピー/DX22_01_plane/packaging_work/assimp-build-staticcrt/install_manifest.txt\"")
ENDIF(NOT EXISTS "C:/HAL/HAL_Program/DX22/DX22_01_plane - コピー/DX22_01_plane/packaging_work/assimp-build-staticcrt/install_manifest.txt")

FILE(READ "C:/HAL/HAL_Program/DX22/DX22_01_plane - コピー/DX22_01_plane/packaging_work/assimp-build-staticcrt/install_manifest.txt" files)
STRING(REGEX REPLACE "\n" ";" files "${files}")
FOREACH(file ${files})
  MESSAGE(STATUS "Uninstalling \"$ENV{DESTDIR}${file}\"")
  EXEC_PROGRAM(
    "C:/Program Files/Microsoft Visual Studio/2022/Enterprise/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" ARGS "-E remove \"$ENV{DESTDIR}${file}\""
    OUTPUT_VARIABLE rm_out
    RETURN_VALUE rm_retval
    )
  IF(NOT "${rm_retval}" STREQUAL 0)
    MESSAGE(FATAL_ERROR "Problem when removing \"$ENV{DESTDIR}${file}\"")
  ENDIF(NOT "${rm_retval}" STREQUAL 0)
ENDFOREACH(file)
