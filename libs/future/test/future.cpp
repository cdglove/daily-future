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
#define BOOST_TEST_MODULE Future
#include <boost/test/unit_test.hpp>
#include <boost/fusion/container/vector.hpp>
#include <boost/fusion/algorithm/iteration/for_each.hpp>
#include <boost/fusion/container/vector/convert.hpp>
#include <boost/fusion/adapted/mpl.hpp>
#include <boost/mpl/transform.hpp>
#include <boost/mpl/vector.hpp>
#include "daily/future/future.hpp"

template<typename T>
struct make_future
{
	typedef daily::future<T> type;
};

template<typename T>
struct make_promise
{
	typedef daily::promise<T> type;
};

// Types for all specializations.
typedef boost::mpl::vector<
	int, 
	int&, 
	void
> all_types;

typedef boost::fusion::result_of::as_vector<
	typename boost::mpl::transform<all_types, make_future<boost::mpl::_1>>::type
>::type future_types;

typedef boost::fusion::result_of::as_vector<
	typename boost::mpl::transform<all_types, make_promise<boost::mpl::_1>>::type
>::type promise_types;

BOOST_AUTO_TEST_CASE( future_initial_state )
{
	boost::fusion::for_each(
		future_types(),
		[](auto&& f)
		{
			BOOST_TEST_CHECK(f.valid() == false);
		}
	);
}

BOOST_AUTO_TEST_CASE( promise_initial_state )
{
	promise_types promises;
	boost::fusion::for_each(
		promises,
		[](auto&& p)
		{
			auto f = p.get_future();
			BOOST_TEST_CHECK(f.valid() == true);
		}
	);
}

BOOST_AUTO_TEST_CASE( promise_future_communication )
{
	{
		typedef int T;
		daily::promise<T> p;
		daily::future<T> f = p.get_future();
		p.set_value(1);
		BOOST_TEST_CHECK(f.valid() == true);
		BOOST_TEST_CHECK(f.get() == 1);
		BOOST_TEST_CHECK(f.valid() == false);
	}

	{
		typedef int& T;
		daily::promise<T> p;
		daily::future<T> f = p.get_future();
		int result = 1;
		p.set_value(result);
		BOOST_TEST_CHECK(f.valid() == true);
		BOOST_TEST_CHECK(&f.get() == &result);
		BOOST_TEST_CHECK(f.valid() == false);
	}

	{
		typedef void T;
		daily::promise<T> p;
		daily::future<T> f = p.get_future();
		p.set_value();
		BOOST_TEST_CHECK(f.valid() == true);
		f.get();
		BOOST_TEST_CHECK(f.valid() == false);
	}
}

BOOST_AUTO_TEST_CASE( promise_move_semantics )
{
	{
		typedef int T;
		daily::promise<T> p;
		daily::future<T> f = p.get_future();
		daily::promise<T> p2 = std::move(p);
		p2.set_value(1);
		BOOST_TEST_CHECK(f.valid() == true);
		BOOST_TEST_CHECK(f.get() == 1);
		BOOST_TEST_CHECK(f.valid() == false);
	}

	{
		typedef int& T;
		daily::promise<T> p;
		daily::future<T> f = p.get_future();
		daily::promise<T> p2 = std::move(p);
		int result = 1;
		p2.set_value(result);
		BOOST_TEST_CHECK(f.valid() == true);
		BOOST_TEST_CHECK(&f.get() == &result);
		BOOST_TEST_CHECK(f.valid() == false);
	}

	{
		typedef void T;
		daily::promise<T> p;
		daily::future<T> f = p.get_future();
		daily::promise<T> p2 = std::move(p);
		p2.set_value();
		BOOST_TEST_CHECK(f.valid() == true);
		f.get();
		BOOST_TEST_CHECK(f.valid() == false);
	}
}

BOOST_AUTO_TEST_CASE( future_move_semantics )
{
	{
		typedef int T;
		daily::promise<T> p;
		daily::future<T> f = p.get_future();
		daily::future<T> f2 = std::move(f);
		BOOST_TEST_CHECK(f2.valid() == true);
		BOOST_TEST_CHECK(f.valid() == false);
		p.set_value(3);
		BOOST_TEST_CHECK(f2.get() == 3);
		BOOST_TEST_CHECK(f2.valid() == false);
	}

	{
		typedef int& T;
		daily::promise<T> p;
		daily::future<T> f = p.get_future();
		daily::future<T> f2 = std::move(f);
		BOOST_TEST_CHECK(f2.valid() == true);
		BOOST_TEST_CHECK(f.valid() == false);
		int result = 3;
		p.set_value(result);
		BOOST_TEST_CHECK(f2.get() == 3);
		BOOST_TEST_CHECK(f2.valid() == false);
	}

	{
		typedef void T;
		daily::promise<T> p;
		daily::future<T> f = p.get_future();
		daily::future<T> f2 = std::move(f);
		BOOST_TEST_CHECK(f2.valid() == true);
		BOOST_TEST_CHECK(f.valid() == false);
		p.set_value();
		f2.get();
		BOOST_TEST_CHECK(f2.valid() == false);
	}
}

