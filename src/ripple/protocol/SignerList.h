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

#ifndef RIPPLE_PROTOCOL_SIGNER_LIST_H_INCLUDED
#define RIPPLE_PROTOCOL_SIGNER_LIST_H_INCLUDED

#include <ripple/basics/tl/expected.hpp>
#include <ripple/protocol/STAccount.h>
#include <ripple/protocol/STLedgerEntry.h>
#include <ripple/protocol/TER.h>

#include <type_traits>
#include <utility>

namespace ripple {

class SignerList
{
    std::shared_ptr<SLE> wrapped_;

    // These constructors are private so only the factory functions can
    // construct a SignerList.
    SignerList(std::shared_ptr<SLE>&& w);
    SignerList(std::shared_ptr<SLE const>&& w);

    // Friend declarations of factory functions.
    friend tl::expected<SignerList const, NotTEC>
    makeSignerListRd(std::shared_ptr<STLedgerEntry const> slePtr);

    friend tl::expected<SignerList, NotTEC>
    makeSignerList(std::shared_ptr<STLedgerEntry> slePtr);

public:
    // Representation for a single SignerEntry in a SignerList.
    struct SignerEntry
    {
        AccountID account;
        std::uint16_t weight;

        SignerEntry(AccountID const& inAccount, std::uint16_t inWeight)
            : account(inAccount), weight(inWeight)
        {
        }

        // For sorting to look for duplicate accounts
        friend bool
        operator<(SignerEntry const& lhs, SignerEntry const& rhs)
        {
            return lhs.account < rhs.account;
        }

        friend bool
        operator==(SignerEntry const& lhs, SignerEntry const& rhs)
        {
            return lhs.account == rhs.account;
        }
    };

    SignerList(SignerList&&) = default;

    [[nodiscard]] std::shared_ptr<SLE const>
    slePtr() const;

    [[nodiscard]] std::shared_ptr<SLE>
    slePtr();

    [[nodiscard]] std::uint32_t
    flags() const;

    [[nodiscard]] bool
    isFlag(std::uint32_t flagsToCheck) const;

    [[nodiscard]] std::uint64_t
    ownerNode() const;

    [[nodiscard]] std::uint32_t
    signerQuorum() const;

    [[nodiscard]] std::vector<SignerEntry>
    signerEntries() const;

    [[nodiscard]] std::size_t
    signerEntriesSize() const;

    [[nodiscard]] uint256
    previousTxnID() const;

    void
    setPreviousTxnID(uint256 prevTxID);

    [[nodiscard]] std::uint32_t
    previousTxnLgrSeq() const;

    void
    setPreviousTxnLgrSeq(std::uint32_t prevTxLgrSeq);

    // Deserialize a SignerEntries array from the network or from the ledger.
    static tl::expected<std::vector<SignerEntry>, NotTEC>
    deserializeSignerEntries(STObject const& obj);
};

#ifndef __INTELLISENSE__
static_assert(!std::is_default_constructible_v<SignerList>);
static_assert(!std::is_copy_constructible_v<SignerList>);
static_assert(std::is_move_constructible_v<SignerList>);
static_assert(!std::is_copy_assignable_v<SignerList>);
static_assert(!std::is_move_assignable_v<SignerList>);
static_assert(std::is_nothrow_destructible_v<SignerList>);
#endif // __INTELLISENSE__

[[nodiscard]] tl::expected<SignerList const, NotTEC>
makeSignerListRd(std::shared_ptr<STLedgerEntry const> slePtr);

[[nodiscard]] tl::expected<SignerList, NotTEC>
makeSignerList(std::shared_ptr<STLedgerEntry> slePtr);

}  // namespace ripple

#endif  // RIPPLE_PROTOCOL_SIGNER_LIST_H_INCLUDED
