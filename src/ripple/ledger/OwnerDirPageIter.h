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

namespace ripple {

class OwnerDirPageIter
{
    constexpr static std::uint64_t endPageIndex =
        std::numeric_limits<std::uint64_t>::max();

    ApplyView& view_;
    AccountID const& ownerID_;
    Keylet const ownerDirKeylet_;
    std::uint64_t pageIndex_;
    std::optional<OwnerDirPage> ownerDirPage_;

    // Private constructor used by begin(), end(), and page().
    OwnerDirPageIter(ApplyView& view, AccountID const& ownerID)
        : view_(view)
        , ownerID_(ownerID)
        , ownerDirKeylet_(keylet::ownerDir(ownerID))
        , pageIndex_(endPageIndex)
    {
    }

    // Private function that produces an OwnerDirPage from a SLE
    static OwnerDirPage
    makeOwnerDirPage(std::shared_ptr<SLE> slePtr);

    // Only set the ownerDirPage_ member if the passed SLE is valid.
    void
    setOwnerDirPageIfOkay(std::shared_ptr<SLE> slePtr, std::uint64_t pageIndex);

public:
    using iterator_category = std::bidirectional_iterator_tag;
    using difference_type = std::ptrdiff_t;
    using value_type = OwnerDirPage;
    using pointer = OwnerDirPage*;
    using reference = OwnerDirPage&;

    [[nodiscard]] static OwnerDirPageIter
    begin(ApplyView& view, AccountID const& ownerID);

    [[nodiscard]] static OwnerDirPageIter
    end(ApplyView& view, AccountID const& ownerID);

    [[nodiscard]] static OwnerDirPageIter
    page(ApplyView& view, AccountID const& ownerID, std::uint64_t pageIndex);

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
    OwnerDirPageIter&
    operator++();

    OwnerDirPageIter&
    operator--();

    // Postfix increment and decrement
    OwnerDirPageIter
    operator++(int);

    OwnerDirPageIter
    operator--(int);

    // In C++17 a [[nodiscard]] friend function declaration must also be a
    // definition according to [dcl.attr.grammar] paragraph 5.  So we would
    // lose the [[nodiscard]] if the definition were in the cpp file.
    [[nodiscard]] friend bool
    operator==(OwnerDirPageIter const& a, OwnerDirPageIter const& b)
    {
        return (
            a.pageIndex_ == b.pageIndex_ &&
            a.ownerDirKeylet_.key == b.ownerDirKeylet_.key);
    }

    [[nodiscard]] friend bool
    operator!=(OwnerDirPageIter const& a, OwnerDirPageIter const& b)
    {
        return !(a == b);
    }
};

#ifndef __INTELLISENSE__
static_assert(std::is_default_constructible_v<OwnerDirPageIter> == false, "");
static_assert(std::is_copy_constructible_v<OwnerDirPageIter> == true, "");
static_assert(std::is_move_constructible_v<OwnerDirPageIter> == true, "");
static_assert(std::is_copy_assignable_v<OwnerDirPageIter> == false, "");
static_assert(std::is_move_assignable_v<OwnerDirPageIter> == false, "");
static_assert(std::is_nothrow_destructible_v<OwnerDirPageIter> == true, "");
#endif

}  // namespace ripple

#endif  // RIPPLE_LEDGER_OWNER_DIR_PAGE_ITER_H_INCLUDED
