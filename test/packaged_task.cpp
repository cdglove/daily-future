// ****************************************************************************
// daily/future/test/compat.cpp
//
// Copyright Chris Glover 2016
//
// Distributed under the Boost Software License, Version 1.0.
// See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt
//
// ****************************************************************************
#define BOOST_TEST_MODULE PackagedTask
#include <boost/test/unit_test.hpp>
#include "daily/future/future.hpp"

BOOST_AUTO_TEST_CASE( packaged_task )
{
	daily::packaged_task<int(int)> pt([](int i) { return i * 2; });
	daily::future<int> f = pt.get_future();
	pt(5);
	BOOST_TEST_CHECK(f.get() == 10);
} 