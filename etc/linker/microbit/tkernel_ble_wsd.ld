/*
 * tkernel_ble_wsd.ld
 *
 * Based on tkernel_map.ld from PMC's 362_mbit_mtk3 (μT-Kernel 3.0 for micro:bit v2,
 * distributed by Personal Media Corporation).
 *
 * Modified by koh aiaida (koh@aiaida.jp), 2025-2026:
 *   - Add memory regions for MBR / SoftDevice (SD)
 *   - Add SoftDevice-related sections (sdh_*_observers, etc.)
 *   - Rename the .data / .bss symbols to __data_start__ / _bss_start
 *
 * The modified parts above are provided by koh aiaida under Apache-2.0. The base
 * tkernel_map.ld is subject to PMC's distribution terms (T-License 2.x).
 */

ENTRY(Reset_Handler)

/* Memories definition */
MEMORY
{
  MBR (rx) : ORIGIN = 0x0000, LENGTH = 0x1000
  SD (rx) : ORIGIN = 0x1000, LENGTH = 0x1B000
  FLASH (rx) : ORIGIN = 0x1c000, LENGTH = 0x64000 /* Adjust for specific chip e.g. nRF52833 */
  /* S113 + this BLE cfg needs app RAM >= 0x20002608. VTOR must be 256-byte aligned. */
  RAM (rwx)  : ORIGIN = 0x20002700, LENGTH = 0x1d900
}

/*
 * Section Definitions
 */
SECTIONS
{
  . = ALIGN(4);
  .mbr :
  {
      KEEP(*(.mbr))
  } > MBR
  
  . = ALIGN(4);
  .softdevice :
  {
      KEEP(*(.softdevice))
  } > SD  
}

SECTIONS 
{
  .vectors :
  {
    __vector_org = .;
    KEEP(*(.vector))
  } > FLASH

	.text : 
  {
		. = ALIGN(8);
		__start = .;
    _start = .;
		*(.text)
		*(.text.*)
		*(.rodata)
		*(.rodata.*)
    KEEP(*(.group))
	} >FLASH
}


SECTIONS
{
  . = ALIGN(4);
  .mem_section_dummy_rom :
  {
  }
  .sdh_soc_observers :
  {
    . = ALIGN(4);
    PROVIDE(__start_sdh_soc_observers = .);
    KEEP(*(SORT(.sdh_soc_observers*)))
    PROVIDE(__stop_sdh_soc_observers = .);
  } > FLASH
  .sdh_ble_observers :
  {
    . = ALIGN(4);
    PROVIDE(__start_sdh_ble_observers = .);
    KEEP(*(SORT(.sdh_ble_observers*)))
    PROVIDE(__stop_sdh_ble_observers = .);
  } > FLASH
  .pwr_mgmt_data :
  {
    PROVIDE(__start_pwr_mgmt_data = .);
    KEEP(*(SORT(.pwr_mgmt_data*)))
    PROVIDE(__stop_pwr_mgmt_data = .);
  } > FLASH
  .sdh_req_observers :
  {
    PROVIDE(__start_sdh_req_observers = .);
    KEEP(*(SORT(.sdh_req_observers*)))
    PROVIDE(__stop_sdh_req_observers = .);
  } > FLASH
  .sdh_state_observers :
  {
    PROVIDE(__start_sdh_state_observers = .);
    KEEP(*(SORT(.sdh_state_observers*)))
    PROVIDE(__stop_sdh_state_observers = .);
  } > FLASH
  .sdh_stack_observers :
  {
    PROVIDE(__start_sdh_stack_observers = .);
    KEEP(*(SORT(.sdh_stack_observers*)))
    PROVIDE(__stop_sdh_stack_observers = .);
  } > FLASH
    .nrf_queue :
  {
    PROVIDE(__start_nrf_queue = .);
    KEEP(*(.nrf_queue))
    PROVIDE(__stop_nrf_queue = .);
  } > FLASH
    .nrf_balloc :
  {
    PROVIDE(__start_nrf_balloc = .);
    KEEP(*(.nrf_balloc))
    PROVIDE(__stop_nrf_balloc = .);
  } > FLASH
    .cli_command :
  {
    PROVIDE(__start_cli_command = .);
    KEEP(*(.cli_command))
    PROVIDE(__stop_cli_command = .);
  } > FLASH
  .crypto_data :
  {
    PROVIDE(__start_crypto_data = .);
    KEEP(*(SORT(.crypto_data*)))
    PROVIDE(__stop_crypto_data = .);
  } > FLASH
  .log_const_data :
  {
    PROVIDE(__start_log_const_data = .);
    KEEP(*(SORT(.log_const_data*)))
    PROVIDE(__stop_log_const_data = .);
  } > FLASH
  .log_backends :
  {
    PROVIDE(__start_log_backends = .);
    KEEP(*(SORT(.log_backends*)))
    PROVIDE(__stop_log_backends = .);
  } > FLASH
  . = ALIGN(4);
  __data_org = .;
} INSERT AFTER .text


SECTIONS {
	vector ORIGIN(RAM) (NOLOAD) :
  {
    . = ALIGN(4);
		__vector_start = .;
		*(.data_vector)
		__vector_end = .;
	} >RAM

	.data : AT(__data_org)
  {
		__data_start__ = .;
    __data_start = .;
		*(.data)
		*(.data.*)
    *(.gnu.linkonce.d.*)
		. = ALIGN(4);
		__data_end = .;
	} >RAM

  /* BSS section (zero-initialized data) in RAM */
  .bss (NOLOAD) :
  {
    . = ALIGN(4);
    __bss_start__ = .;
    _bss_start = .;
    PROVIDE (__noinit_start = .);
		*(.noinit)
		PROVIDE (__noinit_end = .);
    KEEP(*(.bss))
    KEEP(*(.bss.*))
    *(.gnu.linkonce.b.*)
    *(COMMON)
    __bss_end__ = .;
    __bss_end = .;
  } > RAM
}
