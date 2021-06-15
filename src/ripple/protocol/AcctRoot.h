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

#include <ripple/protocol/STLedgerEntry.h>

namespace ripple {

// A wrapper that improves access to an AccountRoot through an STLedgerEntry.

// The template allows an acctRoot to be constructed form either a const
// or non-const std::shared_ptr<STLedgerEntry>
template <class STLE>
class AcctRoot
{
private:
    std::shared_ptr<STLE> slePtr_;

    static_assert(
        std::is_same_v<std::remove_const_t<STLE>, STLedgerEntry>,
        "Construct an AcctRoot from a shared_ptr<STLedgerEntry> or "
        "shared_ptr<STLedgerEntry const>");

    // Values that are cached because they are expensive to retrieve and
    // frequently accessed.
    mutable std::optional<AccountID> accountID_;

public:
    AcctRoot(std::shared_ptr<STLE>&& acctRootPtr)
        : slePtr_(std::move(acctRootPtr))
    {
        std::uint16_t const type = {slePtr_->at(sfLedgerEntryType)};
        assert(type == ltACCOUNT_ROOT);
        if (type != ltACCOUNT_ROOT)
            Throw<std::runtime_error>("Wrong ledger type for AcctRoot");
    }

    AcctRoot(std::shared_ptr<STLE> const& acctRootPtr)
        : AcctRoot(std::shared_ptr<STLE>(acctRootPtr))
    {
    }

    AcctRoot&
    operator=(AcctRoot const&) = delete;

private:
    // Helper functions --------------------------------------------------------
    template <typename SF, typename T>
    void
    setOptional(SF const& field, T const& value)
    {
        static_assert(
            std::is_base_of_v<SField, SF>,
            "setOptional()requires an SField as its first argument.");

        if (!slePtr_->isFieldPresent(field))
            slePtr_->makeFieldPresent(field);
        slePtr_->at(field) = value;
    }

    template <typename SF>
    void
    clearOptional(SF const& field)
    {
        static_assert(
            std::is_base_of_v<SField, SF>,
            "setOptional()requires an SField as its argument.");

        if (slePtr_->isFieldPresent(field))
            slePtr_->makeFieldAbsent(field);
    }

    Blob
    getOptionalVL(SF_VL const& field) const
    {
        Blob ret;
        if (slePtr_->isFieldPresent(field))
            ret = slePtr_->getFieldVL(field);
        return ret;
    }

    void
    setOrClearVLIfEmpty(SF_VL const& field, Blob const& value)
    {
        if (value.empty())
        {
            clearOptional(field);
            return;
        }
        if (!slePtr_->isFieldPresent(field))
            slePtr_->makeFieldPresent(field);
        slePtr_->setFieldVL(field, value);
    }

public:
    // AccountID field (immutable) ---------------------------------------------
    [[nodiscard]] AccountID const&
    accountID() const
    {
        if (!accountID_)
            accountID_ = slePtr_->at(sfAccount);
        return *accountID_;
    }

    // Flags field -------------------------------------------------------------
    [[nodiscard]] std::uint32_t
    flags() const
    {
        return slePtr_->at(sfFlags);
    }

    void
    setFlags(std::uint32_t newFlags)
    {
        slePtr_->at(sfFlags) = newFlags;
    }

    // Sequence field ----------------------------------------------------------
    [[nodiscard]] std::uint32_t
    sequence() const
    {
        return slePtr_->at(sfSequence);
    }

    void
    setSequence(std::uint32_t seq)
    {
        slePtr_->at(sfSequence) = seq;
    }

    // Balance field -----------------------------------------------------------
    [[nodiscard]] STAmount
    balance() const
    {
        return slePtr_->at(sfBalance);
    }

    void
    setBalance(STAmount const& amount)
    {
        slePtr_->at(sfBalance) = amount;
    }

    // OwnerCount field
    // ----------------------------------------------------------
    [[nodiscard]] std::uint32_t
    ownerCount() const
    {
        return slePtr_->at(sfOwnerCount);
    }

    void
    setOwnerCount(std::uint32_t newCount)
    {
        slePtr_->at(sfOwnerCount) = newCount;
    }

    // PreviousTxnID field
    // ----------------------------------------------------------
    [[nodiscard]] std::uint32_t
    previousTxnID() const
    {
        return slePtr_->at(sfOwnerCount);
    }

    void
    setPreviousTxnID(uint256 prevTxID)
    {
        slePtr_->at(sfPreviousTxnID) = prevTxID;
    }

    // PreviousTxnLgrSeq field
    // ----------------------------------------------------------
    [[nodiscard]] std::uint32_t
    previousTxnLgrSeq() const
    {
        return slePtr_->at(sfPreviousTxnLgrSeq);
    }

