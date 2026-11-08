# Additional clean files
cmake_minimum_required(VERSION 3.16)

if("${CONFIG}" STREQUAL "" OR "${CONFIG}" STREQUAL "")
  file(REMOVE_RECURSE
  "bootloader/bootloader.bin"
  "bootloader/bootloader.elf"
  "bootloader/bootloader.map"
  "config/sdkconfig.cmake"
  "config/sdkconfig.h"
  "encrypted_app-flash_args"
  "encrypted_bootloader-flash_args"
  "encrypted_flash_args"
  "encrypted_flash_args.in"
  "encrypted_partition-table-flash_args"
  "esp-idf/bootloader/encrypted_bootloader-flash_args.in"
  "esp-idf/esptool_py/encrypted_app-flash_args.in"
  "esp-idf/esptool_py/flasher_args.json.in"
  "esp-idf/mbedtls/x509_crt_bundle"
  "esp-idf/partition_table/encrypted_partition-table-flash_args.in"
  "flash_app_args"
  "flash_bootloader_args"
  "flash_encrypted_app_args"
  "flash_encrypted_bootloader_args"
  "flash_encrypted_project_args"
  "flash_project_args"
  "flasher_args.json"
  "hub_p4_esp_hosted_host.bin"
  "hub_p4_esp_hosted_host.map"
  "ldgen_libraries"
  "ldgen_libraries.in"
  "project_elf_src_esp32p4.c"
  "x509_crt_bundle.S"
  )
endif()
