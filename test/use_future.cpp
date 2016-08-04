// ****************************************************************************
// daily/future/test/use_future.cpp
//
// Copyright Chris Glover 2016
//
// Distributed under the Boost Software License, Version 1.0.
// See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt
//
// ****************************************************************************
#define BOOST_TEST_MODULE Future
#include <boost/test/unit_test.hpp>

#include <experimental/executor>
#include <experimental/thread_pool>
#include <experimental/future>

#include "daily/future/use_future.hpp"

float get_float()
{
	return 1.f;
}

BOOST_AUTO_TEST_CASE( future_use_future_basic )
{
	std::experimental::thread_pool pool;
	auto f = std::experimental::dispatch(
		pool,
		get_float,
		daily::use_future
	);

	BOOST_TEST_CHECK(f.valid() == true);
	auto f2 = f.then(daily::continue_on::get, [](float f) { return f * 2; });
	BOOST_TEST_CHECK(f.valid() == false);
	BOOST_TEST_CHECK(f2.get() == 2.f);
}

BOOST_AUTO_TEST_CASE( future_use_future_throw )
{
	std::experimental::thread_pool pool;
	auto f = std::experimental::dispatch(
		pool,
		get_float,
		daily::use_future
	);

	BOOST_TEST_CHECK(f.valid() == true);
	auto f2 = f.then(
		daily::continue_on::get, 
		[](float f)
		{
			throw std::logic_error("");
			return f * 2; 
		}
	);
	BOOST_TEST_CHECK(f.valid() == false);
	
	bool exception_caught = false;
	try
	{
		f2.get();
	}
	catch(std::logic_error&)
	{
		exception_caught = true;
	}
	BOOST_TEST_CHECK(exception_caught == true);
	BOOST_TEST_CHECK(f2.valid() == false);
}