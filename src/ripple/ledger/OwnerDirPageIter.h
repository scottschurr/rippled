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

#ifndef RIPPLE_LEDGER_OWNER_DIR_PAGE_ITER_H_INCLUDED
#define RIPPLE_LEDGER_OWNER_DIR_PAGE_ITER_H_INCLUDED

#include <ripple/ledger/ApplyView.h>
#include <ripple/protocol/OwnerDirPage.h>
#include <memory>
#include <type_traits>

namespace ripple {

template <bool Const>
class OwnerDirPageIterImpl
{
    constexpr static std::uint64_t endPageIndex =
        std::numeric_limits<std::uint64_t>::max();

    using ViewT = typename std::conditional_t<Const, ReadView const, ApplyView>;
    using SleT = typename std::conditional_t<Const, SLE const, SLE>;
    using OwnerDirPageT =
        typename std::conditional_t<Const, OwnerDirPage const, OwnerDirPage>;

    ViewT& view_;
    AccountID const& ownerID_;
    Keylet const ownerDirKeylet_;
    std::uint64_t pageIndex_;
    std::optional<OwnerDirPageT> ownerDirPage_;

    // Private constructor used by begin(), end(), and page().
    OwnerDirPageIterImpl(ViewT& view, AccountID const& ownerID)
        : view_(view)
        , ownerID_(ownerID)
        , ownerDirKeylet_(keylet::ownerDir(ownerID))
        , pageIndex_(endPageIndex)
    {
    }

    // Private function that returns a SLE given a View and a Keylet.
    static std::shared_ptr<SleT>
    getSle(ViewT& view, Keylet const& keylet)
    {
        if constexpr (Const)
            return view.read(keylet);
        else
            return view.peek(keylet);
    }

    // Private function that produces an OwnerDirPage from a SLE
    static OwnerDirPageT
    makeOwnerDirPage(std::shared_ptr<SleT> slePtr);

    // Only set the ownerDirPage_ member if the passed SLE is valid.
    void
    setOwnerDirPageIfOkay(
        std::shared_ptr<SleT> slePtr,
        std::uint64_t pageIndex);

public:
    using iterator_category = std::bidirectional_iterator_tag;
    using difference_type = std::ptrdiff_t;
    using value_type = OwnerDirPageT;
    using pointer = OwnerDirPageT*;
    using reference = OwnerDirPageT&;

    [[nodiscard]] static OwnerDirPageIterImpl
    begin(ViewT& view, AccountID const& ownerID);

    [[nodiscard]] static OwnerDirPageIterImpl
    end(ViewT& view, AccountID const& ownerID);

    [[nodiscard]] static OwnerDirPageIterImpl
    page(ViewT& view, AccountID const& ownerID, std::uint64_t pageIndex);

    [[nodiscard]] AccountID const&
    ownerID() const
    {
        return ownerID_;
    }

    [[nodiscard]] Keylet const&
    ownerRootKeylet() const
    {
        return ownerDirKeylet_;
    }

    [[nodiscard]] std::optional<std::uint64_t>
    pageIndex() const
    {
        return isEnd() ? std::nullopt : std::optional(pageIndex_);
    }

    [[nodiscard]] bool
    isEnd() const
    {
        return pageIndex_ == endPageIndex;
    }

    [[nodiscard]] reference
    operator*();

    [[nodiscard]] pointer
    operator->();

    // Prefix increment and decrement
    OwnerDirPageIterImpl&
    operator++();

    OwnerDirPageIterImpl&
    operator--();

    // Postfix increment and decrement
    [[nodiscard]] OwnerDirPageIterImpl
    operator++(int);

    [[nodiscard]] OwnerDirPageIterImpl
    operator--(int);

    // In C++17 a [[nodiscard]] friend function declaration must also be a
    // definition according to [dcl.attr.grammar] paragraph 5.  So the
    // definition must be in line if we want to keep the [[nodiscard]].
    [[nodiscard]] friend bool
    operator==(OwnerDirPageIterImpl const& a, OwnerDirPageIterImpl const& b)
    {
        return (
            a.pageIndex_ == b.pageIndex_ &&
            a.ownerDirKeylet_.key == b.ownerDirKeylet_.key);
    }

