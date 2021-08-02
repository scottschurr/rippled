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

#include <ripple/protocol/STArray.h>
#include <ripple/protocol/SignerList.h>
#include <type_traits>

namespace ripple {

SignerList::SignerList(std::shared_ptr<SLE>&& w) : wrapped_(std::move(w))
{
}

SignerList::SignerList(std::shared_ptr<SLE const>&& w)
    : wrapped_(std::const_pointer_cast<SLE>(std::move(w)))
{
}

std::shared_ptr<SLE>
SignerList::slePtr()
{
    return wrapped_;
}

std::shared_ptr<SLE const>
SignerList::slePtr() const
{
    return wrapped_;
}

std::uint32_t
SignerList::flags() const
{
    return wrapped_->at(sfFlags);
}

bool
SignerList::isFlag(std::uint32_t flagsToCheck) const
{
    return (flags() & flagsToCheck) == flagsToCheck;
}

std::uint64_t
SignerList::ownerNode() const
{
    return wrapped_->at(sfOwnerNode);
}

std::uint32_t
SignerList::signerQuorum() const
{
    return wrapped_->at(sfSignerQuorum);
}

std::vector<SignerList::SignerEntry>
SignerList::signerEntries() const
{
    auto entries = deserializeSignerEntries(*wrapped_);
    if (!entries.has_value())
        return {};
    return std::move(*entries);
}

std::size_t
SignerList::signerEntriesSize() const
{
    return wrapped_->getFieldArray(sfSignerEntries).size();
}

uint256
SignerList::previousTxnID() const
{
    return wrapped_->at(sfPreviousTxnID);
}

void
SignerList::setPreviousTxnID(uint256 prevTxID)
{
    wrapped_->at(sfPreviousTxnID) = prevTxID;
}

std::uint32_t
SignerList::previousTxnLgrSeq() const
{
    return wrapped_->at(sfPreviousTxnLgrSeq);
}

void
SignerList::setPreviousTxnLgrSeq(std::uint32_t prevTxLgrSeq)
{
    wrapped_->at(sfPreviousTxnLgrSeq) = prevTxLgrSeq;
}

tl::expected<std::vector<SignerList::SignerEntry>, NotTEC>
SignerList::deserializeSignerEntries(STObject const& obj)
{
    std::vector<SignerEntry> accountVec;

    // If no SignerEntries present return empty vector.
    if (!obj.isFieldPresent(sfSignerEntries))
        return accountVec;

    STArray const& sEntries(obj.getFieldArray(sfSignerEntries));
    accountVec.reserve(sEntries.size());

    for (STObject const& sEntry : sEntries)
    {
        // Validate the SignerEntry.
        if (sEntry.getFName() != sfSignerEntry)
            return tl::unexpected(tefINTERNAL);

        // Extract SignerEntry fields.
        AccountID const account = sEntry.getAccountID(sfAccount);
        std::uint16_t const weight = sEntry.getFieldU16(sfSignerWeight);
        accountVec.emplace_back(account, weight);
    }
    return accountVec;
}

template <class T>
[[nodiscard]] static NotTEC
validateSignerListSle(std::shared_ptr<T> const& slePtr)
{
    static_assert(std::is_same_v<std::remove_const_t<T>, STLedgerEntry>);

    if (!slePtr)
        return tefNOT_MULTI_SIGNING;

    std::uint16_t const type = {slePtr->at(sfLedgerEntryType)};
    assert(type == ltSIGNER_LIST);
    if (type != ltSIGNER_LIST)
        return tefINTERNAL;

    // SignerLists have a (currently unused but required) ID field.  Verify
    // that the field contains zero.
    std::uint32_t listID = {slePtr->at(sfSignerListID)};
    if (listID != 0)
        return tefINTERNAL;

    return tesSUCCESS;
}

tl::expected<SignerList const, NotTEC>
makeSignerListRd(std::shared_ptr<STLedgerEntry const> slePtr)
{
    if (NotTEC const ter = validateSignerListSle(slePtr); !isTesSuccess(ter))
        return tl::unexpected(ter);

    return SignerList(std::move(slePtr));
}

tl::expected<SignerList, NotTEC>
makeSignerList(std::shared_ptr<STLedgerEntry> slePtr)
{
    if (NotTEC const ter = validateSignerListSle(slePtr); !isTesSuccess(ter))
        return tl::unexpected(ter);

    return SignerList(std::move(slePtr));
}

}  // namespace ripple
