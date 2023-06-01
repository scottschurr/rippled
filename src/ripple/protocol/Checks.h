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

#ifndef RIPPLE_PROTOCOL_CHECKS_H_INCLUDED
#define RIPPLE_PROTOCOL_CHECKS_H_INCLUDED

#include <ripple/protocol/LedgerEntryWrapper.h>
#include <ripple/protocol/STAccount.h>
#include <ripple/protocol/STAmount.h>

namespace ripple {

template <bool Writable>
class ChecksImpl final : public LedgerEntryWrapper<Writable>
{
private:
    using Base = LedgerEntryWrapper<Writable>;
    using SleT = typename Base::SleT;
    using Base::wrapped_;

    ChecksImpl(std::shared_ptr<SleT>&& w) : Base(std::move(w))
    {
    }

    // Friend declarations of factory functions.
    //
    // For classes that contain factories we must declare the entire class
    // as a friend unless the class declaration is visible at this point.
    friend class ReadView;
    friend class ApplyView;

public:
    // Conversion operator from ChecksImpl<true> to ChecksImpl<false>.
    operator ChecksImpl<true>() const
    {
        return ChecksImpl<false>(
            std::const_pointer_cast<std::shared_ptr<STLedgerEntry const>>(
                wrapped_));
    }

    uint256
    key() const
    {
        return wrapped_->key();
    }

    auto
    getCheckExpiration() const
    {
        return wrapped_->at(~sfExpiration);
    }

    // Keshava: This function is identical to the function
    // STObject::getAccountID ?
    // should I piggy-back on that function instead?
    AccountID
    getCheckCreator() const
    {
        return wrapped_->at(sfAccount);
    }

    AccountID
    getCheckRecipient() const
    {
        return wrapped_->at(sfDestination);
    }

    // Keshava: The below two functions refer to a page in a directory of an
    // account. What is an appropriate name? These function names are cryptic
    // :((
    std::uint64_t
    getDestinationNode() const
    {
        return wrapped_->at(sfDestinationNode);
    }

    std::uint64_t
    getOwnerNode() const
    {
        return wrapped_->at(sfOwnerNode);
    }
};

using Checks = ChecksImpl<true>;
using ChecksRd = ChecksImpl<false>;
}  // namespace ripple

#endif  // RIPPLE_PROTOCOL_CHECKS_H_INCLUDED
