set(sources
  CAN_SHC31.cpp
  Shc31ChannelSettings.cpp
  Shc31BitTiming.cpp
  Shc31GsUsb.cpp
  ../DIL_Interface/HardwareListingCAN.cpp)

set(headers
  CAN_SHC31_Extern.h
  CAN_SHC31_stdafx.h
  ChannelSelectionResource.h
  Shc31ChannelSettings.h
  Shc31BitTiming.h
  Shc31GsUsb.h
  Shc31Resource.h
  ../DIL_Interface/HardwareListingCAN.h)

set(resources
  CAN_SHC31.rc)

add_library(CAN_SHC31 SHARED ${sources} ${headers} ${resources})

include_directories(
  ..
  ${ICONV_INCLUDE_DIR}
  ${LIBXML2_INCLUDE_DIR}
  ${ATL_INCLUDE_DIRS}
  ${MFC_INCLUDE_DIRS})

# compiler options
add_definitions(-D_USRDLL)

# linker options
target_link_libraries(CAN_SHC31
  DataTypes
  Utils
  winusb
  cfgmgr32)

# installer options
add_custom_command(
  TARGET CAN_SHC31
  POST_BUILD
  COMMAND ${CMAKE_COMMAND} ARGS -E make_directory ${PROJECT_SOURCE_DIR}/BIN/${CMAKE_BUILD_TYPE}/
  COMMAND ${CMAKE_COMMAND} ARGS -E copy $<TARGET_FILE:CAN_SHC31> ${PROJECT_SOURCE_DIR}/BIN/${CMAKE_BUILD_TYPE}/)
