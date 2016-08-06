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
#include "daily/future/default_allocator.hpp"
#include <utility>

// -----------------------------------------------------------------------------
//
namespace daily
{
    template<typename Allocator = future_default_allocator>
    struct use_future_t
    {
        constexpr use_future_t() noexcept
        {}

        constexpr explicit use_future_t(Allocator alloc) noexcept
            : allocator_(std::move(alloc))
        {}

        Allocator get_allocator() const noexcept
        {
            return allocator_;
        }

    private:

        Allocator allocator_;
    };

#if defined(_MSC_VER)
    __declspec(selectany) use_future_t<> use_future;
#elif __GNUC__ == 6 && __GNUC_MINOR__ == 1
    const use_future_t<> use_future;
#else
    constexpr use_future_t<> use_future;
#endif

    template <typename... Args>
    class promise_handler
    {
    public:

        typedef promise<Args...> promise_type;

        template<typename Allocator>
        promise_handler(use_future_t<Allocator> const& tag)
            : promise_(std::allocator_arg, tag.get_allocator())
        {}

        void operator()(Args... args)
        {
            promise_.set_value(std::move(args)...);
        }
        
        promise_type promise_;
    };

} // namespace daily

// -----------------------------------------------------------------------------
//
namespace std {  namespace experimental 
{
    template<typename Allocator, typename R, typename... Args>
    struct handler_type<daily::use_future_t<Allocator>, R(Args...)>
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