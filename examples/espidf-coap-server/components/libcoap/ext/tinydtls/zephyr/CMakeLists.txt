###############################################################################
#
#  Copyright (c) 2022 Contributors to the Eclipse Foundation
#
#  See the LICENSE file(s) distributed with this work for additional
#  information regarding copyright ownership.
#
#  This program and the accompanying materials are made available under the
#  terms of the Eclipse Public License 1.0 
#  which is available at http://www.eclipse.org/legal/epl-v10.html
#  and the Eclipse Distribution License v. 1.0
#  available at http://www.eclipse.org/org/documents/edl-v10.php
#
#  SPDX-License-Identifier: EPL-1.0
#
#  Contributors:
#     Achim Kraus - initial build file for supmodule
#
###############################################################################

cmake_minimum_required(VERSION 3.20.0)

if(CONFIG_LIBTINYDTLS)
  enable_language(C)
  # TEST_BIG_ENDIAN doesn't work for zephyr/ncs 1.9.1
  if(${ARCH} STREQUAL "arm")
     set(CMAKE_C_BYTE_ORDER LITTLE_ENDIAN)
  endif()
  if(CONFIG_LIBTINYDTLS_PSK)
    set(DTLS_PSK On)
  else()
    set(DTLS_PSK Off)
  endif()
  if(CONFIG_LIBTINYDTLS_ECDHE_ECDSA)
    set(DTLS_ECC On)
  else()
    set(DTLS_ECC Off)
  endif()
  add_subdirectory(.. build)
  target_compile_definitions(tinydtls PUBLIC WITH_ZEPHYR)
  target_link_libraries(tinydtls PUBLIC zephyr_interface)
  set_property(GLOBAL APPEND PROPERTY ZEPHYR_INTERFACE_LIBS tinydtls)
endif()
