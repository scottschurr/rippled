//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2023 Ripple Labs Inc.

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

#ifndef RIPPLE_PROTOCOL_MULTI_SIGNERS_H_INCLUDED
#define RIPPLE_PROTOCOL_MULTI_SIGNERS_H_INCLUDED

#include <ripple/protocol/LedgerEntryWrapper.h>

namespace ripple {

template <bool Writable>
class SignersImpl final : public LedgerEntryWrapper<Writable>
{
private:
    using Base = LedgerEntryWrapper<Writable>;
    using SleT = typename Base::SleT;
    using Base::wrapped_;

    // This constructor is private so only the factory functions can
    // construct an SignersImpl.
    SignersImpl(std::shared_ptr<SleT>&& w) : Base(std::move(w))
    {
    }

    // Friend declarations of factory functions.
    //
    // For classes that contain factories we must declare the entire class
    // as a friend unless the class declaration is visible at this point.
    friend class ReadView;
    friend class ApplyView;

public:
    // Conversion operator from SignersImpl<true> to SignersImpl<false>.
    operator SignersImpl<true>() const
    {
        return SignersImpl<false>(
            std::const_pointer_cast<std::shared_ptr<STLedgerEntry const>>(
                wrapped_));
    }

    [[nodiscard]] Json::Value
    getJson(JsonOptions options) const
    {
        return wrapped_->getJson(options);
    }

    [[nodiscard]] uint256 const&
    key() const
    {
        return wrapped_->key();
    }
};

using SignersRd = SignersImpl<false>;
using Signers = SignersImpl<true>;

}  // namespace ripple

#endif  // RIPPLE_PROTOCOL_MULTI_SIGNERS_H_INCLUDED
