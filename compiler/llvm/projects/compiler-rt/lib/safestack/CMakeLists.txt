add_compiler_rt_component(safestack)
add_compiler_rt_component(safestacksepseg)

set(SAFESTACK_SOURCES safestack.cc)
set(SAFESTACKMPX_SOURCES safestackmpx.cc)

include_directories(..)

set(SAFESTACK_CFLAGS ${SANITIZER_COMMON_CFLAGS})
# CPUID will be used to check for XSAVEC support at runtime
set(SAFESTACKMPX_CFLAGS ${SANITIZER_COMMON_CFLAGS} -mxsavec)

if(APPLE)
  # Build universal binary on APPLE.
  add_compiler_rt_runtime(clang_rt.safestack
    STATIC
    OS osx
    ARCHS ${SAFESTACK_SUPPORTED_ARCH}
    SOURCES ${SAFESTACK_SOURCES}
            $<TARGET_OBJECTS:RTInterception.osx>
            $<TARGET_OBJECTS:RTSanitizerCommon.osx>
            $<TARGET_OBJECTS:RTSanitizerCommonNoLibc.osx>
    CFLAGS ${SAFESTACK_CFLAGS}
    PARENT_TARGET safestack)
else()
  # Otherwise, build separate libraries for each target.
  foreach(arch ${SAFESTACK_SUPPORTED_ARCH})
    add_compiler_rt_runtime(clang_rt.safestack
      STATIC
      ARCHS ${arch}
      SOURCES ${SAFESTACK_SOURCES}
              $<TARGET_OBJECTS:RTInterception.${arch}>
              $<TARGET_OBJECTS:RTSanitizerCommon.${arch}>
              $<TARGET_OBJECTS:RTSanitizerCommonNoLibc.${arch}>
      CFLAGS ${SAFESTACK_CFLAGS}
      PARENT_TARGET safestack)
  endforeach()
  add_compiler_rt_runtime(clang_rt.safestacksepseg
    STATIC
    ARCHS x86_64
    # Build safestack sources into this library instead of linking both
    # libraries, because RTSanitizerCommonNoLibc and RTSanitizerCommonLibc define
    # some identical symbols.
    SOURCES ${SAFESTACK_SOURCES}
            ${SAFESTACKMPX_SOURCES}
            $<TARGET_OBJECTS:RTInterception.x86_64>
            $<TARGET_OBJECTS:RTSanitizerCommon.x86_64>
            $<TARGET_OBJECTS:RTSanitizerCommonLibc.x86_64>
    CFLAGS ${SAFESTACKMPX_CFLAGS}
    PARENT_TARGET safestacksepseg)
endif()