    [[nodiscard]] friend bool
    operator!=(OwnerDirPageIterImpl const& a, OwnerDirPageIterImpl const& b)
    {
        return !(a == b);
    }
};

template <bool Const>
auto
OwnerDirPageIterImpl<Const>::makeOwnerDirPage(std::shared_ptr<SleT> slePtr)
    -> OwnerDirPageT
{
    return OwnerDirPage(std::move(slePtr));
}

template <bool Const>
void
OwnerDirPageIterImpl<Const>::setOwnerDirPageIfOkay(
    std::shared_ptr<SleT> slePtr,
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

template <bool Const>
auto
OwnerDirPageIterImpl<Const>::begin(ViewT& view, AccountID const& ownerID)
    -> OwnerDirPageIterImpl
{
    OwnerDirPageIterImpl ret(view, ownerID);
    ret.setOwnerDirPageIfOkay(getSle(view, ret.ownerDirKeylet_), 0u);
    return ret;
}

template <bool Const>
auto
OwnerDirPageIterImpl<Const>::end(ViewT& view, AccountID const& ownerID)
    -> OwnerDirPageIterImpl
{
    OwnerDirPageIterImpl ret(view, ownerID);
    return ret;
}

template <bool Const>
auto
OwnerDirPageIterImpl<Const>::page(
    ViewT& view,
    AccountID const& ownerID,
    std::uint64_t pageIndex) -> OwnerDirPageIterImpl
{
    OwnerDirPageIterImpl ret(view, ownerID);
    {
        Keylet key = ret.ownerDirKeylet_;
        if (pageIndex != 0)
            key = keylet::page(key, pageIndex);
        ret.setOwnerDirPageIfOkay(getSle(view, ret.ownerDirKeylet_), pageIndex);
    }
    return ret;
}

template <bool Const>
auto
OwnerDirPageIterImpl<Const>::operator*() -> OwnerDirPageIterImpl::reference
{
    if (!ownerDirPage_)
        Throw<std::out_of_range>("Invalid OwnerDirPageIter access");
    return *ownerDirPage_;
}

template <bool Const>
auto
OwnerDirPageIterImpl<Const>::operator->() -> OwnerDirPageIterImpl::pointer
{
    if (!ownerDirPage_)
        Throw<std::out_of_range>("Invalid OwnerDirPageIter access");
    return &(*ownerDirPage_);
}

template <bool Const>
auto
OwnerDirPageIterImpl<Const>::operator++() -> OwnerDirPageIterImpl&
{
    // If we're already end(), then stay put.
    if (pageIndex_ == endPageIndex)
        return *this;

    std::uint64_t const next = ownerDirPage_->indexNext();

    // If next == 0 we've incremented past the last page on the list.
    // Become end().
    if (next == 0)
    {
        ownerDirPage_.reset();
        pageIndex_ = endPageIndex;
        return *this;
    }

    setOwnerDirPageIfOkay(
        getSle(view_, keylet::page(ownerDirKeylet_, next)), next);

    return *this;
}

template <bool Const>
auto
OwnerDirPageIterImpl<Const>::operator--() -> OwnerDirPageIterImpl&
{
    // A bidirectional iterator needs to be able to get to the last element
    // from an end() iterator.
    if (pageIndex_ == endPageIndex)
    {
        auto sle = getSle(view_, ownerDirKeylet_);
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
                sle = getSle(view_, keylet::page(ownerDirKeylet_, newIndex));
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
        setOwnerDirPageIfOkay(getSle(view_, ownerDirKeylet_), 0u);

    // If someone tries to decrement before begin(), then stay at begin().
    else if (prev < pageIndex_)
        setOwnerDirPageIfOkay(
            getSle(view_, keylet::page(ownerDirKeylet_, prev)), prev);

    return *this;
}

template <bool Const>
auto
OwnerDirPageIterImpl<Const>::operator++(int) -> OwnerDirPageIterImpl
{
    OwnerDirPageIterImpl ret = *this;
    ++(*this);
    return ret;
}

template <bool Const>
auto
OwnerDirPageIterImpl<Const>::operator--(int) -> OwnerDirPageIterImpl
{
    OwnerDirPageIterImpl ret = *this;
    --(*this);
    return ret;
}

using OwnerDirPageConstIter = OwnerDirPageIterImpl<true>;
using OwnerDirPageIter = OwnerDirPageIterImpl<false>;

#ifndef __INTELLISENSE__
static_assert(!std::is_default_constructible_v<OwnerDirPageConstIter>, "");
static_assert(std::is_copy_constructible_v<OwnerDirPageConstIter>, "");
static_assert(std::is_move_constructible_v<OwnerDirPageConstIter>, "");
static_assert(!std::is_copy_assignable_v<OwnerDirPageConstIter>, "");
static_assert(!std::is_move_assignable_v<OwnerDirPageConstIter>, "");
static_assert(std::is_nothrow_destructible_v<OwnerDirPageConstIter>, "");

static_assert(!std::is_default_constructible_v<OwnerDirPageIter>, "");
static_assert(std::is_copy_constructible_v<OwnerDirPageIter>, "");
static_assert(std::is_move_constructible_v<OwnerDirPageIter>, "");
static_assert(!std::is_copy_assignable_v<OwnerDirPageIter>, "");
static_assert(!std::is_move_assignable_v<OwnerDirPageIter>, "");
static_assert(std::is_nothrow_destructible_v<OwnerDirPageIter>, "");
#endif

}  // namespace ripple

#endif  // RIPPLE_LEDGER_OWNER_DIR_PAGE_ITER_H_INCLUDED
