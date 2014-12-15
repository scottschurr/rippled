//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2014 Ripple Labs Inc.

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

#include <BeastConfig.h>
#include <ripple/app/transactors/Transactor.h>
#include <ripple/app/transactors/impl/SignerEntries.h>
#include <ripple/protocol/STObject.h>
#include <ripple/protocol/STArray.h>
#include <ripple/protocol/STTx.h>
#include <ripple/protocol/STAccount.h>
#include <ripple/protocol/Indexes.h>
#include <ripple/basics/Log.h>
#include <cstdint>
#include <algorithm>

namespace ripple {

/**
See the README.md for an overview of the SetSignerList transaction that
this class implements.
*/
class SetSignerList final
    : public Transactor
{
public:
    SetSignerList (
        STTx const& txn,
        TransactionEngineParams params,
        TransactionEngine* engine)
        : Transactor (
            txn,
            params,
            engine,
            deprecatedLogs().journal("SetSignerList"))
    {

    }

    /**
    Applies the transaction if it is well formed and the ledger state permits.
    */
    TER doApply () override;

private:
    TER replaceSignerList (std::uint32_t quorum, uint256 const& index);
    TER destroySignerList (uint256 const& index);

    // signers are not const because method (intentionally) sorts vector.
    TER validateQuorumAndSignerEntries (
        std::uint32_t quorum,
        std::vector<SignerEntries::SignerEntry>& signers) const;

    void writeSignersToLedger (
        SLE::pointer ledgerEntry,
        std::uint32_t quorum,
        std::vector<SignerEntries::SignerEntry> const& signers);

    static std::size_t ownerCountDelta (std::size_t entryCount);
};

//------------------------------------------------------------------------------

TER SetSignerList::doApply ()
{
    assert (mTxnAccount);

    // All operations require our ledger index.  Compute that once and pass it
    // to our handlers.
    uint256 const index = getSignerListIndex (mTxnAccountID);

    // Check the quorum.  A non-zero quorum means we're creating or replacing
    // the list.  A zero quorum means we're destroying the list.
    std::uint32_t const quorum (mTxn.getFieldU32 (sfSignerQuorum));

    bool const hasSignerEntries (mTxn.isFieldPresent (sfSignerEntries));
    if (quorum && hasSignerEntries)
        return replaceSignerList (quorum, index);

    if ((quorum == 0) && !hasSignerEntries)
        return destroySignerList (index);

    if (m_journal.trace) m_journal.trace <<
        "Malformed transaction: Invalid signer set list format.";
    return temMALFORMED;
}

TER
SetSignerList::replaceSignerList (std::uint32_t quorum, uint256 const& index)
{
    if (!mTxn.isFieldPresent (sfSignerEntries))
    {
        if (m_journal.trace) m_journal.trace <<
            "Malformed transaction: Need signer entry array.";
        return temMALFORMED;
    }

    SignerEntries::Decoded signers (
        SignerEntries::deserialize (mTxn, m_journal, "transaction"));

    if (signers.ter != tesSUCCESS)
        return signers.ter;

    // Validate our settings.
    if (TER const ter = validateQuorumAndSignerEntries (quorum, signers.vec))
        return ter;

    // This may be either a create or a replace.  Preemptively destroy any
    // old signer list.  May reduce the reserve, so this is done before
    // checking the reserve.
    if (TER const ter = destroySignerList (index))
        return ter;

    // Compute new reserve.  Verify the account has funds to meet the reserve.
    std::size_t const oldOwnerCount = mTxnAccount->getFieldU32 (sfOwnerCount);
    std::size_t const addedOwnerCount = ownerCountDelta (signers.vec.size ());

    std::uint64_t const newReserve =
        mEngine->getLedger ()->getReserve (oldOwnerCount + addedOwnerCount);

    if (mPriorBalance.getNValue () < newReserve)
        return tecINSUFFICIENT_RESERVE;

    // Everything's ducky.  Add the ltSIGNER_LIST to the ledger.
    SLE::pointer signerList (mEngine->entryCreate (ltSIGNER_LIST, index));
    writeSignersToLedger (signerList, quorum, signers.vec);

    // Lambda for call to dirAdd.
    auto describer = [&] (SLE::ref sle, bool dummy)
        {
            Ledger::ownerDirDescriber (sle, dummy, mTxnAccountID);
        };

    // Add the signer list to the account's directory.
    std::uint64_t hint;
    TER result = mEngine->view ().dirAdd (
        hint, getOwnerDirIndex (mTxnAccountID), index, describer);

    if (m_journal.trace) m_journal.trace <<
        "Create signer list for account " <<
        mTxnAccountID << ": " << transHuman (result);

    if (result != tesSUCCESS)
        return result;

    signerList->setFieldU64 (sfOwnerNode, hint);

    // If we succeeded, the new entry counts against the creator's reserve.
    mEngine->view ().increaseOwnerCount (mTxnAccount, addedOwnerCount);

    return result;
}

TER SetSignerList::destroySignerList (uint256 const& index)
{
    // See if there's an ltSIGNER_LIST for this account.
    SLE::pointer signerList =
        mEngine->view ().entryCache (ltSIGNER_LIST, index);

    // If the signer list doesn't exist we've already succeeded in deleting it.
    if (!signerList)
        return tesSUCCESS;

    // We have to examine the current SignerList so we know how much to
    // reduce the OwnerCount.
    std::size_t removeFromOwnerCount = 0;
    uint256 const signerListIndex = getSignerListIndex (mTxnAccountID);
    SLE::pointer accountSignersList =
        mEngine->view ().entryCache (ltSIGNER_LIST, signerListIndex);
    if (accountSignersList)
    {
        STArray const& actualList =
            accountSignersList->getFieldArray (sfSignerEntries);
        removeFromOwnerCount = ownerCountDelta (actualList.size ());
    }

    // Remove the node from the account directory.
    std::uint64_t const hint (signerList->getFieldU64 (sfOwnerNode));

    TER const result  = mEngine->view ().dirDelete (false, hint,
        getOwnerDirIndex (mTxnAccountID), index, false, (hint == 0));

    if (result == tesSUCCESS)
        mEngine->view ().decreaseOwnerCount (mTxnAccount, removeFromOwnerCount);

    mEngine->view ().entryDelete (signerList);

    return result;
}

TER SetSignerList::validateQuorumAndSignerEntries (
    std::uint32_t quorum,
    std::vector<SignerEntries::SignerEntry>& signers) const
{
    // Reject if there are too many or too few entries in the list.
    {
        std::size_t const signerCount = signers.size ();
        if ((signerCount < SignerEntries::minEntries)
            || (signerCount > SignerEntries::maxEntries))
        {
            if (m_journal.trace) m_journal.trace <<
                "Too many or too few signers in signer list.";
            return temMALFORMED;
        }
    }

    // Make sure there are no duplicate signers.
    std::sort (signers.begin (), signers.end ());
    if (std::adjacent_find (
        signers.begin (), signers.end ()) != signers.end ())
    {
        if (m_journal.trace) m_journal.trace <<
            "Duplicate signers in signer list";
        return temBAD_SIGNER;
    }

    // Make sure no signers reference this account.  Also make sure the
    // quorum can be reached.
    std::uint64_t allSignersWeight (0);
    for (auto const& signer : signers)
    {
        allSignersWeight += signer.weight;

        if (signer.account == mTxnAccountID)
        {
            if (m_journal.trace) m_journal.trace <<
                "A signer may not self reference account.";
            return temBAD_SIGNER;
        }

        // Don't verify that the signer accounts exist.  Verifying them is
        // expensive and they may not exist yet due to network phenomena.
    }
    if ((quorum <= 0) || (allSignersWeight < quorum))
    {
        if (m_journal.trace) m_journal.trace <<
            "Quorum is unreachable";
        return temBAD_QUORUM;
    }
    return tesSUCCESS;
}

void SetSignerList::writeSignersToLedger (
    SLE::pointer ledgerEntry,
    std::uint32_t quorum,
    std::vector<SignerEntries::SignerEntry> const& signers)
{
    // Assign the quorum.
    ledgerEntry->setFieldU32 (sfSignerQuorum, quorum);

    // Create the SignerListArray one STObject at a time.
    STArray::vector list (signers.size ());
    for (auto const& entry : signers)
    {
        boost::ptr_vector <STBase> data;
        data.reserve (2);

        auto account = std::make_unique <STAccount> (sfAccount);
        account->setValueH160 (entry.account);
        data.push_back (account.release ());

        data.push_back (new STUInt16 (sfSignerWeight, entry.weight));

        list.push_back (new STObject (sfSignerEntry, data));
    }

    // Assign the SignerEntries.
    STArray toLedger(sfSignerEntries, list);
    ledgerEntry->setFieldArray (sfSignerEntries, toLedger);
}

std::size_t SetSignerList::ownerCountDelta (std::size_t entryCount)
{
    // We always compute the full change in OwnerCount, taking into account:
    //  o The fact that we're adding/removing a SignerList and
    //  o Accounting for the number of entries in the list.
    // We can get away with that because lists are not adjusted incrementally;
    // we add or remove an entire list.

    // The wiki currently says (December 2014) the reserve should be
    //   Reserve * (N + 1) / 2
    // That's not making sense to me right now, since I'm working in
    // integral OwnerCount units.  If, say, N is 4 I don't know how to return
    // 4.5 units as an integer.
    //
    // So, just to get started, I'm saying that:
    //  o Simply having a SignerList costs 2 OwnerCount units.
    //  o And each signer in the list costs 1 more OwnerCount unit.
    // So, at a minimum, adding a SignerList with 2 entries costs 4 OwnerCount
    // units.  A SignerList with 32 entries would cost 34 OwnerCount units.
    //
    // It's worth noting that once this reserve policy has gotten into the
    // wild it will be very difficult to change.  So think hard about what
    // we want for the long term.
    return 2 + entryCount;
}

TER
transact_SetSignerList (
    STTx const& txn,
    TransactionEngineParams params,
    TransactionEngine* engine)
{
    return SetSignerList (txn, params, engine).apply ();
}

} // namespace ripple
