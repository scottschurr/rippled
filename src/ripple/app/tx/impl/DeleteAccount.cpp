//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2019 Ripple Labs Inc.

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

#include <ripple/app/tx/impl/DeleteAccount.h>
#include <ripple/app/tx/impl/DepositPreauth.h>
#include <ripple/app/tx/impl/SetSignerList.h>
#include <ripple/basics/FeeUnits.h>
#include <ripple/basics/Log.h>
#include <ripple/basics/mulDiv.h>
#include <ripple/ledger/OwnerDirPageIter.h>
#include <ripple/ledger/View.h>
#include <ripple/protocol/AcctRoot.h>
#include <ripple/protocol/Feature.h>
#include <ripple/protocol/Indexes.h>
#include <ripple/protocol/TxFlags.h>
#include <ripple/protocol/st.h>

namespace ripple {

NotTEC
DeleteAccount::preflight(PreflightContext const& ctx)
{
    if (!ctx.rules.enabled(featureDeletableAccounts))
        return temDISABLED;

    if (ctx.tx.getFlags() & tfUniversalMask)
        return temINVALID_FLAG;

    auto const ret = preflight1(ctx);

    if (!isTesSuccess(ret))
        return ret;

    if (ctx.tx[sfAccount] == ctx.tx[sfDestination])
        // An account cannot be deleted and give itself the resulting XRP.
        return temDST_IS_SRC;

    return preflight2(ctx);
}

FeeUnit64
DeleteAccount::calculateBaseFee(ReadView const& view, STTx const& tx)
{
    // The fee required for AccountDelete is one owner reserve.  But the
    // owner reserve is stored in drops.  We need to convert it to fee units.
    Fees const& fees{view.fees()};
    std::pair<bool, FeeUnit64> const mulDivResult{
        mulDiv(fees.increment, safe_cast<FeeUnit64>(fees.units), fees.base)};
    if (mulDivResult.first)
        return mulDivResult.second;

    // If mulDiv returns false then overflow happened.  Punt by using the
    // standard calculation.
    return Transactor::calculateBaseFee(view, tx);
}

namespace {
// Define a function pointer type that can be used to delete ledger node types.
using DeleterFuncPtr = TER (*)(
    Application& app,
    ApplyView& view,
    AccountID const& account,
    uint256 const& delIndex,
    std::shared_ptr<SLE> const& sleDel,
    beast::Journal j);

// Local function definitions that provides signature compatibility.
TER
offerDelete(
    Application& app,
    ApplyView& view,
    AccountID const& account,
    uint256 const& delIndex,
    std::shared_ptr<SLE> const& sleDel,
    beast::Journal j)
{
    return offerDelete(view, sleDel, j);
}

TER
removeSignersFromLedger(
    Application& app,
    ApplyView& view,
    AccountID const& account,
    uint256 const& delIndex,
    std::shared_ptr<SLE> const& sleDel,
    beast::Journal j)
{
    return SetSignerList::removeFromLedger(app, view, account, j);
}

TER
removeTicketFromLedger(
    Application&,
    ApplyView& view,
    AccountID const& account,
    uint256 const& delIndex,
    std::shared_ptr<SLE> const&,
    beast::Journal j)
{
    return Transactor::ticketDelete(view, account, delIndex, j);
}

TER
removeDepositPreauthFromLedger(
    Application& app,
    ApplyView& view,
    AccountID const& account,
    uint256 const& delIndex,
    std::shared_ptr<SLE> const& sleDel,
    beast::Journal j)
{
    return DepositPreauth::removeFromLedger(app, view, delIndex, j);
}

// Return nullptr if the LedgerEntryType represents an obligation that can't
// be deleted.  Otherwise return the pointer to the function that can delete
// the non-obligation
DeleterFuncPtr
nonObligationDeleter(LedgerEntryType t)
{
    switch (t)
    {
        case ltOFFER:
            return offerDelete;
        case ltSIGNER_LIST:
            return removeSignersFromLedger;
        case ltTICKET:
            return removeTicketFromLedger;
        case ltDEPOSIT_PREAUTH:
            return removeDepositPreauthFromLedger;
        default:
            return nullptr;
    }
}

}  // namespace

TER
DeleteAccount::preclaim(PreclaimContext const& ctx)
{
    AccountID const account{ctx.tx[sfAccount]};
    AccountID const dst{ctx.tx[sfDestination]};

    auto const dstRoot = makeAcctRootRd(ctx.view.read(keylet::account(dst)));
    if (!dstRoot.has_value())
        return tecNO_DST;

    if (dstRoot->flags() & lsfRequireDestTag && !ctx.tx[~sfDestinationTag])
        return tecDST_TAG_NEEDED;

    // Check whether the destination account requires deposit authorization.
    if (ctx.view.rules().enabled(featureDepositAuth) &&
        (dstRoot->flags() & lsfDepositAuth))
    {
        if (!ctx.view.exists(keylet::depositPreauth(dst, account)))
            return tecNO_PERMISSION;
    }

    auto const acctRoot =
        makeAcctRootRd(ctx.view.read(keylet::account(account)));
    assert(acctRoot.has_value());
    if (!acctRoot.has_value())
        return terNO_ACCOUNT;

    // We don't allow an account to be deleted if its sequence number
    // is within 256 of the current ledger.  This prevents replay of old
    // transactions if this account is resurrected after it is deleted.
    //
    // We look at the account's Sequence rather than the transaction's
    // Sequence in preparation for Tickets.
    constexpr std::uint32_t seqDelta{255};
    if (acctRoot->sequence() + seqDelta > ctx.view.seq())
        return tecTOO_SOON;

    // Verify that the account does not own any objects that would prevent
    // the account from being deleted.
    Keylet const ownerDirKeylet{keylet::ownerDir(account)};
    if (dirIsEmpty(ctx.view, ownerDirKeylet))
        return tesSUCCESS;

    std::shared_ptr<SLE const> sleDirNode{};
    unsigned int uDirEntry{0};
    uint256 dirEntry{beast::zero};

    if (!cdirFirst(
            ctx.view,
            ownerDirKeylet.key,
            sleDirNode,
            uDirEntry,
            dirEntry,
            ctx.j))
        // Account has no directory at all.  This _should_ have been caught
        // by the dirIsEmpty() check earlier, but it's okay to catch it here.
        return tesSUCCESS;

    std::int32_t deletableDirEntryCount{0};
    do
    {
        // Make sure any directory node types that we find are the kind
        // we can delete.
        Keylet const itemKeylet{ltCHILD, dirEntry};
        auto sleItem = ctx.view.read(itemKeylet);
        if (!sleItem)
        {
            // Directory node has an invalid index.  Bail out.
            JLOG(ctx.j.fatal())
                << "DeleteAccount: directory node in ledger " << ctx.view.seq()
                << " has index to object that is missing: "
                << to_string(dirEntry);
            return tefBAD_LEDGER;
        }

        LedgerEntryType const nodeType{
            safe_cast<LedgerEntryType>((*sleItem)[sfLedgerEntryType])};

        if (!nonObligationDeleter(nodeType))
            return tecHAS_OBLIGATIONS;

        // We found a deletable directory entry.  Count it.  If we find too
        // many deletable directory entries then bail out.
        if (++deletableDirEntryCount > maxDeletableDirEntries)
            return tefTOO_BIG;

    } while (cdirNext(
        ctx.view, ownerDirKeylet.key, sleDirNode, uDirEntry, dirEntry, ctx.j));

    return tesSUCCESS;
}

TER
DeleteAccount::doApply()
{
    auto srcRoot = makeAcctRoot(view().peek(keylet::account(account_)));
    assert(srcRoot.has_value());

    auto dstRoot =
        makeAcctRoot(view().peek(keylet::account(ctx_.tx[sfDestination])));
    assert(dstRoot.has_value());

    if (!srcRoot.has_value() || !dstRoot.has_value())
        return tefBAD_LEDGER;

    // Delete all of the entries in the account directory.
    OwnerDirPageIter iter = OwnerDirPageIter::begin(view(), account_);
    for (; !iter.isEnd(); ++iter)
    {
        // Delete all Indexes held by this page.  We delete the first entry
        // in Indexes repeatedly until Indexes is empty.  It would be slightly
        // more efficient to work backwards through Indexes, but that would be
        // transaction changing.  The slight improvement in efficiency is not
        // worth an amendment.
        STVector256 const& indexes = iter->indexes();
        while (!indexes.empty())
        {
            uint256 const& index = indexes[0];

            // Choose the right way to delete each directory node.
            Keylet const itemKeylet{ltCHILD, index};
            auto sleItem = view().peek(itemKeylet);
            if (!sleItem)
            {
                // Directory node has an invalid index.  Bail out.
                JLOG(j_.fatal())
                    << "DeleteAccount: Directory node in ledger "
                    << view().seq() << " has index to object that is missing: "
                    << to_string(index);
                return tefBAD_LEDGER;
            }

            LedgerEntryType const nodeType{safe_cast<LedgerEntryType>(
                sleItem->getFieldU16(sfLedgerEntryType))};

            if (auto deleter = nonObligationDeleter(nodeType))
            {
                TER const result{
                    deleter(ctx_.app, view(), account_, index, sleItem, j_)};

                if (!isTesSuccess(result))
                    return result;
            }
            else
            {
                assert(!"Undeletable entry should be found in preclaim.");
                JLOG(j_.error())
                    << "DeleteAccount undeletable item not found in preclaim.";
                return tecHAS_OBLIGATIONS;
            }
        }
    }

    // Transfer any XRP remaining after the fee is paid to the destination:
    dstRoot->setBalance(dstRoot->balance() + mSourceBalance);
    srcRoot->setBalance(srcRoot->balance() - mSourceBalance);
    ctx_.deliver(mSourceBalance);

    assert(srcRoot->balance() == XRPAmount(0));

    // If there's still an owner directory associated with the source account
    // delete it.
    if (view().exists(iter.ownerRootKeylet()) &&
        !view().emptyDirDelete(iter.ownerRootKeylet()))
    {
        JLOG(j_.error()) << "DeleteAccount cannot delete root dir node of "
                         << toBase58(account_);
        return tecHAS_OBLIGATIONS;
    }

    // Re-arm the password change fee if we can and need to.
    if (mSourceBalance > XRPAmount(0) && dstRoot->isFlag(lsfPasswordSpent))
        dstRoot->clearFlag(lsfPasswordSpent);

    view().update(dstRoot->slePtr());
    view().erase(srcRoot->slePtr());

    return tesSUCCESS;
}

}  // namespace ripple
