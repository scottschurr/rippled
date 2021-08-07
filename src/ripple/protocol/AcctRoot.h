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

#ifndef RIPPLE_PROTOCOL_ACCT_ROOT_H_INCLUDED
#define RIPPLE_PROTOCOL_ACCT_ROOT_H_INCLUDED

#include <ripple/basics/Result.h>
#include <ripple/protocol/LedgerObjectWrapper.h>
#include <ripple/protocol/STAccount.h>
#include <ripple/protocol/STLedgerEntry.h>
#include <ripple/protocol/TER.h>

#include <utility>

namespace ripple {

template <bool Const>
class AcctRootImpl final : public LedgerObjectWrapper<Const>
{
private:
    using SleT = typename LedgerObjectWrapper<Const>::SleT;
    using LedgerObjectWrapper<Const>::wrapped_;

    // This constructor is private so only the factory functions can
    // construct an AcctRootImpl.
    AcctRootImpl(std::shared_ptr<SleT>&& w)
        : LedgerObjectWrapper<Const>(std::move(w))
    {
    }

    // Friend declarations of factory functions.
    friend Result<AcctRootImpl<true>, NotTEC>
    asAcctRootRd(std::shared_ptr<STLedgerEntry const> slePtr);

    friend Result<AcctRootImpl<false>, NotTEC>
    asAcctRoot(std::shared_ptr<STLedgerEntry> slePtr);

public:
    AcctRootImpl(AcctRootImpl const&) = default;
    AcctRootImpl(AcctRootImpl&&) = default;

    // Conversion operator from AcctRootImpl<false> to AcctRootImpl<true>.
    operator AcctRootImpl<true>() const
    {
        return AcctRootImpl<true>(
            std::const_pointer_cast<std::shared_ptr<STLedgerEntry const>>(
                wrapped_));
    }

    [[nodiscard]] AccountID
    accountID() const
    {
        return wrapped_->at(sfAccount);
    }

    [[nodiscard]] std::uint32_t
    sequence() const
    {
        return wrapped_->at(sfSequence);
    }

    void
    setSequence(std::uint32_t seq)
    {
        static_assert(not Const, "Cannot set member of const ledger object.");
        wrapped_->at(sfSequence) = seq;
    }

    [[nodiscard]] STAmount
    balance() const
    {
        return wrapped_->at(sfBalance);
    }

    void
    setBalance(STAmount const& amount)
    {
        static_assert(not Const, "Cannot set member of const ledger object.");
        wrapped_->at(sfBalance) = amount;
    }

    [[nodiscard]] std::uint32_t
    ownerCount() const
    {
        return wrapped_->at(sfOwnerCount);
    }

    void
    setOwnerCount(std::uint32_t newCount)
    {
        static_assert(not Const, "Cannot set member of const ledger object.");
        wrapped_->at(sfOwnerCount) = newCount;
    }

    [[nodiscard]] std::uint32_t
    previousTxnID() const
    {
        return wrapped_->at(sfOwnerCount);
    }

    void
    setPreviousTxnID(uint256 prevTxID)
    {
        static_assert(not Const, "Cannot set member of const ledger object.");
        wrapped_->at(sfPreviousTxnID) = prevTxID;
    }

    [[nodiscard]] std::uint32_t
    previousTxnLgrSeq() const
    {
        return wrapped_->at(sfPreviousTxnLgrSeq);
    }

    void
    setPreviousTxnLgrSeq(std::uint32_t prevTxLgrSeq)
    {
        static_assert(not Const, "Cannot set member of const ledger object.");
        wrapped_->at(sfPreviousTxnLgrSeq) = prevTxLgrSeq;
    }

    [[nodiscard]] std::optional<uint256>
    accountTxnID() const
    {
        return wrapped_->at(~sfAccountTxnID);
    }

    void
    setAccountTxnID(uint256 const& newAcctTxnID)
    {
        static_assert(not Const, "Cannot set member of const ledger object.");
        this->setOptional(sfAccountTxnID, newAcctTxnID);
    }

    void
    clearAccountTxnID()
    {
        static_assert(not Const, "Cannot set member of const ledger object.");
        this->clearOptional(sfAccountTxnID);
    }

    [[nodiscard]] std::optional<AccountID>
    regularKey() const
    {
        return wrapped_->at(~sfRegularKey);
    }

    void
    setRegularKey(AccountID const& newRegKey)
    {
        static_assert(not Const, "Cannot set member of const ledger object.");
        this->setOptional(sfRegularKey, newRegKey);
    }

    void
    clearRegularKey()
    {
        static_assert(not Const, "Cannot set member of const ledger object.");
        this->clearOptional(sfRegularKey);
    }

    [[nodiscard]] std::optional<uint128>
    emailHash() const
    {
        return wrapped_->at(~sfEmailHash);
    }

