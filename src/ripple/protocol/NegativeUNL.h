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

#ifndef RIPPLE_PROTOCOL_NEGATIVE_UNL_H_INCLUDED
#define RIPPLE_PROTOCOL_NEGATIVE_UNL_H_INCLUDED

#include <ripple/protocol/LedgerEntryWrapper.h>
#include <ripple/protocol/STArray.h>

namespace ripple {

template <bool Writable>
class NegUNLImpl final : public LedgerEntryWrapper<Writable>
{
private:
    using Base = LedgerEntryWrapper<Writable>;
    using SleT = typename Base::SleT;
    using Base::wrapped_;

    // Friend declarations of factory functions.
    //
    // For classes that contain factories we must declare the entire class
    // as a friend unless the class declaration is visible at this point.
    friend class ReadView;
    friend class ApplyView;

public:
    // This constructor is used in Change::applyUNLModify() function.
    NegUNLImpl(std::shared_ptr<SleT>&& w) : Base(std::move(w))
    {
    }

    // Conversion operator from NegUNLImpl<true> to NegUNLImpl<false>.
    operator NegUNLImpl<true>() const
    {
        return NegUNLImpl<false>(
            std::const_pointer_cast<std::shared_ptr<STLedgerEntry const>>(
                wrapped_));
    }

    [[nodiscard]] bool
    isFieldPresent(SField const& field) const
    {
        return wrapped_->isFieldPresent(field);
    }

    [[nodiscard]] Blob
    getFieldVL(SField const& field) const
    {
        return wrapped_->getFieldVL(field);
    }

    void
    setFieldVL(SField const& field, Slice const& sl) const
    {
        return wrapped_->setFieldVL(field, sl);
    }

    void
    setFieldVL(SField const& field, Blob const& blob) const
    {
        return wrapped_->setFieldVL(field, blob);
    }

    [[nodiscard]] const ripple::STArray&
    getFieldArray(SField const& field) const
    {
        return wrapped_->getFieldArray(field);
    }
};

using NegUNL = NegUNLImpl<true>;
using NegUNLRd = NegUNLImpl<false>;

}  // namespace ripple

#endif  // RIPPLE_PROTOCOL_NEGATIVE_UNL_H_INCLUDED
