// Copyright (C) 2004 Arkadiy Vertleyb
// Copyright (C) 2004 Peder Holt
// Use, modification and distribution is subject to the Boost Software
// License, Version 1.0. (http://www.boost.org/LICENSE_1_0.txt)

#ifndef NDNBOOST_TYPEOF_REGISTER_FUNDAMENTAL_HPP_INCLUDED
#define NDNBOOST_TYPEOF_REGISTER_FUNDAMENTAL_HPP_INCLUDED

#include <ndnboost/typeof/typeof.hpp>

#include NDNBOOST_TYPEOF_INCREMENT_REGISTRATION_GROUP()

NDNBOOST_TYPEOF_REGISTER_TYPE(unsigned char)
NDNBOOST_TYPEOF_REGISTER_TYPE(unsigned short)
NDNBOOST_TYPEOF_REGISTER_TYPE(unsigned int)
NDNBOOST_TYPEOF_REGISTER_TYPE(unsigned long)

NDNBOOST_TYPEOF_REGISTER_TYPE(signed char)
NDNBOOST_TYPEOF_REGISTER_TYPE(signed short)
NDNBOOST_TYPEOF_REGISTER_TYPE(signed int)
NDNBOOST_TYPEOF_REGISTER_TYPE(signed long)

NDNBOOST_TYPEOF_REGISTER_TYPE(bool)
NDNBOOST_TYPEOF_REGISTER_TYPE(char)

NDNBOOST_TYPEOF_REGISTER_TYPE(float)
NDNBOOST_TYPEOF_REGISTER_TYPE(double)
NDNBOOST_TYPEOF_REGISTER_TYPE(long double)

#ifndef NDNBOOST_NO_INTRINSIC_WCHAR_T
// If the following line fails to compile and you're using the Intel
// compiler, see http://lists.boost.org/MailArchives/boost-users/msg06567.php,
// and define NDNBOOST_NO_INTRINSIC_WCHAR_T on the command line.
NDNBOOST_TYPEOF_REGISTER_TYPE(wchar_t)
#endif

#if (defined(NDNBOOST_MSVC) && (NDNBOOST_MSVC == 1200)) \
    || (defined(NDNBOOST_INTEL_CXX_VERSION) && defined(_MSC_VER) && (NDNBOOST_INTEL_CXX_VERSION <= 600)) \
    || (defined(__BORLANDC__) && (__BORLANDC__ == 0x600) && (_MSC_VER == 1200))
NDNBOOST_TYPEOF_REGISTER_TYPE(unsigned __int8)
NDNBOOST_TYPEOF_REGISTER_TYPE(__int8)
NDNBOOST_TYPEOF_REGISTER_TYPE(unsigned __int16)
NDNBOOST_TYPEOF_REGISTER_TYPE(__int16)
NDNBOOST_TYPEOF_REGISTER_TYPE(unsigned __int32)
NDNBOOST_TYPEOF_REGISTER_TYPE(__int32)
#ifdef __BORLANDC__
NDNBOOST_TYPEOF_REGISTER_TYPE(unsigned __int64)
NDNBOOST_TYPEOF_REGISTER_TYPE(__int64)
#endif
#endif

# if defined(NDNBOOST_HAS_LONG_LONG)
NDNBOOST_TYPEOF_REGISTER_TYPE(::ndnboost::ulong_long_type)
NDNBOOST_TYPEOF_REGISTER_TYPE(::ndnboost::long_long_type)
#elif defined(NDNBOOST_HAS_MS_INT64)
NDNBOOST_TYPEOF_REGISTER_TYPE(unsigned __int64)
NDNBOOST_TYPEOF_REGISTER_TYPE(__int64)
#endif

NDNBOOST_TYPEOF_REGISTER_TYPE(void)

#endif//NDNBOOST_TYPEOF_REGISTER_FUNDAMENTAL_HPP_INCLUDED