    void
    setEmailHash(uint128 const& newEmailHash)
    {
        static_assert(not Const, "Cannot set member of const ledger object.");
        this->setOptional(sfEmailHash, newEmailHash);
    }

    void
    clearEmailHash()
    {
        static_assert(not Const, "Cannot set member of const ledger object.");
        this->clearOptional(sfEmailHash);
    }

    [[nodiscard]] std::optional<uint256>
    walletLocator() const
    {
        return wrapped_->at(~sfWalletLocator);
    }

    void
    setWalletLocator(uint256 const& newWalletLocator)
    {
        static_assert(not Const, "Cannot set member of const ledger object.");
        this->setOptional(sfWalletLocator, newWalletLocator);
    }

    void
    clearWalletLocator()
    {
        static_assert(not Const, "Cannot set member of const ledger object.");
        this->clearOptional(sfWalletLocator);
    }

    [[nodiscard]] std::optional<std::uint32_t>
    walletSize() const
    {
        return wrapped_->at(~sfWalletSize);
    }

    [[nodiscard]] Blob
    messageKey() const
    {
        return this->getOptionalVL(sfMessageKey);
    }

    void
    setMessageKey(Blob const& newMessageKey)
    {
        static_assert(not Const, "Cannot set member of const ledger object.");
        this->setOrClearVLIfEmpty(sfMessageKey, newMessageKey);
    }

    [[nodiscard]] std::optional<std::uint32_t>
    transferRate() const
    {
        return wrapped_->at(~sfTransferRate);
    }

    void
    setTransferRate(std::uint32_t newTransferRate)
    {
        static_assert(not Const, "Cannot set member of const ledger object.");
        this->setOptional(sfTransferRate, newTransferRate);
    }

    void
    clearTransferRate()
    {
        static_assert(not Const, "Cannot set member of const ledger object.");
        this->clearOptional(sfTransferRate);
    }

    [[nodiscard]] Blob
    domain() const
    {
        return this->getOptionalVL(sfDomain);
    }

    void
    setDomain(Blob const& newDomain)
    {
        static_assert(not Const, "Cannot set member of const ledger object.");
        this->setOrClearVLIfEmpty(sfDomain, newDomain);
    }

    [[nodiscard]] std::optional<std::uint8_t>
    tickSize() const
    {
        return wrapped_->at(sfTickSize);
    }

    void
    setTickSize(std::uint8_t newTickSize)
    {
        static_assert(not Const, "Cannot set member of const ledger object.");
        this->setOptional(sfTickSize, newTickSize);
    }

    void
    clearTickSize()
    {
        static_assert(not Const, "Cannot set member of const ledger object.");
        this->clearOptional(sfTickSize);
    }

    [[nodiscard]] std::optional<std::uint32_t>
    ticketCount() const
    {
        return wrapped_->at(~sfTicketCount);
    }

    void
    setTicketCount(std::uint32_t newTicketCount)
    {
        static_assert(not Const, "Cannot set member of const ledger object.");
        this->setOptional(sfTicketCount, newTicketCount);
    }

    void
    clearTicketCount()
    {
        static_assert(not Const, "Cannot set member of const ledger object.");
        this->clearOptional(sfTicketCount);
    }
};

using AcctRoot = AcctRootImpl<false>;
using AcctRootRd = AcctRootImpl<true>;

// clang-format off
#ifndef __INTELLISENSE__
static_assert(not std::is_default_constructible_v<AcctRoot>);
static_assert(    std::is_copy_constructible_v<AcctRoot>);
static_assert(    std::is_move_constructible_v<AcctRoot>);
static_assert(not std::is_copy_assignable_v<AcctRoot>);
static_assert(not std::is_move_assignable_v<AcctRoot>);
static_assert(    std::is_nothrow_destructible_v<AcctRoot>);

static_assert(not std::is_default_constructible_v<AcctRootRd>);
static_assert(    std::is_copy_constructible_v<AcctRootRd>);
static_assert(    std::is_move_constructible_v<AcctRootRd>);
static_assert(not std::is_copy_assignable_v<AcctRootRd>);
static_assert(not std::is_move_assignable_v<AcctRootRd>);
static_assert(    std::is_nothrow_destructible_v<AcctRootRd>);
#endif  // __INTELLISENSE__
// clang-format on

[[nodiscard]] Result<AcctRootRd, NotTEC>
asAcctRootRd(std::shared_ptr<STLedgerEntry const> slePtr);

[[nodiscard]] Result<AcctRoot, NotTEC>
asAcctRoot(std::shared_ptr<STLedgerEntry> slePtr);

}  // namespace ripple

#endif  // RIPPLE_PROTOCOL_ACCT_ROOT_H_INCLUDED
