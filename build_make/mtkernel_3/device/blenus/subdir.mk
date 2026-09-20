################################################################################
# micro T-Kernel 3.00.03  makefile
################################################################################

OBJS += \
./mtkernel_3/device/blenus/blenus.o 

C_DEPS += \
./mtkernel_3/device/blenus/blenus.d 

mtkernel_3/device/blenus/%.o: ../device/blenus/%.c
	@echo 'Building file: $<'
	$(GCC) $(CFLAGS) -D$(TARGET) $(INCPATH) -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '

ifeq ($(TARGET), _MICROBIT_)
-include mtkernel_3/device/blenus/sysdepend/nrf5/subdir.mk
endif
