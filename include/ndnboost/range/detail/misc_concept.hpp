// Boost.Range library concept checks
//
//  Copyright Neil Groves 2009. Use, modification and distribution
//  are subject to the Boost Software License, Version 1.0. (See
//  accompanying file LICENSE_1_0.txt or copy at
//  http://www.boost.org/LICENSE_1_0.txt)
//
#ifndef NDNBOOST_RANGE_DETAIL_MISC_CONCEPT_HPP_INCLUDED
#define NDNBOOST_RANGE_DETAIL_MISC_CONCEPT_HPP_INCLUDED

#include <ndnboost/concept_check.hpp>

namespace ndnboost
{
    namespace range_detail
    {
        template<typename T1, typename T2>
        class SameTypeConcept
        {
        public:
            NDNBOOST_CONCEPT_USAGE(SameTypeConcept)
            {
                same_type(a,b);
            }
        private:
            template<typename T> void same_type(T,T) {}
            T1 a;
            T2 b;
        };
    }
}

#endif // include guard
