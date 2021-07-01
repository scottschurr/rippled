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

#include <ripple/ledger/OwnerDirPageIter.h>

namespace ripple {

OwnerDirPage
OwnerDirPageIter::makeOwnerDirPage(std::shared_ptr<SLE> slePtr)
{
    return OwnerDirPage(std::move(slePtr));
}

void
OwnerDirPageIter::setOwnerDirPageIfOkay(
    std::shared_ptr<SLE> slePtr,
    std::uint64_t pageIndex)
{
    // If slePtr is a valid owner directory page use it.
    if (slePtr && slePtr->getFieldU16(sfLedgerEntryType) == ltDIR_NODE &&
        slePtr->isFieldPresent(sfOwner))
    {
        ownerDirPage_.emplace(makeOwnerDirPage(std::move(slePtr)));
        pageIndex_ = pageIndex;
        return;
    }

    // The slePtr is not a valid owner directory page.  Become end().
    ownerDirPage_.reset();
    pageIndex_ = endPageIndex;
}

OwnerDirPageIter
OwnerDirPageIter::begin(ApplyView& view, AccountID const& ownerID)
{
    OwnerDirPageIter ret(view, ownerID);
    ret.setOwnerDirPageIfOkay(view.peek(ret.ownerDirKeylet_), 0u);
    return ret;
}

OwnerDirPageIter
OwnerDirPageIter::end(ApplyView& view, AccountID const& ownerID)
{
    OwnerDirPageIter ret(view, ownerID);
    return ret;
}

OwnerDirPageIter
OwnerDirPageIter::page(
    ApplyView& view,
    AccountID const& ownerID,
    std::uint64_t pageIndex)
{
    OwnerDirPageIter ret(view, ownerID);
    {
        Keylet key = ret.ownerDirKeylet_;
        if (pageIndex != 0)
            key = keylet::page(key, pageIndex);

        ret.setOwnerDirPageIfOkay(view.peek(key), pageIndex);
    }
    return ret;
}

OwnerDirPageIter::reference
OwnerDirPageIter::operator*()
{
    if (!ownerDirPage_)
        Throw<std::out_of_range>("Invalid OwnerDirPageIter access");
    return *ownerDirPage_;
}

OwnerDirPageIter::pointer
OwnerDirPageIter::operator->()
{
    if (!ownerDirPage_)
        Throw<std::out_of_range>("Invalid OwnerDirPageIter access");
    return &(*ownerDirPage_);
}

OwnerDirPageIter&
OwnerDirPageIter::operator++()
{
    // If we're already end(), then stay put.
    if (pageIndex_ == endPageIndex)
        return *this;

    std::uint64_t const next = ownerDirPage_->indexNext();

    // If next == 0 we've incrementing past the last page on the list.
    // Become end().
    if (next == 0)
    {
        ownerDirPage_.reset();
        pageIndex_ = endPageIndex;
        return *this;
    }

    setOwnerDirPageIfOkay(
        view_.peek(keylet::page(ownerDirKeylet_, next)), next);

    return *this;
}

OwnerDirPageIter&
OwnerDirPageIter::operator--()
{
    // A bidirectional iterator needs to be able to get to the last element
    // from an end() iterator.
    if (pageIndex_ == endPageIndex)
    {
        auto sle = view_.peek(ownerDirKeylet_);
        if (sle && sle->getFieldU16(sfLedgerEntryType) == ltDIR_NODE &&
            sle->isFieldPresent(sfOwner))
        {
            // Looking at the owner root page, sfIndexPrevious is the
            // index of the last page in the directory.
            std::uint64_t newIndex = 0u;
            std::uint64_t const prev = sle->getFieldU64(sfIndexPrevious);

            // Many directory structures have a single (root) page.  Only
            // look up a new SLE if it's necessary.
            if (prev != newIndex)
            {
                newIndex = prev;
                sle = view_.peek(keylet::page(ownerDirKeylet_, newIndex));
            }
            setOwnerDirPageIfOkay(sle, newIndex);
        }
        return *this;
    }

    // If we get here this was not an end() iterator, so ownerDirPage_
    // should have a valid value.
    assert(ownerDirPage_);

    std::uint64_t const prev = ownerDirPage_->indexPrevious();

    // If someone is decrementing to index zero then the request for the
    // SLE is a little different.
    if (prev == 0u && pageIndex_ != 0)
        setOwnerDirPageIfOkay(view_.peek(ownerDirKeylet_), 0u);

    // If someone tries to decrement before begin(), then stay at begin().
    else if (prev < pageIndex_)
        setOwnerDirPageIfOkay(
            view_.peek(keylet::page(ownerDirKeylet_, prev)), prev);

    return *this;
}

OwnerDirPageIter
OwnerDirPageIter::operator++(int)
{
    OwnerDirPageIter ret = *this;
    ++(*this);
    return ret;
}

OwnerDirPageIter
OwnerDirPageIter::operator--(int)
{
    OwnerDirPageIter ret = *this;
    --(*this);
    return ret;
}

}  // namespace ripple
