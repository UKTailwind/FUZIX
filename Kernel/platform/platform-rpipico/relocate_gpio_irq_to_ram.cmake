# Relocate the Pico SDK's GPIO IRQ dispatcher into RAM without forking
# the SDK.  Ported from PicoMite (cmake/relocate_gpio_irq_to_ram.cmake
# in the reference tree), where the user's MMBasic firmware does exactly
# this - and it is the piece the counting inputs cannot ship without.
#
# The SDK's shared GPIO interrupt handler, gpio_default_irq_handler() in
# hardware_gpio/gpio.c, is the function that runs from the IO_IRQ_BANK0
# vector and dispatches to countpin.c's callback.  This kernel executes
# from flash, and the whole of the SDK's gpio.c is deliberately IN flash
# (default_text_excludes.incl - it was moved there when FS32 needed the
# RAM).  For setup-speed calls that is right; for a priority-0 handler
# taking an edge per pulse it is not: every fetch would go through the
# QMI that core1's scanout streams PSRAM through.
#
# Mechanism: the SDK compiles with -ffunction-sections, so the handler
# sits in its own input section ".text.gpio_default_irq_handler".  We
# rename that section to ".time_critical.gpio_default_irq_handler" in
# the compiled object at pre-link.  The excludes file's flash glob
# (*hardware_gpio/gpio.c.o(.text*)) no longer matches it, and the SDK
# linker script's RAM .data section collects *(.time_critical*)
# (script_include/section_copy_to_ram_data.incl:23) - exactly as if the
# source used __not_in_flash_func, at a cost of ~100 bytes of RAM for
# the one function instead of ~3K for the object.
#
# Idempotent: on an incremental build where gpio.c was not recompiled
# the section is already renamed and the rename is a no-op.  UNLIKE the
# PicoMite original this version VERIFIES: a silent miss here would put
# the dispatcher back in flash with nothing failing, which is the exact
# failure the relocation exists to prevent.  So after renaming, the
# object must contain the .time_critical section or this dies.
#
# Required -D args: OBJ_ROOT (CMakeFiles/<target>.dir), OBJCOPY, OBJDUMP.

if(NOT OBJCOPY)
    message(FATAL_ERROR "relocate_gpio_irq_to_ram: OBJCOPY not provided")
endif()
if(NOT OBJDUMP)
    message(FATAL_ERROR "relocate_gpio_irq_to_ram: OBJDUMP not provided")
endif()

# .o here (Linux build); the PicoMite original globs .obj (Windows).
file(GLOB_RECURSE _all "${OBJ_ROOT}/*.o" "${OBJ_ROOT}/*.obj")
set(_objs "")
foreach(_o IN LISTS _all)
    if(_o MATCHES "hardware_gpio/gpio\\.c\\.(o|obj)$")
        list(APPEND _objs "${_o}")
    endif()
endforeach()
if(NOT _objs)
    message(FATAL_ERROR
        "relocate_gpio_irq_to_ram: no hardware_gpio/gpio.c.o found under ${OBJ_ROOT}")
endif()

foreach(_o IN LISTS _objs)
    execute_process(
        COMMAND "${OBJCOPY}" --rename-section
            .text.gpio_default_irq_handler=.time_critical.gpio_default_irq_handler
            "${_o}"
        RESULT_VARIABLE _rc)
    if(NOT _rc EQUAL 0)
        message(FATAL_ERROR "relocate_gpio_irq_to_ram: objcopy failed (${_rc}) on ${_o}")
    endif()
    execute_process(
        COMMAND "${OBJDUMP}" -h "${_o}"
        OUTPUT_VARIABLE _hdrs
        RESULT_VARIABLE _rc)
    if(NOT _rc EQUAL 0)
        message(FATAL_ERROR "relocate_gpio_irq_to_ram: objdump failed (${_rc}) on ${_o}")
    endif()
    if(NOT _hdrs MATCHES "\\.time_critical\\.gpio_default_irq_handler")
        message(FATAL_ERROR
            "relocate_gpio_irq_to_ram: ${_o} has no "
            ".time_critical.gpio_default_irq_handler section after rename - "
            "the SDK stopped using -ffunction-sections, or renamed the "
            "handler.  The dispatcher would ship in FLASH.  Fix this "
            "before building a kernel with counting inputs.")
    endif()
endforeach()
