###############################################################################
#
#  Copyright (c) 2022 Contributors to the Eclipse Foundation
#
#  See the LICENSE file(s) distributed with this work for additional
#  information regarding copyright ownership.
#
#  This program and the accompanying materials are made available under the
#  terms of the Eclipse Public License 1.0 
#  which is available at https://www.eclipse.org/legal/epl-v10.html
#  and the Eclipse Distribution License v. 1.0
#  available at http://www.eclipse.org/org/documents/edl-v10.php
#
#  SPDX-License-Identifier: EPL-1.0
#
#  Contributors:
#     Jimmy Björklund  - initial version
#     Achim Kraus      - minor fixes
#
###############################################################################

cmake_minimum_required(VERSION 3.5)

project(tinydtls)

include (AutoConf.cmake)

option(BUILD_SHARED_LIBS "Link using shared libs" OFF)
option(make_tests "Make test programs and examples" OFF)

if(NOT PLATFORM)
   # PLATFORM seems to be not used
   set(PLATFORM "posix" CACHE STRING "Choose platform." FORCE)
   set_property(CACHE PLATFORM PROPERTY STRINGS "contiki" "espidf" "posix" "riot" "zephyr")
endif()

set(PACKAGE_NAME "tinydtls")
set(PACKAGE_VERSION "0.8.6" )
set(SOVERSION "0" )
 
option(DTLS_ECC "disable/enable support for TLS_ECDHE_ECDSA_WITH_AES_128_CCM_8" ON )
option(DTLS_PSK "disable/enable support for TLS_PSK_WITH_AES_128_CCM_8" ON)

configure_file(dtls_config.h.cmake.in dtls_config.h )

add_library(tinydtls)

target_sources(tinydtls PRIVATE 
   dtls.c
   netq.c
   peer.c
   session.c
   crypto.c
   ccm.c
   hmac.c
   dtls_time.c
   dtls_debug.c
   dtls_prng.c
   aes/rijndael.c
   aes/rijndael_wrap.c
   sha2/sha2.c
   ecc/ecc.c)

target_include_directories(tinydtls PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR})
target_compile_definitions(tinydtls PUBLIC DTLSv12 WITH_SHA256 SHA2_USE_INTTYPES_H DTLS_CHECK_CONTENTTYPE)

if(NOT ZEPHYR_BASE)
   target_compile_options(tinydtls PRIVATE -fPIC -pedantic -std=c99 -Wall -Wextra -Wformat-security -Winline -Wmissing-declarations -Wmissing-prototypes -Wnested-externs -Wpointer-arith -Wshadow -Wstrict-prototypes -Wswitch-default -Wswitch-enum -Wunused)
endif()

set_target_properties(tinydtls PROPERTIES VERSION ${PACKAGE_VERSION} SOVERSION ${SOVERSION})

if( ${make_tests} )
    add_subdirectory(tests)
endif()

if(BUILD_SHARED_LIBS)
    install(TARGETS    tinydtls       LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} )
else()
    install(TARGETS    tinydtls       DESTINATION ${CMAKE_INSTALL_LIBDIR} )
endif()

file(CONFIGURE OUTPUT .gitignore
  NEWLINE_STYLE UNIX
  CONTENT "*")
