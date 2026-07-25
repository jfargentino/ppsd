#LDFLAGS += -specs=nosys.specs
#LDFLAGS += -specs=nano.specs
LDFLAGS+= -Wl,--gc-sections
LDFLAGS+=-Wl,-z,nodlopen -Wl,-z,noexecstack
LDFLAGS+=-Wl,-z,relro -Wl,-z,now -Wl,--as-needed
LDFLAGS+=-Wl,--no-copy-dt-needed-entries
LDFLAGS+=-lrt -lm
LDFLAGS+=-fPIE -pie

#LDFLAGS += -Wl,--start-group -lc -lm -Wl,--end-group

