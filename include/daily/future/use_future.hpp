// ****************************************************************************
// daily/future/use_future.hpp
//
// An extension of std::experimental::use_future that allows using the
// daily::future with user supplied continuations.
// 
// Copyright Chris Glover 2016
//
// Distributed under the Boost Software License, Version 1.0.
// See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt
//
// ****************************************************************************
#pragma once
#ifndef DAILY_FUTURE_USEFUTURE_HPP_
#define DAILY_FUTURE_USEFUTURE_HPP_

#include "daily/future/future.hpp"
#include <utility>

// -----------------------------------------------------------------------------
//
namespace daily
{
	struct use_future_t {} constexpr use_future;

	template <typename... Args>
	class promise_handler
	{
	public:

		typedef promise<Args...> promise_type;

		promise_handler(use_future_t)
		{}

		void operator()(Args... args)
		{
			promise_.set_value(std::move(args)...);
		}
		
		promise_type promise_;
	};

} // daily


namespace std {  namespace experimental 
{
	template<typename R, typename... Args>
	struct handler_type<daily::use_future_t, R(Args...)>
	{
		typedef daily::promise_handler<Args...> type;
	};


	template <typename... Args>
	class async_result<daily::promise_handler<Args...>>
	{
	public:
		typedef daily::promise_handler<Args...> handler_type;
		typedef daily::promise<Args...> promise_type;
		typedef daily::future<Args...> type;

		async_result(handler_type& handler)
			: future_(handler.promise_.get_future())
		{}

		type get() { return std::move(future_); }

	private:

		type future_;
	};
}}

#endif // DAILY_FUTURE_USEFUTURE_HPP_