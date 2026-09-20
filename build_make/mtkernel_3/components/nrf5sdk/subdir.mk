################################################################################
# micro T-Kernel 3.00.03  makefile
################################################################################

SDK_SRCS = $(wildcard ../components/nrf5sdk/*.c)
TMP_OBJS = $(SDK_SRCS:.c=.o)
TMP_DEPS = $(SDK_SRCS:.c=.d)

SDK_OBJS := $(subst ../, ./mtkernel_3/, $(TMP_OBJS))
OBJS += $(SDK_OBJS)
SDK_DEPS := $(subst ../, ./mtkernel_3/, $(TMP_DEPS))
C_DEPS += $(SDK_DEPS)
INCPATH += -I../components/nrf5sdk

mtkernel_3/components/nrf5sdk/%.o: ../components/nrf5sdk/%.c
	@echo 'Building file: $<'
	$(GCC) $(CFLAGS) -D$(TARGET) $(INCPATH) -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '
