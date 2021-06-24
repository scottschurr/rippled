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

#include <ripple/basics/tl/expected.hpp>
#include <ripple/protocol/STAccount.h>
#include <ripple/protocol/STLedgerEntry.h>
#include <utility>

namespace ripple {

// AcctRoot ====================================================================

// Use CRTP to provide a common interface for AcctRoot and AcctRootRd.
// The Writable argument enables mutating methods only if true.
template <class Derived, bool Writable>
class AcctRootIfc
{
private:
    // The constructor is private as a hack to guarantee that the Derived
    // argument is actually the class that drives from this.  See...
    //
    // http://www.fluentcpp.com/2017/05/12/curiously-recurring-template-pattern
    //
    // Give Derived access to private constructor.
    friend Derived;
    AcctRootIfc() = default;

    // A virtual destructor is not necessary since this base class is empty.

    // Helper functions --------------------------------------------------------
    [[nodiscard]] Derived const&
    asDerived() const
    {
        return static_cast<Derived const&>(*this);
    }

    [[nodiscard]] Derived&
    asDerived()
    {
        return static_cast<Derived&>(*this);
    }

    [[nodiscard]] Blob
    getOptionalVL(SF_VL const& field) const
    {
        Blob ret;
        Derived const& derived = asDerived();
        if (derived.slePtr()->isFieldPresent(field))
            ret = derived.slePtr()->getFieldVL(field);
        return ret;
    }

    template <typename SF, typename T, bool W = Writable>
    std::enable_if_t<W>
    setOptional(SF const& field, T const& value)
    {
        static_assert(
            std::is_base_of_v<SField, SF>,
            "setOptional()requires an SField as its first argument.");

        Derived& derived = asDerived();
        if (!derived.slePtr()->isFieldPresent(field))
            derived.slePtr()->makeFieldPresent(field);
        derived.slePtr()->at(field) = value;
    }

    template <typename SF, bool W = Writable>
    std::enable_if_t<W>
    clearOptional(SF const& field)
    {
        static_assert(
            std::is_base_of_v<SField, SF>,
            "setOptional()requires an SField as its argument.");

        Derived& derived = asDerived();
        if (derived.slePtr()->isFieldPresent(field))
            derived.slePtr()->makeFieldAbsent(field);
    }

    template <bool W = Writable>
    std::enable_if_t<W>
    setOrClearVLIfEmpty(SF_VL const& field, Blob const& value)
    {
        if (value.empty())
        {
            clearOptional(field);
            return;
        }
        Derived& derived = asDerived();
        if (!derived.slePtr()->isFieldPresent(field))
            derived.slePtr()->makeFieldPresent(field);
        derived.slePtr()->setFieldVL(field, value);
    }

public:
    // AccountID field (immutable) ---------------------------------------------
    [[nodiscard]] AccountID
    accountID() const
    {
        return asDerived().slePtr()->at(sfAccount);
    }

    // Flags field -------------------------------------------------------------
    [[nodiscard]] std::uint32_t
    flags() const
    {
        return asDerived().slePtr()->at(sfFlags);
    }

    [[nodiscard]] bool
    isFlag(std::uint32_t flagsToCheck) const
    {
        return (flags() & flagsToCheck) == flagsToCheck;
    }

    template <bool W = Writable>
    std::enable_if_t<W>
    replaceAllFlags(std::uint32_t newFlags)
    {
        asDerived().slePtr()->at(sfFlags) = newFlags;
    }

    template <bool W = Writable>
    std::enable_if_t<W>
    setFlag(std::uint32_t flagsToSet)
    {
        replaceAllFlags(flags() | flagsToSet);
    }

    template <bool W = Writable>
    std::enable_if_t<W>
    clearFlag(std::uint32_t flagsToClear)
    {
        replaceAllFlags(flags() & ~flagsToClear);
    }

    // Sequence field ----------------------------------------------------------
    [[nodiscard]] std::uint32_t
    sequence() const
    {
        return asDerived().slePtr()->at(sfSequence);
    }

    template <bool W = Writable>
    std::enable_if_t<W>
    setSequence(std::uint32_t seq)
    {
        asDerived().slePtr()->at(sfSequence) = seq;
    }

