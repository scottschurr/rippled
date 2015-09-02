//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2014 Ripple Labs Inc.

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

#ifndef RIPPLE_RPC_TRANSACTIONSIGN_H_INCLUDED
#define RIPPLE_RPC_TRANSACTIONSIGN_H_INCLUDED

#include <ripple/app/ledger/LedgerMaster.h>
#include <ripple/app/misc/NetworkOPs.h>
#include <ripple/app/main/Application.h>     // getApp()
#include <ripple/server/Role.h>

namespace ripple {
namespace RPC {

namespace detail {

// A function to auto-fill fees.
Json::Value checkFee (
    Json::Value& request,
    Role const role,
    bool doAutoFill,
    LoadFeeTrack const& feeTrack,
    std::shared_ptr<ReadView const>& ledger);

// Return a std::function<> that calls NetworkOPs::processTransaction.
using ProcessTransactionFn =
    std::function<void (Transaction::pointer& transaction,
        bool bAdmin, bool bLocal, NetworkOPs::FailHard failType)>;

inline ProcessTransactionFn getProcessTxnFn (NetworkOPs& netOPs)
{
    return [&netOPs](Transaction::pointer& transaction,
        bool bAdmin, bool bLocal, NetworkOPs::FailHard failType)
    {
        netOPs.processTransaction(transaction, bAdmin, bLocal, failType);
    };
}

} // namespace detail

/** Returns a Json::objectValue. */
Json::Value transactionSign (
    Json::Value params,  // Passed by value so it can be modified locally.
    NetworkOPs::FailHard failType,
    Role role,
    int validatedLedgerAge,
    LoadFeeTrack const& feeTrack,
    std::shared_ptr<ReadView const> ledger);

/** Returns a Json::objectValue. */
inline
Json::Value transactionSign (
    Json::Value const& params,
    NetworkOPs::FailHard failType,
    NetworkOPs& netOPs,
    Role role)
{
    return transactionSign (
        params,
        failType,
        role,
        getApp().getLedgerMaster().getValidatedLedgerAge(),
        getApp().getFeeTrack(),
        getApp().getLedgerMaster().getCurrentLedger());
}

/** Returns a Json::objectValue. */
Json::Value transactionSubmit (
    Json::Value params,  // Passed by value so it can be modified locally.
    NetworkOPs::FailHard failType,
    Role role,
    int validatedLedgerAge,
    LoadFeeTrack const& feeTrack,
    std::shared_ptr<ReadView const> ledger,
    detail::ProcessTransactionFn const& processTransaction);

/** Returns a Json::objectValue. */
inline
Json::Value transactionSubmit (
    Json::Value const& params,
    NetworkOPs::FailHard failType,
    NetworkOPs& netOPs,
    Role role)
{
    return transactionSubmit (
        params,
        failType,
        role,
        getApp().getLedgerMaster().getValidatedLedgerAge(),
        getApp().getFeeTrack(),
        getApp().getLedgerMaster().getCurrentLedger(),
        detail::getProcessTxnFn (netOPs));
}

/** Returns a Json::objectValue. */
Json::Value transactionSignFor (
    Json::Value params,  // Passed by value so it can be modified locally.
    NetworkOPs::FailHard failType,
    Role role,
    int validatedLedgerAge,
    LoadFeeTrack const& feeTrack,
    std::shared_ptr<ReadView const> ledger);

/** Returns a Json::objectValue. */
inline
Json::Value transactionSignFor (
    Json::Value const& params,
    NetworkOPs::FailHard failType,
    NetworkOPs& netOPs,
    Role role)
{
    return transactionSignFor (
        params,
        failType,
        role,
        getApp().getLedgerMaster().getValidatedLedgerAge(),
        getApp().getFeeTrack(),
        getApp().getLedgerMaster().getCurrentLedger());
}

/** Returns a Json::objectValue. */
Json::Value transactionSubmitMultiSigned (
    Json::Value params,  // Passed by value so it can be modified locally.
    NetworkOPs::FailHard failType,
    Role role,
    int validatedLedgerAge,
    LoadFeeTrack const& feeTrack,
    std::shared_ptr<ReadView const> ledger,
    detail::ProcessTransactionFn const& processTransaction);

/** Returns a Json::objectValue. */
inline
Json::Value transactionSubmitMultiSigned (
    Json::Value const& params,
    NetworkOPs::FailHard failType,
    NetworkOPs& netOPs,
    Role role)
{
    return transactionSubmitMultiSigned (
        params,
        failType,
        role,
        getApp().getLedgerMaster().getValidatedLedgerAge(),
        getApp().getFeeTrack(),
        getApp().getLedgerMaster().getCurrentLedger(),
        detail::getProcessTxnFn (netOPs));
}

} // RPC
} // ripple

#endif
