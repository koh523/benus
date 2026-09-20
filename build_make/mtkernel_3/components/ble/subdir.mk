################################################################################
# micro T-Kernel 3.00.03  makefile
################################################################################

BLE_SRCS = $(wildcard ../components/ble/*.c)
TMP_OBJS = $(BLE_SRCS:.c=.o)
TMP_DEPS = $(BLE_SRCS:.c=.d)

BLE_OBJS := $(subst ../, ./mtkernel_3/, $(TMP_OBJS))
OBJS += $(BLE_OBJS)
BLE_DEPS := $(subst ../, ./mtkernel_3/, $(TMP_DEPS))
C_DEPS += $(BLE_DEPS)
INCPATH += -I../components/ble

mtkernel_3/components/ble/%.o: ../components/ble/%.c
	@echo 'Building file: $<'
	$(GCC) $(CFLAGS) -D$(TARGET) $(INCPATH) -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '
