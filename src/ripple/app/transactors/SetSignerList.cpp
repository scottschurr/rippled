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

#include "SetSignerList.h"

namespace ripple {

class SetSignerList final
    : public Transactor
{
public:
    SetSignerList (
        SerializedTransaction const& txn,
        TransactionEngineParams params,
        TransactionEngine* engine)
        : Transactor (
            txn,
            params,
            engine,
            deprecatedLogs().journal("SetSignerList"))
    {

    }

    TER doApply () override;

private:
    // Handlers for supported requests
    TER createSignerList ();
    TER destroySignerList ();
    TER addSigners ();
    TER removeSigners ();
    TER changeSignerWeights ();
    TER changeQuorumWeight ();

    // Deserialize SignerEntry
    struct SignerEntry
    {
        Account account;
        std::uint16_t weight;

        // For sorting to look for duplicate accounts
        friend bool operator< (SignerEntry const& lhs, SignerEntry const& rhs)
        {
            return lhs.account < rhs.account;
        }

        friend bool operator== (SignerEntry const& lhs, SignerEntry const& rhs)
        {
            return lhs.account == rhs.account;
        }
    };

    typedef std::vector <SignerEntry> SignerEntryArray;

    struct SignerEntryArrayDecode
    {
        SignerEntryArray vec;
        TER ter = temMALFORMED;
    };
    SignerEntryArrayDecode deserializeSignerEntryArray ();

    TER validateQuorumAndSignerEntries (
        std::uint32_t quorum, SignerEntryArray& signers);

    static std::size_t const maxSigners = 32;
};

TER SetSignerList::doApply ()
{
    assert (mTxnAccount);

    // No implementation yet.  TBD...
    return tesSUCCESS;    // Don't return tefFAILURE, it can wedge the account
}

TER
transact_SetSignerList (
    SerializedTransaction const& txn,
    TransactionEngineParams params,
    TransactionEngine* engine)
{
    return SetSignerList (txn, params, engine).apply ();
}


// inline
// std::unique_ptr <Transactor>
// make_SetSignerList (
//     SerializedTransaction const& txn,
//     TransactionEngineParams params,
//     TransactionEngine* engine)
// {
//     return std::make_unique <SetSignerList> (txn, params, engine);
// }

} // namespace ripple
