############################################################################
# apps/examples/tp2info/CMakeLists.txt
#
# MODIFICACAO TP2: equivalente CMake do Makefile acima.
############################################################################

if(CONFIG_EXAMPLES_TP2INFO)
  nuttx_add_application(
    NAME
    ${CONFIG_EXAMPLES_TP2INFO_PROGNAME}
    SRCS
    tp2info_main.c
    STACKSIZE
    ${CONFIG_EXAMPLES_TP2INFO_STACKSIZE}
    PRIORITY
    ${CONFIG_EXAMPLES_TP2INFO_PRIORITY}
    MODULE
    ${CONFIG_EXAMPLES_TP2INFO})
endif()
