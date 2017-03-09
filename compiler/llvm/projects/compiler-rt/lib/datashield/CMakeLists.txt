add_custom_target(datashield)

#set(DATASHIELD_SOURCES datashield.cc malloc.cc mpxrt.c mpxrt-utils.c)
set(DATASHIELD_SOURCES datashield.cc malloc.cc) # am i actually using these mpx files?

include_directories(..)

set(DATASHIELD_CFLAGS ${SANITIZER_COMMON_CFLAGS})

if(APPLE)
  # Build universal binary on APPLE.
  add_compiler_rt_runtime(clang_rt.datashield
    STATIC
    OS osx
    ARCHS ${DATASHIELD_SUPPORTED_ARCH}
    SOURCES ${DATASHIELD_SOURCES}
            $<TARGET_OBJECTS:RTInterception.osx>
            $<TARGET_OBJECTS:RTSanitizerCommon.osx>
            $<TARGET_OBJECTS:RTSanitizerCommonNoLibc.osx>
    CFLAGS ${DATASHIELD_CFLAGS}
    PARENT_TARGET datashield)
else()
  # Otherwise, build separate libraries for each target.
  foreach(arch ${DATASHIELD_SUPPORTED_ARCH})
    add_compiler_rt_runtime(clang_rt.datashield
      STATIC
      ARCHS ${arch}
      SOURCES ${DATASHIELD_SOURCES}
              $<TARGET_OBJECTS:RTInterception.${arch}>
              $<TARGET_OBJECTS:RTSanitizerCommon.${arch}>
              $<TARGET_OBJECTS:RTSanitizerCommonNoLibc.${arch}>
      CFLAGS ${DATASHIELD_CFLAGS}
      PARENT_TARGET datashield)
  endforeach()
endif()
