################################################################################
# micro T-Kernel 3.00.03  makefile
################################################################################

SD_SRCS = $(wildcard ../components/softdevice/*.c)
TMP_OBJS = $(SD_SRCS:.c=.o)
TMP_DEPS = $(SD_SRCS:.c=.d)

SD_OBJS := $(subst ../, ./mtkernel_3/, $(TMP_OBJS))
OBJS += $(SD_OBJS)
SD_DEPS := $(subst ../, ./mtkernel_3/, $(TMP_DEPS))
C_DEPS += $(SD_DEPS)
INCPATH += -I../components/softdevice

mtkernel_3/components/softdevice/%.o: ../components/softdevice/%.c
	@echo 'Building file: $<'
	$(GCC) $(CFLAGS) -D$(TARGET) $(INCPATH) -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '

