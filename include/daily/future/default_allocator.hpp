// ****************************************************************************
// daily/future/default_allocator.hpp
//
// Provides a typedef for a default allocator because std::allocator<void>
// doesn't work on all compilers.
// 
// Copyright Chris Glover 2016
//
// Distributed under the Boost Software License, Version 1.0.
// See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt
//
// ****************************************************************************
#pragma once
#ifndef DAILY_FUTURE_DEFAULTALLOCATOR_HPP_
#define DAILY_FUTURE_DEFAULTALLOCATOR_HPP_

namespace daily { 
#if __GNUC__ == 6 && __GNUC_MINOR__ == 1
    // libstdc++ is broken with allocator void on 6.1
    typedef std::allocator<char> future_default_allocator;
#else
    typedef std::allocator<void> future_default_allocator;
#endif
} // namespace daily { 

#endif // DAILY_FUTURE_DEFAULTALLOCATOR_HPP_