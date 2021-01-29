//
// Copyright (c) 2015-2020 Microsoft Corporation and Contributors.
// SPDX-License-Identifier: Apache-2.0
//

#ifdef HAVE_MAT_SHORT_NS
#define MAT_NS_BEGIN  MAT
#define MAT_NS_END
#define PAL_NS_BEGIN  PAL
#define PAL_NS_END
#else
#define MAT_NS_BEGIN  Microsoft { namespace Applications { namespace Events
#define MAT_NS_END    }}
#define MAT           ::Microsoft::Applications::Events
#define PAL_NS_BEGIN  Microsoft { namespace Applications { namespace Events { namespace PlatformAbstraction
#define PAL_NS_END    }}}
#define PAL           ::Microsoft::Applications::Events::PlatformAbstraction
#endif
