//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

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
#include <ripple/protocol/Indexes.h>
#include <ripple/core/Config.h>

namespace ripple {

TER transact_Payment (STTx const& txn, TransactionEngineParams params, TransactionEngine* engine);
TER transact_SetAccount (STTx const& txn, TransactionEngineParams params, TransactionEngine* engine);
TER transact_SetRegularKey (STTx const& txn, TransactionEngineParams params, TransactionEngine* engine);
TER transact_SetTrust (STTx const& txn, TransactionEngineParams params, TransactionEngine* engine);
TER transact_CreateOffer (STTx const& txn, TransactionEngineParams params, TransactionEngine* engine);
TER transact_CancelOffer (STTx const& txn, TransactionEngineParams params, TransactionEngine* engine);
TER transact_AddWallet (STTx const& txn, TransactionEngineParams params, TransactionEngine* engine);
TER transact_Change (STTx const& txn, TransactionEngineParams params, TransactionEngine* engine);
TER transact_CreateTicket (STTx const& txn, TransactionEngineParams params, TransactionEngine* engine);
TER transact_CancelTicket (STTx const& txn, TransactionEngineParams params, TransactionEngine* engine);
TER transact_SetSignerList (STTx const& txn, TransactionEngineParams params, TransactionEngine* engine);

TER
Transactor::transact (
    STTx const& txn,
    TransactionEngineParams params,
    TransactionEngine* engine)
{
    switch (txn.getTxnType ())
    {
    case ttPAYMENT:
        return transact_Payment (txn, params, engine);

    case ttACCOUNT_SET:
        return transact_SetAccount (txn, params, engine);

    case ttREGULAR_KEY_SET:
        return transact_SetRegularKey (txn, params, engine);

    case ttTRUST_SET:
        return transact_SetTrust (txn, params, engine);

    case ttOFFER_CREATE:
        return transact_CreateOffer (txn, params, engine);

    case ttOFFER_CANCEL:
        return transact_CancelOffer (txn, params, engine);

    case ttWALLET_ADD:
        return transact_AddWallet (txn, params, engine);

    case ttAMENDMENT:
    case ttFEE:
        return transact_Change (txn, params, engine);

    case ttTICKET_CREATE:
        return transact_CreateTicket (txn, params, engine);

    case ttTICKET_CANCEL:
        return transact_CancelTicket (txn, params, engine);

#if RIPPLE_ENABLE_MULTI_SIGN

    case ttSIGNER_LIST_SET:
        return transact_SetSignerList (txn, params, engine);

#endif // RIPPLE_ENABLE_MULTI_SIGN

    default:
        return temUNKNOWN;
    }
}

Transactor::Transactor (
    STTx const& txn,
    TransactionEngineParams params,
    TransactionEngine* engine,
    beast::Journal journal)
    : mTxn (txn)
    , mEngine (engine)
    , mParams (params)
    , mHasAuthKey (false)
    , mSigMaster (false)
    , m_journal (journal)
{
}

void Transactor::calculateFee ()
{
    mFeeDue = STAmount (mEngine->getLedger ()->scaleFeeLoad (
        calculateBaseFee (), mParams & tapADMIN));
}

std::uint64_t Transactor::calculateBaseFee ()
{
    // Returns the fee in fee units
    return getConfig ().TRANSACTION_FEE_BASE;
}

TER Transactor::payFee ()
{
    STAmount saPaid = mTxn.getTransactionFee ();

    if (!isLegalNet (saPaid))
        return temBAD_AMOUNT;

    // Only check fee is sufficient when the ledger is open.
    if ((mParams & tapOPEN_LEDGER) && saPaid < mFeeDue)
    {
        m_journal.trace << "Insufficient fee paid: " <<
            saPaid.getText () << "/" << mFeeDue.getText ();

        return telINSUF_FEE_P;
    }

    if (saPaid < zero || !saPaid.isNative ())
        return temBAD_FEE;

    if (!saPaid)
        return tesSUCCESS;

    if (mSourceBalance < saPaid)
    {
        m_journal.trace << "Insufficient balance:" <<
            " balance=" << mSourceBalance.getText () <<
            " paid=" << saPaid.getText ();

        if ((mSourceBalance > zero) && (!(mParams & tapOPEN_LEDGER)))
        {
            // Closed ledger, non-zero balance, less than fee
            mSourceBalance.clear ();
            mTxnAccount->setFieldAmount (sfBalance, mSourceBalance);
            return tecINSUFF_FEE;
        }

        return terINSUF_FEE_B;
    }

    // Deduct the fee, so it's not available during the transaction.
    // Will only write the account back, if the transaction succeeds.

    mSourceBalance -= saPaid;
    mTxnAccount->setFieldAmount (sfBalance, mSourceBalance);

    return tesSUCCESS;
}

TER Transactor::checkSeq ()
{
    std::uint32_t t_seq = mTxn.getSequence ();
    std::uint32_t a_seq = mTxnAccount->getFieldU32 (sfSequence);

    m_journal.trace << "Aseq=" << a_seq << ", Tseq=" << t_seq;

    if (t_seq != a_seq)
    {
        if (a_seq < t_seq)
        {
            m_journal.trace << "applyTransaction: transaction has future sequence number";

            return terPRE_SEQ;
        }
        else
        {
            if (mEngine->getLedger ()->hasTransaction (mTxn.getTransactionID ()))
                return tefALREADY;
        }

        m_journal.warning << "applyTransaction: transaction has past sequence number";

        return tefPAST_SEQ;
    }

    // Deprecated: Do not use
    if (mTxn.isFieldPresent (sfPreviousTxnID) &&
            (mTxnAccount->getFieldH256 (sfPreviousTxnID) != mTxn.getFieldH256 (sfPreviousTxnID)))
        return tefWRONG_PRIOR;

    if (mTxn.isFieldPresent (sfAccountTxnID) &&
            (mTxnAccount->getFieldH256 (sfAccountTxnID) != mTxn.getFieldH256 (sfAccountTxnID)))
        return tefWRONG_PRIOR;

    if (mTxn.isFieldPresent (sfLastLedgerSequence) &&
            (mEngine->getLedger()->getLedgerSeq() > mTxn.getFieldU32 (sfLastLedgerSequence)))
        return tefMAX_LEDGER;

    mTxnAccount->setFieldU32 (sfSequence, t_seq + 1);

    if (mTxnAccount->isFieldPresent (sfAccountTxnID))
        mTxnAccount->setFieldH256 (sfAccountTxnID, mTxn.getTransactionID ());

    return tesSUCCESS;
}

// check stuff before you bother to lock the ledger
TER Transactor::preCheck ()
{
    mTxnAccountID   = mTxn.getSourceAccount ().getAccountID ();

    if (!mTxnAccountID)
    {
        m_journal.warning << "applyTransaction: bad transaction source id";
        return temBAD_SRC_ACCOUNT;
    }

    // Extract signing key
    // Transactions contain a signing key.  This allows us to trivially verify a
    // transaction has at least been properly signed without going to disk.
    // Each transaction also notes a source account id. This is used to verify
    // that the signing key is associated with the account.
    // XXX This could be a lot cleaner to prevent unnecessary copying.
    mSigningPubKey = RippleAddress::createAccountPublic (mTxn.getSigningPubKey ());

    // Consistency: really signed.
    if (!mTxn.isKnownGood ())
    {
        if (mTxn.isKnownBad () ||
            (!(mParams & tapNO_CHECK_SIGN) && !mTxn.checkSign ()))
        {
            mTxn.setBad ();
            m_journal.warning << "applyTransaction: Invalid transaction (bad signature)";
            return temINVALID;
        }

        mTxn.setGood ();
    }

    return tesSUCCESS;
}

TER Transactor::apply ()
{
    TER terResult (preCheck ());

    if (terResult != tesSUCCESS)
        return (terResult);

    mTxnAccount = mEngine->entryCache (ltACCOUNT_ROOT,
        getAccountRootIndex (mTxnAccountID));
    calculateFee ();

    // Find source account
    // If are only forwarding, due to resource limitations, we might verifying
    // only some transactions, this would be probabilistic.

    if (!mTxnAccount)
    {
        if (mustHaveValidAccount ())
        {
            m_journal.trace <<
                "applyTransaction: delay transaction: source account does not exist " <<
                mTxn.getSourceAccount ().humanAccountID ();
            return terNO_ACCOUNT;
        }
    }
    else
    {
        mPriorBalance   = mTxnAccount->getFieldAmount (sfBalance);
        mSourceBalance  = mPriorBalance;
        mHasAuthKey     = mTxnAccount->isFieldPresent (sfRegularKey);
    }

    terResult = checkSeq ();

    if (terResult != tesSUCCESS) return (terResult);

    terResult = payFee ();

    if (terResult != tesSUCCESS) return (terResult);

    terResult = checkSign ();

    if (terResult != tesSUCCESS) return (terResult);

    if (mTxnAccount)
        mEngine->entryModify (mTxnAccount);

    return doApply ();
}

TER Transactor::checkSign ()
{
    // If the mSigningPubKey is empty, then we must be multi-signing.
    TER const signingTER = mSigningPubKey.getAccountPublic ().empty () ?
        checkMultiSign () : checkSingleSign ();

    return signingTER;
}

TER Transactor::checkSingleSign ()
{
    // Consistency: Check signature
    // Verify the transaction's signing public key is authorized for signing.
    if (mSigningPubKey.getAccountID () == mTxnAccountID)
    {
        // Authorized to continue.
        mSigMaster = true;
        if (mTxnAccount->isFlag(lsfDisableMaster))
            return tefMASTER_DISABLED;
    }
    else if (mHasAuthKey &&
        (mSigningPubKey.getAccountID () ==
            mTxnAccount->getFieldAccount160 (sfRegularKey)))
    {
        // Authorized to continue.
    }
    else if (mHasAuthKey)
    {
        m_journal.trace <<
            "applyTransaction: Delay: Not authorized to use account.";
        return tefBAD_AUTH;
    }
    else
    {
        m_journal.trace <<
            "applyTransaction: Invalid: Not authorized to use account.";
        return temBAD_AUTH_MASTER;
    }

    return tesSUCCESS;
}

TER Transactor::checkMultiSign ()
{
    STArray const& txnSigners (mTxn.getFieldArray (sfSigningAccounts));

    // The TxnAccount must be multi-signed for this to work.
    // See if there's an ltSIGNER_LIST for this account.
    uint256 const index = getSignerListIndex (mTxnAccountID);

    // Get a vector of the account's signers.
    std::uint32_t quorum = std::numeric_limits <std::uint32_t>::max ();
    std::vector<SignerEntries::SignerEntry> accountSigners;
    {
        SLE::pointer accountSignersList =
            mEngine->view ().entryCache (ltSIGNER_LIST, index);

        // If the signer list doesn't exist the account is not multi-signing.
        if (!accountSignersList)
        {
            m_journal.trace <<
                "applyTransaction: Invalid: Not authorized to use account.";
            return temBAD_AUTH_MASTER;
        }
        quorum = accountSignersList->getFieldU32 (sfSignerQuorum);

        SignerEntries::Decoded signersOnAccountDecode =
            SignerEntries::deserialize (
                *accountSignersList, m_journal, "ledger");

        if (signersOnAccountDecode.ter != tesSUCCESS)
            return signersOnAccountDecode.ter;

        accountSigners = std::move (signersOnAccountDecode.vec);
    }

    // Walk the SignerEntries performing a variety of checks and see if
    // the quorum is met.

    // Both the txnSigners and accountSigners are sorted by account.  So
    // matching transaction signers to account signers should be a simple
    // linear walk.  *All* signers must be valid or the transaction fails.
    std::uint32_t weightSum = 0;
    auto accountSignersItr = accountSigners.begin ();
    for (auto const& txnSigner : txnSigners)
    {
        // See if this is a valid signer.
        Account const txnSignerID =
            txnSigner.getFieldAccount (sfAccount).getAccountID ();

        // Attempt to match the txnSignerID with an AccountSigner
        while (accountSignersItr->account < txnSignerID)
        {
            if (++accountSignersItr == accountSigners.end ())
            {
                m_journal.trace <<
                    "applyTransaction: Invalid SignerEntry.Account.";
                return tefBAD_SIGNATURE;
            }
        }
        if (accountSignersItr->account != txnSignerID)
        {
            // The txnSignerID is not in the SignerEntries.
            m_journal.trace <<
                "applyTransaction: Invalid SignerEntry.Account.";
            return tefBAD_SIGNATURE;
        }

        // We found the txnSignerID in the list of valid signers.  Now we need
        // to compute the accountID that is associated with the signer's public
        // key.
        RippleAddress const txnSignerPubKey =
            RippleAddress::createAccountPublic (
                txnSigner.getFieldVL (sfPublicKey));

        Account const txnSignerAcctFromPubKey = txnSignerPubKey.getAccountID ();

        // Verify that the txnSignerID and the txnSignerAcctFromPubKey belong
        // together.  Here is are the rules:
        //
        //   1. "Phantom account": an account that is not in the ledger
        //      A. If txnSignerID == txnSignerAcctFromPubKey and the
        //         txnSignerID is not in the ledger then we have a phantom
        //         account.
        //      B. Phantom accounts are always allowed as multi-signers.
        //
        //   2. "Master Key"
        //      A. txnSignerID == txnSignerAcctFromPubKey, and txnSignerID is
        //         in the ledger.
        //      B. If the txnSignerID in the ledger does not have the
        //         asfDisableMaster flag set, then the signature is allowed.
        //
        //   3. "Regular Key"
        //      A. txnSignerID != txnSignerAcctFromPubKey, and txnSignerID is
        //         in the ledger.
        //      B. If txnSignerAcctFromPubKey == txnSignerID.RegularKey (from
        //         ledger) then the signature is allowed.
        //
        // No other signatures are allowed.  (January 2015)

        // In any of these cases we need to know whether the account is in
        // the ledger.  Determine that now.
        uint256 const signerAccountIndex = getAccountRootIndex (txnSignerID);
        SLE::pointer signersAccountRoot =
            mEngine->view ().entryCache (ltACCOUNT_ROOT, signerAccountIndex);

        if (txnSignerAcctFromPubKey == txnSignerID)
        {
            // Either Phantom or Master.  Phantom's automatically get a pass.
            if (signersAccountRoot)
            {
                // Master Key.  Account may not have asfDisableMaster set.
                std::uint32_t const signerAccountFlags =
                    signersAccountRoot->getFieldU32 (sfFlags);

                if (signerAccountFlags & lsfDisableMaster)
                {
                    m_journal.trace <<
                        "applyTransaction: MultiSignature lsfDisableMaster.";
                    return tefMASTER_DISABLED;
                }
            }
        }
        else
        {
            // May be a Regular Key.  Let's find out.
            // Public key must hash to the account's regular key.
            if (!signersAccountRoot->isFieldPresent (sfRegularKey))
            {
                m_journal.trace <<
                    "applyTransaction: Account lacks RegularKey.";
                return tefBAD_SIGNATURE;
            }
            if (txnSignerAcctFromPubKey !=
                signersAccountRoot->getFieldAccount160 (sfRegularKey))
            {
                m_journal.trace <<
                    "applyTransaction: Account doesn't match RegularKey.";
                return tefBAD_SIGNATURE;
            }
        }

        // The signer is legitimate.  Add their weight toward the quorum.
        weightSum += accountSignersItr->weight;
    }

    // Cannot perform transaction if quorum is not met.
    if (weightSum < quorum)
    {
        m_journal.trace <<
            "applyTransaction: MultiSignature failed to meet quorum.";
        return tefBAD_QUORUM;
    }

    // Met the quorum.  Continue.
    return tesSUCCESS;
}

}
