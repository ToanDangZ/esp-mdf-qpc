#
# Component Makefile
#

COMPONENT_DEPENDS := qpc
CFLAGS += -DQ_SPY
COMPONENT_ADD_INCLUDEDIRS += .

COMPONENT_SRCDIRS := src app/components/test

COMPONENT_PRIV_INCLUDEDIRS := app