    void
    setPreviousTxnLgrSeq(std::uint32_t prevTxLgrSeq)
    {
        slePtr_->at(sfPreviousTxnLgrSeq) = prevTxLgrSeq;
    }

    // AccountTxnID field (optional) -------------------------------------------
    [[nodiscard]] std::optional<uint256>
    accountTxnID() const
    {
        return slePtr_->at(~sfAccountTxnID);
    }

    void
    setAccountTxnID(uint256 const& newAcctTxnID)
    {
        setOptional(sfAccountTxnID, newAcctTxnID);
    }

    void
    clearAccountTxnID()
    {
        clearOptional(sfAccountTxnID);
    }

    // RegularKey field (optional) ---------------------------------------------
    [[nodiscard]] std::optional<AccountID>
    regularKey() const
    {
        return slePtr_->at(~sfRegularKey);
    }

    void
    setRegularKey(AccountID const& newRegKey)
    {
        setOptional(sfRegularKey, newRegKey);
    }

    void
    clearRegularKey()
    {
        clearOptional(sfRegularKey);
    }

    // EmailHash field (optional) ----------------------------------------------
    [[nodiscard]] std::optional<uint128>
    emailHash() const
    {
        return slePtr_->at(~sfEmailHash);
    }

    void
    setEmailHash(uint128 const& newEmailHash)
    {
        setOptional(sfEmailHash, newEmailHash);
    }

    void
    clearEmailHash()
    {
        clearOptional(sfEmailHash);
    }

    // WalletLocator field (optional) ------------------------------------------
    [[nodiscard]] std::optional<uint256>
    walletLocator() const
    {
        return slePtr_->at(~sfWalletLocator);
    }

    void
    setWalletLocator(uint256 const& newWalletLocator)
    {
        setOptional(sfWalletLocator, newWalletLocator);
    }

    void
    clearWalletLocator()
    {
        clearOptional(sfWalletLocator);
    }

    // WalletSize field (optional) -------------------------------------------
    [[nodiscard]] std::optional<std::uint32_t>
    walletSize()
    {
        return slePtr_->at(~sfWalletSize);
    }

    void
    setWalletSize(std::uint32_t newWalletSize)
    {
        setOptional(sfWalletSize, newWalletSize);
    }

    void
    clearWalletSize()
    {
        clearOptional(sfWalletSize);
    }

    // MessageKey field (optional) -------------------------------------------
    [[nodiscard]] Blob
    messageKey() const
    {
        return getOptionalVL(sfMessageKey);
    }

    void
    setMessageKey(Blob const& newMessageKey)
    {
        setOrClearVLIfEmpty(sfMessageKey, newMessageKey);
    }

    // TransferRate field (optional) -----------------------------------------
    [[nodiscard]] std::optional<std::uint32_t>
    transferRate() const
    {
        return slePtr_->at(~sfTransferRate);
    }

    void
    setTransferRate(std::uint32_t newTransferRate)
    {
        setOptional(sfTransferRate, newTransferRate);
    }

    void
    clearTransferRate()
    {
        clearOptional(sfTransferRate);
    }

    // Domain field (optional) -------------------------------------------------
    [[nodiscard]] Blob
    domain() const
    {
        return getOptionalVL(sfDomain);
    }

    void
    setDomain(Blob const& newDomain)
    {
        setOrClearVLIfEmpty(sfDomain, newDomain);
    }

    // TickSize (optional) -----------------------------------------------------
    [[nodiscard]] std::optional<std::uint8_t>
    tickSize() const
    {
        return slePtr_->at(sfTickSize);
    }

    void
    setTickSize(std::uint8_t newTickSize)
    {
        setOptional(sfTickSize, newTickSize);
    }

    void
    clearTickSize()
    {
        clearOptional(sfTickSize);
    }

    // TicketCount (optional) --------------------------------------------------
    [[nodiscard]] std::optional<std::uint32_t>
    ticketCount()
    {
        return slePtr_->at(~sfTicketCount);
    }

    void
    setTicketCount(std::uint32_t newTicketCount)
    {
        setOptional(sfTicketCount, newTicketCount);
    }

    void
    clearTicketCount()
    {
        clearOptional(sfTicketCount);
    }
};

#ifndef __INTELLISENSE__
static_assert(std::is_default_constructible_v<AcctRoot<SLE>> == false, "");
static_assert(std::is_copy_constructible_v<AcctRoot<SLE>> == true, "");
static_assert(std::is_move_constructible_v<AcctRoot<SLE>> == true, "");
static_assert(std::is_copy_assignable_v<AcctRoot<SLE>> == false, "");
static_assert(std::is_move_assignable_v<AcctRoot<SLE>> == false, "");
static_assert(std::is_nothrow_destructible_v<AcctRoot<SLE>> == true, "");
#endif

}  // namespace ripple

#endif  // RIPPLE_PROTOCOL_ACCT_ROOT_H_INCLUDED
