//
// Copyright (c) 2015-2020 Microsoft Corporation and Contributors.
// SPDX-License-Identifier: Apache-2.0
//
#ifndef MAT_VERSION_HPP
#define MAT_VERSION_HPP
// WARNING: DO NOT MODIFY THIS FILE!
// This file has been automatically generated, manual changes will be lost.
#define BUILD_VERSION_STR "3.5.25.1"
#define BUILD_VERSION 3,5,25,1

#ifndef RESOURCE_COMPILER_INVOKED
#include <stdint.h>

#include "CTMacros.h"

#define MAT_v1        ::Microsoft::Applications::Telemetry

namespace MAT_NS_BEGIN {

uint64_t const Version =
    ((uint64_t)3 << 48) |
    ((uint64_t)5 << 32) |
    ((uint64_t)25 << 16) |
    ((uint64_t)1);

} MAT_NS_END

namespace PAL_NS_BEGIN { } PAL_NS_END

#endif // RESOURCE_COMPILER_INVOKED
#endif