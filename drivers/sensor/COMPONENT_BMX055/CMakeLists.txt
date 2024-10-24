# Copyright (c) 2020 ARM Limited. All rights reserved.
# SPDX-License-Identifier: Apache-2.0

add_library(bmx055 STATIC EXCLUDE_FROM_ALL)

target_include_directories(bmx055
    PUBLIC
        .
)

target_sources(bmx055
    PRIVATE
        BMX055.cpp
)

target_link_libraries(bmx055 PUBLIC mbed-core-flags)
