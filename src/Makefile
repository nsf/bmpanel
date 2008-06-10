APP := bmpanel
SOURCES := $(wildcard src/*.c)

TARGETS += $(APP)

OBJS := $(patsubst %.c,build/%.o,$(SOURCES))

ifneq ($(UGLY),1)
	V_C = @echo 'C: '$<;
	V_L = @echo 'L: '$@;
	V_S = @echo 'strip: '$@;
endif

$(APP): $(OBJS)
	$(V_L)$(LD) $(LIBS) -o $@ $(OBJS)
ifeq ($(DEBUG),0)
	$(V_S)strip -s $(APP)
endif

build/src/%.o: src/%.c .mk/config.mk
	$(V_C)$(CC) -c -MMD$(CFLAGS) $< -o $@

DEPS += $(patsubst %.o,%.d,$(OBJS))
