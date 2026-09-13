# Install script for directory: C:/HAL/HAL_Program/DX22/DX22_01_plane - コピー/DX22_01_plane/packaging_work/assimp-5.2.5-src/code

# Set the install prefix
if(NOT DEFINED CMAKE_INSTALL_PREFIX)
  set(CMAKE_INSTALL_PREFIX "C:/HAL/HAL_Program/DX22/DX22_01_plane - コピー/DX22_01_plane/third_party/assimp")
endif()
string(REGEX REPLACE "/$" "" CMAKE_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")

# Set the install configuration name.
if(NOT DEFINED CMAKE_INSTALL_CONFIG_NAME)
  if(BUILD_TYPE)
    string(REGEX REPLACE "^[^A-Za-z0-9_]+" ""
           CMAKE_INSTALL_CONFIG_NAME "${BUILD_TYPE}")
  else()
    set(CMAKE_INSTALL_CONFIG_NAME "Release")
  endif()
  message(STATUS "Install configuration: \"${CMAKE_INSTALL_CONFIG_NAME}\"")
endif()

# Set the component getting installed.
if(NOT CMAKE_INSTALL_COMPONENT)
  if(COMPONENT)
    message(STATUS "Install component: \"${COMPONENT}\"")
    set(CMAKE_INSTALL_COMPONENT "${COMPONENT}")
  else()
    set(CMAKE_INSTALL_COMPONENT)
  endif()
endif()

# Is this installation the result of a crosscompile?
if(NOT DEFINED CMAKE_CROSSCOMPILING)
  set(CMAKE_CROSSCOMPILING "FALSE")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "libassimp5.2.4-dev" OR NOT CMAKE_INSTALL_COMPONENT)
  if(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Dd][Ee][Bb][Uu][Gg])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY OPTIONAL FILES "C:/HAL/HAL_Program/DX22/DX22_01_plane - コピー/DX22_01_plane/packaging_work/assimp-build-staticcrt/lib/Debug/assimp-vc143-mtd.lib")
  elseif(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Rr][Ee][Ll][Ee][Aa][Ss][Ee])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY OPTIONAL FILES "C:/HAL/HAL_Program/DX22/DX22_01_plane - コピー/DX22_01_plane/packaging_work/assimp-build-staticcrt/lib/Release/assimp-vc143-mt.lib")
  elseif(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Mm][Ii][Nn][Ss][Ii][Zz][Ee][Rr][Ee][Ll])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY OPTIONAL FILES "C:/HAL/HAL_Program/DX22/DX22_01_plane - コピー/DX22_01_plane/packaging_work/assimp-build-staticcrt/lib/MinSizeRel/assimp-vc143-mt.lib")
  elseif(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Rr][Ee][Ll][Ww][Ii][Tt][Hh][Dd][Ee][Bb][Ii][Nn][Ff][Oo])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY OPTIONAL FILES "C:/HAL/HAL_Program/DX22/DX22_01_plane - コピー/DX22_01_plane/packaging_work/assimp-build-staticcrt/lib/RelWithDebInfo/assimp-vc143-mt.lib")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "libassimp5.2.4" OR NOT CMAKE_INSTALL_COMPONENT)
  if(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Dd][Ee][Bb][Uu][Gg])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/bin" TYPE SHARED_LIBRARY FILES "C:/HAL/HAL_Program/DX22/DX22_01_plane - コピー/DX22_01_plane/packaging_work/assimp-build-staticcrt/bin/Debug/assimp-vc143-mtd.dll")
  elseif(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Rr][Ee][Ll][Ee][Aa][Ss][Ee])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/bin" TYPE SHARED_LIBRARY FILES "C:/HAL/HAL_Program/DX22/DX22_01_plane - コピー/DX22_01_plane/packaging_work/assimp-build-staticcrt/bin/Release/assimp-vc143-mt.dll")
  elseif(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Mm][Ii][Nn][Ss][Ii][Zz][Ee][Rr][Ee][Ll])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/bin" TYPE SHARED_LIBRARY FILES "C:/HAL/HAL_Program/DX22/DX22_01_plane - コピー/DX22_01_plane/packaging_work/assimp-build-staticcrt/bin/MinSizeRel/assimp-vc143-mt.dll")
  elseif(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Rr][Ee][Ll][Ww][Ii][Tt][Hh][Dd][Ee][Bb][Ii][Nn][Ff][Oo])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/bin" TYPE SHARED_LIBRARY FILES "C:/HAL/HAL_Program/DX22/DX22_01_plane - コピー/DX22_01_plane/packaging_work/assimp-build-staticcrt/bin/RelWithDebInfo/assimp-vc143-mt.dll")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "assimp-dev" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/assimp" TYPE FILE FILES
    "C:/HAL/HAL_Program/DX22/DX22_01_plane - コピー/DX22_01_plane/packaging_work/assimp-5.2.5-src/code/../include/assimp/anim.h"
    "C:/HAL/HAL_Program/DX22/DX22_01_plane - コピー/DX22_01_plane/packaging_work/assimp-5.2.5-src/code/../include/assimp/aabb.h"
    "C:/HAL/HAL_Program/DX22/DX22_01_plane - コピー/DX22_01_plane/packaging_work/assimp-5.2.5-src/code/../include/assimp/ai_assert.h"
    "C:/HAL/HAL_Program/DX22/DX22_01_plane - コピー/DX22_01_plane/packaging_work/assimp-5.2.5-src/code/../include/assimp/camera.h"
    "C:/HAL/HAL_Program/DX22/DX22_01_plane - コピー/DX22_01_plane/packaging_work/assimp-5.2.5-src/code/../include/assimp/color4.h"
    "C:/HAL/HAL_Program/DX22/DX22_01_plane - コピー/DX22_01_plane/packaging_work/assimp-5.2.5-src/code/../include/assimp/color4.inl"
    "C:/HAL/HAL_Program/DX22/DX22_01_plane - コピー/DX22_01_plane/packaging_work/assimp-build-staticcrt/code/../include/assimp/config.h"
    "C:/HAL/HAL_Program/DX22/DX22_01_plane - コピー/DX22_01_plane/packaging_work/assimp-5.2.5-src/code/../include/assimp/ColladaMetaData.h"
    "C:/HAL/HAL_Program/DX22/DX22_01_plane - コピー/DX22_01_plane/packaging_work/assimp-5.2.5-src/code/../include/assimp/commonMetaData.h"
    "C:/HAL/HAL_Program/DX22/DX22_01_plane - コピー/DX22_01_plane/packaging_work/assimp-5.2.5-src/code/../include/assimp/defs.h"
    "C:/HAL/HAL_Program/DX22/DX22_01_plane - コピー/DX22_01_plane/packaging_work/assimp-5.2.5-src/code/../include/assimp/cfileio.h"
    "C:/HAL/HAL_Program/DX22/DX22_01_plane - コピー/DX22_01_plane/packaging_work/assimp-5.2.5-src/code/../include/assimp/light.h"
    "C:/HAL/HAL_Program/DX22/DX22_01_plane - コピー/DX22_01_plane/packaging_work/assimp-5.2.5-src/code/../include/assimp/material.h"
    "C:/HAL/HAL_Program/DX22/DX22_01_plane - コピー/DX22_01_plane/packaging_work/assimp-5.2.5-src/code/../include/assimp/material.inl"
    "C:/HAL/HAL_Program/DX22/DX22_01_plane - コピー/DX22_01_plane/packaging_work/assimp-5.2.5-src/code/../include/assimp/matrix3x3.h"
    "C:/HAL/HAL_Program/DX22/DX22_01_plane - コピー/DX22_01_plane/packaging_work/assimp-5.2.5-src/code/../include/assimp/matrix3x3.inl"
    "C:/HAL/HAL_Program/DX22/DX22_01_plane - コピー/DX22_01_plane/packaging_work/assimp-5.2.5-src/code/../include/assimp/matrix4x4.h"
    "C:/HAL/HAL_Program/DX22/DX22_01_plane - コピー/DX22_01_plane/packaging_work/assimp-5.2.5-src/code/../include/assimp/matrix4x4.inl"
    "C:/HAL/HAL_Program/DX22/DX22_01_plane - コピー/DX22_01_plane/packaging_work/assimp-5.2.5-src/code/../include/assimp/mesh.h"
    "C:/HAL/HAL_Program/DX22/DX22_01_plane - コピー/DX22_01_plane/packaging_work/assimp-5.2.5-src/code/../include/assimp/ObjMaterial.h"
    "C:/HAL/HAL_Program/DX22/DX22_01_plane - コピー/DX22_01_plane/packaging_work/assimp-5.2.5-src/code/../include/assimp/pbrmaterial.h"
    "C:/HAL/HAL_Program/DX22/DX22_01_plane - コピー/DX22_01_plane/packaging_work/assimp-5.2.5-src/code/../include/assimp/GltfMaterial.h"
    "C:/HAL/HAL_Program/DX22/DX22_01_plane - コピー/DX22_01_plane/packaging_work/assimp-5.2.5-src/code/../include/assimp/postprocess.h"
    "C:/HAL/HAL_Program/DX22/DX22_01_plane - コピー/DX22_01_plane/packaging_work/assimp-5.2.5-src/code/../include/assimp/quaternion.h"
    "C:/HAL/HAL_Program/DX22/DX22_01_plane - コピー/DX22_01_plane/packaging_work/assimp-5.2.5-src/code/../include/assimp/quaternion.inl"
    "C:/HAL/HAL_Program/DX22/DX22_01_plane - コピー/DX22_01_plane/packaging_work/assimp-5.2.5-src/code/../include/assimp/scene.h"
    "C:/HAL/HAL_Program/DX22/DX22_01_plane - コピー/DX22_01_plane/packaging_work/assimp-5.2.5-src/code/../include/assimp/metadata.h"
    "C:/HAL/HAL_Program/DX22/DX22_01_plane - コピー/DX22_01_plane/packaging_work/assimp-5.2.5-src/code/../include/assimp/texture.h"
    "C:/HAL/HAL_Program/DX22/DX22_01_plane - コピー/DX22_01_plane/packaging_work/assimp-5.2.5-src/code/../include/assimp/types.h"
    "C:/HAL/HAL_Program/DX22/DX22_01_plane - コピー/DX22_01_plane/packaging_work/assimp-5.2.5-src/code/../include/assimp/vector2.h"
    "C:/HAL/HAL_Program/DX22/DX22_01_plane - コピー/DX22_01_plane/packaging_work/assimp-5.2.5-src/code/../include/assimp/vector2.inl"
    "C:/HAL/HAL_Program/DX22/DX22_01_plane - コピー/DX22_01_plane/packaging_work/assimp-5.2.5-src/code/../include/assimp/vector3.h"
    "C:/HAL/HAL_Program/DX22/DX22_01_plane - コピー/DX22_01_plane/packaging_work/assimp-5.2.5-src/code/../include/assimp/vector3.inl"
    "C:/HAL/HAL_Program/DX22/DX22_01_plane - コピー/DX22_01_plane/packaging_work/assimp-5.2.5-src/code/../include/assimp/version.h"
    "C:/HAL/HAL_Program/DX22/DX22_01_plane - コピー/DX22_01_plane/packaging_work/assimp-5.2.5-src/code/../include/assimp/cimport.h"
    "C:/HAL/HAL_Program/DX22/DX22_01_plane - コピー/DX22_01_plane/packaging_work/assimp-5.2.5-src/code/../include/assimp/importerdesc.h"
    "C:/HAL/HAL_Program/DX22/DX22_01_plane - コピー/DX22_01_plane/packaging_work/assimp-5.2.5-src/code/../include/assimp/Importer.hpp"
    "C:/HAL/HAL_Program/DX22/DX22_01_plane - コピー/DX22_01_plane/packaging_work/assimp-5.2.5-src/code/../include/assimp/DefaultLogger.hpp"
    "C:/HAL/HAL_Program/DX22/DX22_01_plane - コピー/DX22_01_plane/packaging_work/assimp-5.2.5-src/code/../include/assimp/ProgressHandler.hpp"
    "C:/HAL/HAL_Program/DX22/DX22_01_plane - コピー/DX22_01_plane/packaging_work/assimp-5.2.5-src/code/../include/assimp/IOStream.hpp"
    "C:/HAL/HAL_Program/DX22/DX22_01_plane - コピー/DX22_01_plane/packaging_work/assimp-5.2.5-src/code/../include/assimp/IOSystem.hpp"
    "C:/HAL/HAL_Program/DX22/DX22_01_plane - コピー/DX22_01_plane/packaging_work/assimp-5.2.5-src/code/../include/assimp/Logger.hpp"
    "C:/HAL/HAL_Program/DX22/DX22_01_plane - コピー/DX22_01_plane/packaging_work/assimp-5.2.5-src/code/../include/assimp/LogStream.hpp"
    "C:/HAL/HAL_Program/DX22/DX22_01_plane - コピー/DX22_01_plane/packaging_work/assimp-5.2.5-src/code/../include/assimp/NullLogger.hpp"
    "C:/HAL/HAL_Program/DX22/DX22_01_plane - コピー/DX22_01_plane/packaging_work/assimp-5.2.5-src/code/../include/assimp/cexport.h"
    "C:/HAL/HAL_Program/DX22/DX22_01_plane - コピー/DX22_01_plane/packaging_work/assimp-5.2.5-src/code/../include/assimp/Exporter.hpp"
    "C:/HAL/HAL_Program/DX22/DX22_01_plane - コピー/DX22_01_plane/packaging_work/assimp-5.2.5-src/code/../include/assimp/DefaultIOStream.h"
    "C:/HAL/HAL_Program/DX22/DX22_01_plane - コピー/DX22_01_plane/packaging_work/assimp-5.2.5-src/code/../include/assimp/DefaultIOSystem.h"
    "C:/HAL/HAL_Program/DX22/DX22_01_plane - コピー/DX22_01_plane/packaging_work/assimp-5.2.5-src/code/../include/assimp/ZipArchiveIOSystem.h"
    "C:/HAL/HAL_Program/DX22/DX22_01_plane - コピー/DX22_01_plane/packaging_work/assimp-5.2.5-src/code/../include/assimp/SceneCombiner.h"
    "C:/HAL/HAL_Program/DX22/DX22_01_plane - コピー/DX22_01_plane/packaging_work/assimp-5.2.5-src/code/../include/assimp/fast_atof.h"
    "C:/HAL/HAL_Program/DX22/DX22_01_plane - コピー/DX22_01_plane/packaging_work/assimp-5.2.5-src/code/../include/assimp/qnan.h"
    "C:/HAL/HAL_Program/DX22/DX22_01_plane - コピー/DX22_01_plane/packaging_work/assimp-5.2.5-src/code/../include/assimp/BaseImporter.h"
    "C:/HAL/HAL_Program/DX22/DX22_01_plane - コピー/DX22_01_plane/packaging_work/assimp-5.2.5-src/code/../include/assimp/Hash.h"
    "C:/HAL/HAL_Program/DX22/DX22_01_plane - コピー/DX22_01_plane/packaging_work/assimp-5.2.5-src/code/../include/assimp/MemoryIOWrapper.h"
    "C:/HAL/HAL_Program/DX22/DX22_01_plane - コピー/DX22_01_plane/packaging_work/assimp-5.2.5-src/code/../include/assimp/ParsingUtils.h"
    "C:/HAL/HAL_Program/DX22/DX22_01_plane - コピー/DX22_01_plane/packaging_work/assimp-5.2.5-src/code/../include/assimp/StreamReader.h"
    "C:/HAL/HAL_Program/DX22/DX22_01_plane - コピー/DX22_01_plane/packaging_work/assimp-5.2.5-src/code/../include/assimp/StreamWriter.h"
    "C:/HAL/HAL_Program/DX22/DX22_01_plane - コピー/DX22_01_plane/packaging_work/assimp-5.2.5-src/code/../include/assimp/StringComparison.h"
    "C:/HAL/HAL_Program/DX22/DX22_01_plane - コピー/DX22_01_plane/packaging_work/assimp-5.2.5-src/code/../include/assimp/StringUtils.h"
    "C:/HAL/HAL_Program/DX22/DX22_01_plane - コピー/DX22_01_plane/packaging_work/assimp-5.2.5-src/code/../include/assimp/SGSpatialSort.h"
    "C:/HAL/HAL_Program/DX22/DX22_01_plane - コピー/DX22_01_plane/packaging_work/assimp-5.2.5-src/code/../include/assimp/GenericProperty.h"
    "C:/HAL/HAL_Program/DX22/DX22_01_plane - コピー/DX22_01_plane/packaging_work/assimp-5.2.5-src/code/../include/assimp/SpatialSort.h"
    "C:/HAL/HAL_Program/DX22/DX22_01_plane - コピー/DX22_01_plane/packaging_work/assimp-5.2.5-src/code/../include/assimp/SkeletonMeshBuilder.h"
    "C:/HAL/HAL_Program/DX22/DX22_01_plane - コピー/DX22_01_plane/packaging_work/assimp-5.2.5-src/code/../include/assimp/SmallVector.h"
    "C:/HAL/HAL_Program/DX22/DX22_01_plane - コピー/DX22_01_plane/packaging_work/assimp-5.2.5-src/code/../include/assimp/SmoothingGroups.h"
    "C:/HAL/HAL_Program/DX22/DX22_01_plane - コピー/DX22_01_plane/packaging_work/assimp-5.2.5-src/code/../include/assimp/SmoothingGroups.inl"
    "C:/HAL/HAL_Program/DX22/DX22_01_plane - コピー/DX22_01_plane/packaging_work/assimp-5.2.5-src/code/../include/assimp/StandardShapes.h"
    "C:/HAL/HAL_Program/DX22/DX22_01_plane - コピー/DX22_01_plane/packaging_work/assimp-5.2.5-src/code/../include/assimp/RemoveComments.h"
    "C:/HAL/HAL_Program/DX22/DX22_01_plane - コピー/DX22_01_plane/packaging_work/assimp-5.2.5-src/code/../include/assimp/Subdivision.h"
    "C:/HAL/HAL_Program/DX22/DX22_01_plane - コピー/DX22_01_plane/packaging_work/assimp-5.2.5-src/code/../include/assimp/Vertex.h"
    "C:/HAL/HAL_Program/DX22/DX22_01_plane - コピー/DX22_01_plane/packaging_work/assimp-5.2.5-src/code/../include/assimp/LineSplitter.h"
    "C:/HAL/HAL_Program/DX22/DX22_01_plane - コピー/DX22_01_plane/packaging_work/assimp-5.2.5-src/code/../include/assimp/TinyFormatter.h"
    "C:/HAL/HAL_Program/DX22/DX22_01_plane - コピー/DX22_01_plane/packaging_work/assimp-5.2.5-src/code/../include/assimp/Profiler.h"
    "C:/HAL/HAL_Program/DX22/DX22_01_plane - コピー/DX22_01_plane/packaging_work/assimp-5.2.5-src/code/../include/assimp/LogAux.h"
    "C:/HAL/HAL_Program/DX22/DX22_01_plane - コピー/DX22_01_plane/packaging_work/assimp-5.2.5-src/code/../include/assimp/Bitmap.h"
    "C:/HAL/HAL_Program/DX22/DX22_01_plane - コピー/DX22_01_plane/packaging_work/assimp-5.2.5-src/code/../include/assimp/XMLTools.h"
    "C:/HAL/HAL_Program/DX22/DX22_01_plane - コピー/DX22_01_plane/packaging_work/assimp-5.2.5-src/code/../include/assimp/IOStreamBuffer.h"
    "C:/HAL/HAL_Program/DX22/DX22_01_plane - コピー/DX22_01_plane/packaging_work/assimp-5.2.5-src/code/../include/assimp/CreateAnimMesh.h"
    "C:/HAL/HAL_Program/DX22/DX22_01_plane - コピー/DX22_01_plane/packaging_work/assimp-5.2.5-src/code/../include/assimp/XmlParser.h"
    "C:/HAL/HAL_Program/DX22/DX22_01_plane - コピー/DX22_01_plane/packaging_work/assimp-5.2.5-src/code/../include/assimp/BlobIOSystem.h"
    "C:/HAL/HAL_Program/DX22/DX22_01_plane - コピー/DX22_01_plane/packaging_work/assimp-5.2.5-src/code/../include/assimp/MathFunctions.h"
    "C:/HAL/HAL_Program/DX22/DX22_01_plane - コピー/DX22_01_plane/packaging_work/assimp-5.2.5-src/code/../include/assimp/Exceptional.h"
    "C:/HAL/HAL_Program/DX22/DX22_01_plane - コピー/DX22_01_plane/packaging_work/assimp-5.2.5-src/code/../include/assimp/ByteSwapper.h"
    "C:/HAL/HAL_Program/DX22/DX22_01_plane - コピー/DX22_01_plane/packaging_work/assimp-5.2.5-src/code/../include/assimp/Base64.hpp"
    )
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "assimp-dev" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/assimp/Compiler" TYPE FILE FILES
    "C:/HAL/HAL_Program/DX22/DX22_01_plane - コピー/DX22_01_plane/packaging_work/assimp-5.2.5-src/code/../include/assimp/Compiler/pushpack1.h"
    "C:/HAL/HAL_Program/DX22/DX22_01_plane - コピー/DX22_01_plane/packaging_work/assimp-5.2.5-src/code/../include/assimp/Compiler/poppack1.h"
    "C:/HAL/HAL_Program/DX22/DX22_01_plane - コピー/DX22_01_plane/packaging_work/assimp-5.2.5-src/code/../include/assimp/Compiler/pstdint.h"
    )
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  if(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Dd][Ee][Bb][Uu][Gg])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE FILE FILES "C:/HAL/HAL_Program/DX22/DX22_01_plane - コピー/DX22_01_plane/packaging_work/assimp-build-staticcrt/code/Debug/assimp-vc143-mtd.pdb")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  if(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Rr][Ee][Ll][Ww][Ii][Tt][Hh][Dd][Ee][Bb][Ii][Nn][Ff][Oo])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE FILE FILES "C:/HAL/HAL_Program/DX22/DX22_01_plane - コピー/DX22_01_plane/packaging_work/assimp-build-staticcrt/code/RelWithDebInfo/assimp-vc143-mt.pdb")
  endif()
endif()

