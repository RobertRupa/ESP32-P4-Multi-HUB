# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file LICENSE.rst or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION ${CMAKE_VERSION}) # this file comes with cmake

# If CMAKE_DISABLE_SOURCE_CHANGES is set to true and the source directory is an
# existing directory in our source tree, calling file(MAKE_DIRECTORY) on it
# would cause a fatal error, even though it would be a no-op.
if(NOT EXISTS "/home/konatsu/esp/esp-idf/components/bootloader/subproject")
  file(MAKE_DIRECTORY "/home/konatsu/esp/esp-idf/components/bootloader/subproject")
endif()
file(MAKE_DIRECTORY
  "/home/konatsu/Documents/ESP32-P4 Multi HUB/firmware/c6/build-h2-zigbee-wifi/bootloader"
  "/home/konatsu/Documents/ESP32-P4 Multi HUB/firmware/c6/build-h2-zigbee-wifi/bootloader-prefix"
  "/home/konatsu/Documents/ESP32-P4 Multi HUB/firmware/c6/build-h2-zigbee-wifi/bootloader-prefix/tmp"
  "/home/konatsu/Documents/ESP32-P4 Multi HUB/firmware/c6/build-h2-zigbee-wifi/bootloader-prefix/src/bootloader-stamp"
  "/home/konatsu/Documents/ESP32-P4 Multi HUB/firmware/c6/build-h2-zigbee-wifi/bootloader-prefix/src"
  "/home/konatsu/Documents/ESP32-P4 Multi HUB/firmware/c6/build-h2-zigbee-wifi/bootloader-prefix/src/bootloader-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/home/konatsu/Documents/ESP32-P4 Multi HUB/firmware/c6/build-h2-zigbee-wifi/bootloader-prefix/src/bootloader-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/home/konatsu/Documents/ESP32-P4 Multi HUB/firmware/c6/build-h2-zigbee-wifi/bootloader-prefix/src/bootloader-stamp${cfgdir}") # cfgdir has leading slash
endif()