    // Balance field -----------------------------------------------------------
    [[nodiscard]] STAmount
    balance() const
    {
        return asDerived().slePtr()->at(sfBalance);
    }

    template <bool W = Writable>
    std::enable_if_t<W>
    setBalance(STAmount const& amount)
    {
        asDerived().slePtr()->at(sfBalance) = amount;
    }

    // OwnerCount field --------------------------------------------------------
    [[nodiscard]] std::uint32_t
    ownerCount() const
    {
        return asDerived().slePtr()->at(sfOwnerCount);
    }

    template <bool W = Writable>
    std::enable_if_t<W>
    setOwnerCount(std::uint32_t newCount)
    {
        asDerived().slePtr()->at(sfOwnerCount) = newCount;
    }

    // PreviousTxnID field -----------------------------------------------------
    [[nodiscard]] std::uint32_t
    previousTxnID() const
    {
        return asDerived().slePtr()->at(sfOwnerCount);
    }

    template <bool W = Writable>
    std::enable_if_t<W>
    setPreviousTxnID(uint256 prevTxID)
    {
        asDerived().slePtr()->at(sfPreviousTxnID) = prevTxID;
    }

    // PreviousTxnLgrSeq field -------------------------------------------------
    [[nodiscard]] std::uint32_t
    previousTxnLgrSeq() const
    {
        return asDerived().slePtr()->at(sfPreviousTxnLgrSeq);
    }

    template <bool W = Writable>
    std::enable_if_t<W>
    setPreviousTxnLgrSeq(std::uint32_t prevTxLgrSeq)
    {
        asDerived().slePtr()->at(sfPreviousTxnLgrSeq) = prevTxLgrSeq;
    }

    // AccountTxnID field (optional) -------------------------------------------
    [[nodiscard]] std::optional<uint256>
    accountTxnID() const
    {
        return asDerived().slePtr()->at(~sfAccountTxnID);
    }

    template <bool W = Writable>
    std::enable_if_t<W>
    setAccountTxnID(uint256 const& newAcctTxnID)
    {
        setOptional(sfAccountTxnID, newAcctTxnID);
    }

    template <bool W = Writable>
    std::enable_if_t<W>
    clearAccountTxnID()
    {
        clearOptional(sfAccountTxnID);
    }

    // RegularKey field (optional) ---------------------------------------------
    [[nodiscard]] std::optional<AccountID>
    regularKey() const
    {
        return asDerived().slePtr()->at(~sfRegularKey);
    }

    template <bool W = Writable>
    std::enable_if_t<W>
    setRegularKey(AccountID const& newRegKey)
    {
        setOptional(sfRegularKey, newRegKey);
    }

    template <bool W = Writable>
    std::enable_if_t<W>
    clearRegularKey()
    {
        clearOptional(sfRegularKey);
    }

    // EmailHash field (optional) ----------------------------------------------
    [[nodiscard]] std::optional<uint128>
    emailHash() const
    {
        return asDerived().slePtr()->at(~sfEmailHash);
    }

    template <bool W = Writable>
    std::enable_if_t<W>
    setEmailHash(uint128 const& newEmailHash)
    {
        setOptional(sfEmailHash, newEmailHash);
    }

    template <bool W = Writable>
    std::enable_if_t<W>
    clearEmailHash()
    {
        clearOptional(sfEmailHash);
    }

    // WalletLocator field (optional) ------------------------------------------
    [[nodiscard]] std::optional<uint256>
    walletLocator() const
    {
        return asDerived().slePtr()->at(~sfWalletLocator);
    }

    template <bool W = Writable>
    std::enable_if_t<W>
    setWalletLocator(uint256 const& newWalletLocator)
    {
        setOptional(sfWalletLocator, newWalletLocator);
    }

    template <bool W = Writable>
    std::enable_if_t<W>
    clearWalletLocator()
    {
        clearOptional(sfWalletLocator);
    }

    // WalletSize field (optional) ---------------------------------------------
    [[nodiscard]] std::optional<std::uint32_t>
    walletSize()
    {
        return asDerived().slePtr()->at(~sfWalletSize);
    }

