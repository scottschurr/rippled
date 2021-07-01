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

#ifndef RIPPLE_PROTOCOL_OWNER_DIR_PAGE_H_INCLUDED
#define RIPPLE_PROTOCOL_OWNER_DIR_PAGE_H_INCLUDED

#include <ripple/protocol/STLedgerEntry.h>
#include <memory>

namespace ripple {

class OwnerDirPageIter;

class OwnerDirPage
{
    std::shared_ptr<SLE> wrapped_;

    // These constructors are private so only the factory functions can
    // construct an OwnerDirPage from scratch.
    OwnerDirPage(std::shared_ptr<SLE>&& w);
    OwnerDirPage(std::shared_ptr<SLE const>&& w);

    // Make the factory functions friends.

    // We must make the whole class a friend.  A member of the class can
    // only be declared a friend if the full class declaration has been
    // seen previously.  Including the OwnerDirPageIter class declaration
    // would violate leveling.
    friend OwnerDirPageIter;

public:
    OwnerDirPage(OwnerDirPage const&) = default;
    OwnerDirPage(OwnerDirPage&&) = default;

    [[nodiscard]] std::shared_ptr<SLE const>
    slePtr() const;

    [[nodiscard]] std::shared_ptr<SLE>
    slePtr();

    [[nodiscard]] AccountID
    ownerID() const;

    [[nodiscard]] uint256
    rootIndex() const;

    [[nodiscard]] STVector256 const&
    indexes() const;

    void
    setIndexes(STVector256 const& newIndexes);

    [[nodiscard]] std::uint64_t
    indexNext() const;

    void
    setIndexNext(std::uint64_t newIndexNext);

    [[nodiscard]] std::uint64_t
    indexPrevious() const;

    void
    setIndexPrevious(std::uint64_t newIndexPrevious);
};

#ifndef __INTELLISENSE__
static_assert(std::is_default_constructible_v<OwnerDirPage> == false, "");
static_assert(std::is_copy_constructible_v<OwnerDirPage> == true, "");
static_assert(std::is_move_constructible_v<OwnerDirPage> == true, "");
static_assert(std::is_copy_assignable_v<OwnerDirPage> == false, "");
static_assert(std::is_move_assignable_v<OwnerDirPage> == false, "");
static_assert(std::is_nothrow_destructible_v<OwnerDirPage> == true, "");
#endif

}  // namespace ripple

#endif  // RIPPLE_PROTOCOL_OWNER_DIR_PAGE_H_INCLUDED
