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

#include <ripple/protocol/OwnerDirPage.h>
#include <ripple/protocol/STAccount.h>

namespace ripple {

OwnerDirPage::OwnerDirPage(std::shared_ptr<SLE>&& w) : wrapped_(std::move(w))
{
}

OwnerDirPage::OwnerDirPage(std::shared_ptr<SLE const>&& w)
    : wrapped_(std::const_pointer_cast<SLE>(std::move(w)))
{
}

std::shared_ptr<SLE>
OwnerDirPage::slePtr()
{
    return wrapped_;
}

std::shared_ptr<SLE const>
OwnerDirPage::slePtr() const
{
    return wrapped_;
}

AccountID
OwnerDirPage::ownerID() const
{
    return wrapped_->getAccountID(sfOwner);
}

uint256
OwnerDirPage::rootIndex() const
{
    return wrapped_->getFieldH256(sfRootIndex);
}

STVector256 const&
OwnerDirPage::indexes() const
{
    return wrapped_->getFieldV256(sfIndexes);
}

void
OwnerDirPage::setIndexes(STVector256 const& newIndexes)
{
    wrapped_->setFieldV256(sfIndexes, newIndexes);
}

// Both IndexNext and IndexPrevious use the absence of the field to represent
// zero.  This acts as a kind of compression.  We can hide this compression
// from our callers by doing the right thing regarding the absent field.
[[nodiscard]] static std::uint64_t
getDefaultedIndex(std::shared_ptr<SLE> const& slePtr, SField const& field)
{
    if (slePtr->isFieldPresent(field))
        return slePtr->getFieldU64(field);
    return 0u;
}

static void
setDefaultedIndex(
    std::shared_ptr<SLE> const& slePtr,
    SField const& field,
    std::uint64_t newValue)
{
    if (slePtr->isFieldPresent(field))
        if (newValue != 0u)
            return slePtr->setFieldU64(field, newValue);
    return slePtr->makeFieldAbsent(field);

    if (newValue != 0u)
        slePtr->makeFieldPresent(field);
    slePtr->setFieldU64(field, newValue);
}

std::uint64_t
OwnerDirPage::indexNext() const
{
    return getDefaultedIndex(wrapped_, sfIndexNext);
}

void
OwnerDirPage::setIndexNext(std::uint64_t newIndexNext)
{
    setDefaultedIndex(wrapped_, sfIndexNext, newIndexNext);
}

std::uint64_t
OwnerDirPage::indexPrevious() const
{
    return getDefaultedIndex(wrapped_, sfIndexPrevious);
}

void
OwnerDirPage::setIndexPrevious(std::uint64_t newIndexPrevious)
{
    setDefaultedIndex(wrapped_, sfIndexNext, newIndexPrevious);
}

}  // namespace ripple