    /*
    // There is currently no code in the server that modifies a WalletSize
    // field.  So these interfaces are omitted.
    template<bool W = Writable>
    std::enable_if_t<W>
    setWalletSize(std::uint32_t newWalletSize)
    {
        setOptional(sfWalletSize, newWalletSize);
    }

    template<bool W = Writable>
    std::enable_if_t<W>
    clearWalletSize()
    {
        clearOptional(sfWalletSize);
    } */

    // MessageKey field (optional) ---------------------------------------------
    [[nodiscard]] Blob
    messageKey() const
    {
        return getOptionalVL(sfMessageKey);
    }

    template <bool W = Writable>
    std::enable_if_t<W>
    setMessageKey(Blob const& newMessageKey)
    {
        setOrClearVLIfEmpty(sfMessageKey, newMessageKey);
    }

    // TransferRate field (optional) -------------------------------------------
    [[nodiscard]] std::optional<std::uint32_t>
    transferRate() const
    {
        return asDerived().slePtr()->at(~sfTransferRate);
    }

    template <bool W = Writable>
    std::enable_if_t<W>
    setTransferRate(std::uint32_t newTransferRate)
    {
        setOptional(sfTransferRate, newTransferRate);
    }

    template <bool W = Writable>
    std::enable_if_t<W>
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

    template <bool W = Writable>
    std::enable_if_t<W>
    setDomain(Blob const& newDomain)
    {
        setOrClearVLIfEmpty(sfDomain, newDomain);
    }

    // TickSize (optional) -----------------------------------------------------
    [[nodiscard]] std::optional<std::uint8_t>
    tickSize() const
    {
        return asDerived().slePtr()->at(sfTickSize);
    }

    template <bool W = Writable>
    std::enable_if_t<W>
    setTickSize(std::uint8_t newTickSize)
    {
        setOptional(sfTickSize, newTickSize);
    }

    template <bool W = Writable>
    std::enable_if_t<W>
    clearTickSize()
    {
        clearOptional(sfTickSize);
    }

    // TicketCount (optional) --------------------------------------------------
    [[nodiscard]] std::optional<std::uint32_t>
    ticketCount() const
    {
        return asDerived().slePtr()->at(~sfTicketCount);
    }

    template <bool W = Writable>
    std::enable_if_t<W>
    setTicketCount(std::uint32_t newTicketCount)
    {
        setOptional(sfTicketCount, newTicketCount);
    }

    template <bool W = Writable>
    std::enable_if_t<W>
    clearTicketCount()
    {
        clearOptional(sfTicketCount);
    }
};

// AcctRootRd ==================================================================

// A wrapper that improves read-only access to an AccountRoot through an
// STLedgerEntry const.
class AcctRootRd : public AcctRootIfc<AcctRootRd const, false>
{
private:
    // The serialized ledger entry that this type wraps.
    std::shared_ptr<STLedgerEntry const> slePtr_;

public:
    AcctRootRd(AcctRootRd const&) = default;
    AcctRootRd&
    operator=(AcctRootRd const&) = delete;

private:
    // Constructor (private and accessed through a factory function) -----------
    AcctRootRd(std::shared_ptr<STLedgerEntry const>&& acctRootPtr)
        : slePtr_(std::move(acctRootPtr))
    {
    }

    // Declare the factory function a friend
    friend tl::expected<AcctRootRd, NotTEC>
    makeAcctRootRd(std::shared_ptr<STLedgerEntry const> slePtr);

    // Declare AcctRoot a friend so it can construct an AcctRootRd
    friend class AcctRoot;

public:
    // Raw SLE access ----------------------------------------------------------
    [[nodiscard]] std::shared_ptr<STLedgerEntry const> const&
    slePtr() const
    {
        return slePtr_;
    }
};

#ifndef __INTELLISENSE__
static_assert(std::is_default_constructible_v<AcctRootRd> == false, "");
static_assert(std::is_copy_constructible_v<AcctRootRd> == true, "");
static_assert(std::is_move_constructible_v<AcctRootRd> == true, "");
static_assert(std::is_copy_assignable_v<AcctRootRd> == false, "");
static_assert(std::is_move_assignable_v<AcctRootRd> == false, "");
static_assert(std::is_nothrow_destructible_v<AcctRootRd> == true, "");
#endif

// AcctRoot ====================================================================

// A wrapper that improves access to an AccountRoot through an STLedgerEntry.
class AcctRoot : public AcctRootIfc<AcctRoot, true>
{
private:
    // The serialized ledger entry that this type wraps.
    std::shared_ptr<STLedgerEntry> slePtr_;

