################################################################################
# micro T-Kernel 3.00.03  makefile
################################################################################

OBJS += \
./mtkernel_3/device/blenus/sysdepend/nrf5/blenus_nrf5.o

C_DEPS += \
./mtkernel_3/device/blenus/sysdepend/nrf5/blenus_nrf5.d

# Each subdirectory must supply rules for building sources it contributes
mtkernel_3/device/blenus/sysdepend/nrf5/%.o: ../device/blenus/sysdepend/nrf5/%.c
	@echo 'Building file: $<'
	$(GCC) $(CFLAGS) -D$(TARGET) $(INCPATH) -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '
