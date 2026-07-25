# Toolchain ###################################################################
# FIXME gcov segfault with clang
TOOLCHAIN ?=
CC        := $(TOOLCHAIN)gcc
#CC        := $(TOOLCHAIN)g++
#CC        := $(TOOLCHAIN)clang-20

AS        := $(TOOLCHAIN)as
LD        := $(TOOLCHAIN)gcc
OBJCOPY   := $(TOOLCHAIN)objcopy
OBJDUMP   := $(TOOLCHAIN)objdump
STRIP     := $(TOOLCHAIN)strip
SIZE      := $(TOOLCHAIN)size

# Flags #######################################################################
#MFLAGS := -mcpu=$(MCPU) -mthumb
#MFLAGS += -mfpu=$(MFPU) -mfloat-abi=hard

# need multilib gcc more libcmocka-dev:i386 
#MFLAGS := -m32

CFLAGS := $(MFLAGS)