    // Support for the conversion operator to an AcctRootRd.  We cache
    // the value to avoid making shared_ptrs more often than necessary.
    mutable std::optional<AcctRootRd> acctRootRd_;

public:
    AcctRoot(AcctRoot const&) = default;
    AcctRoot&
    operator=(AcctRoot const&) = delete;

    // Conversion operator to an AcctRootRd.  Intentionally implicit to make
    // it easier to "pass" an AcctRoot object to a function that expects
    // an AcctRootRd.
    operator AcctRootRd const &() const
    {
        if (!acctRootRd_)
            acctRootRd_.emplace(AcctRootRd(slePtr_));
        return *acctRootRd_;
    }

private:
    // Constructor (private and accessed through a factory function) -----------
    AcctRoot(std::shared_ptr<STLedgerEntry>&& acctRootPtr)
        : slePtr_(std::move(acctRootPtr))
    {
    }

    // Declare the factory function a friend
    friend tl::expected<AcctRoot, NotTEC>
    makeAcctRoot(std::shared_ptr<STLedgerEntry> slePtr);

    // Helper functions --------------------------------------------------------

    // Interface used by AcctRootIfc.  We trust that AcctRootIfc will not
    // modify the returned STLedgerEntry when it calls this interface.
    [[nodiscard]] std::shared_ptr<STLedgerEntry> const&
    slePtr() const
    {
        return slePtr_;
    }

    // Allow AcctRootIfc access to slePtr() const
    friend class AcctRootIfc<AcctRoot, true>;

public:
    // Raw SLE access ----------------------------------------------------------
    [[nodiscard]] std::shared_ptr<STLedgerEntry> const&
    slePtr()
    {
        return slePtr_;
    }
};

#ifndef __INTELLISENSE__
static_assert(std::is_default_constructible_v<AcctRoot> == false, "");
static_assert(std::is_copy_constructible_v<AcctRoot> == true, "");
static_assert(std::is_move_constructible_v<AcctRoot> == true, "");
static_assert(std::is_copy_assignable_v<AcctRoot> == false, "");
static_assert(std::is_move_assignable_v<AcctRoot> == false, "");
static_assert(std::is_nothrow_destructible_v<AcctRoot> == true, "");
#endif

// Factory functions ===========================================================

// Factory function returns a tl::expected<AcctRootRd, NotTEC>
[[nodiscard]] inline tl::expected<AcctRootRd, NotTEC>
makeAcctRootRd(std::shared_ptr<STLedgerEntry const> slePtr)
{
    if (!slePtr)
        return tl::unexpected(terNO_ACCOUNT);

    std::uint16_t const type = {slePtr->at(sfLedgerEntryType)};
    assert(type == ltACCOUNT_ROOT);
    if (type != ltACCOUNT_ROOT)
        return tl::unexpected(tefINTERNAL);

    return AcctRootRd(std::move(slePtr));
}

// Factory function returns a tl::expected<AcctRoot, NotTEC>
[[nodiscard]] inline tl::expected<AcctRoot, NotTEC>
makeAcctRoot(std::shared_ptr<STLedgerEntry> slePtr)
{
    if (!slePtr)
        return tl::unexpected(terNO_ACCOUNT);

    std::uint16_t const type = {slePtr->at(sfLedgerEntryType)};
    assert(type == ltACCOUNT_ROOT);
    if (type != ltACCOUNT_ROOT)
        return tl::unexpected(tefINTERNAL);

    return AcctRoot(std::move(slePtr));
}

}  // namespace ripple

#endif  // RIPPLE_PROTOCOL_ACCT_ROOT_H_INCLUDED
