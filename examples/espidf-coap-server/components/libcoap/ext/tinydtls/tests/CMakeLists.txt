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

project(tinydtls-tests)

add_executable(dtls-server dtls-server.c)
target_link_libraries(dtls-server LINK_PUBLIC tinydtls)
target_compile_options(dtls-server PUBLIC -DTEST_INCLUDE -DDTLSv12 -DWITH_SHA256)

add_executable(ccm-test ccm-test.c)
target_link_libraries(ccm-test LINK_PUBLIC tinydtls)
target_compile_options(ccm-test PUBLIC -DTEST_INCLUDE -DDTLSv12 -DWITH_SHA256)

add_executable(dtls-client dtls-client.c)
target_link_libraries(dtls-client LINK_PUBLIC tinydtls)
target_compile_options(dtls-client PUBLIC -DTEST_INCLUDE -DDTLSv12 -DWITH_SHA256)

