//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2021 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#ifndef RIPPLE_BASICS_RESULT_H_INCLUDED
#define RIPPLE_BASICS_RESULT_H_INCLUDED

#include <ripple/basics/contract.h>
#include <boost/outcome.hpp>
#include <stdexcept>

namespace ripple {

/** Result is a low budget approximation of std::expected (hoped for in C++23)

    The implementation is entirely based on boost::outcome.
*/

// Exception thrown by an invalid access to Result.
struct bad_result_access : public std::runtime_error
{
    bad_result_access() : runtime_error("bad result access")
    {
    }
};

namespace detail {

// Custom policy for Result.  Always throw on an invalid access.
struct throw_policy : public boost::outcome_v2::policy::base
{
    template <class Impl>
    static constexpr void
    wide_value_check(Impl&& self)
    {
        if (!base::_has_value(std::forward<Impl>(self)))
            Throw<bad_result_access>();
    }

    template <class Impl>
    static constexpr void
    wide_error_check(Impl&& self)
    {
        if (!base::_has_error(std::forward<Impl>(self)))
            Throw<bad_result_access>();
    }

    template <class Impl>
    static constexpr void
    wide_exception_check(Impl&& self)
    {
        if (!base::_has_exception(std::forward<Impl>(self)))
            Throw<bad_result_access>();
    }
};

}  // namespace detail

// Definition of Result.
template <class T, class E>
class Result : public boost::outcome_v2::result<T, E, detail::throw_policy>
{
public:
    // Use all of outcome_v2::result's constructors.
    using boost::outcome_v2::result<T, E, detail::throw_policy>::result;

    // Add operator* and operator-> so the Result API looks a bit more like
    // what std::expected is likely to look like.  See:
    // http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2021/p0323r10.html
    [[nodiscard]] T&
    operator*()
    {
        return this->value();
    }

    [[nodiscard]] T const&
    operator*() const
    {
        return this->value();
    }

    [[nodiscard]] T*
    operator->()
    {
        return &this->value();
    }

    [[nodiscard]] T const*
    operator->() const
    {
        return &this->value();
    }
};

}  // namespace ripple

#endif  // RIPPLE_BASICS_RESULT_H_INCLUDED
