//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2015 Ripple Labs Inc.

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

#include <ripple/app/ledger/OpenLedger.h>
#include <ripple/app/main/Application.h>
#include <ripple/app/misc/TxQ.h>
#include <ripple/basics/mulDiv.h>
#include <ripple/protocol/ErrorCodes.h>
#include <ripple/protocol/Feature.h>
#include <ripple/rpc/Context.h>
#include <ripple/rpc/GRPCHandlers.h>

namespace ripple {

//------------------------------------------------------------------------------
// fee RPC command entry point.
//
// The RPC command has the following output format
//
// {
//    "current_ledger_size" : "4",
//    "current_queue_size" : "0",
//    "drops" : {
//       "base_fee" : "10",
//       "median_fee" : "5000",
//       "minimum_fee" : "10",
//       "open_ledger_fee" : "10"
//    },
//    "expected_ledger_size" : "1000",
//    "ledger_current_index" : 6,
//    "levels" : {
//       "median_level" : "128000",
//       "minimum_level" : "256",
//       "open_ledger_level" : "256",
//       "reference_level" : "256"
//    },
//    "max_queue_size" : "20000",
//    "status" : "success"
// }
Json::Value
doFee(RPC::JsonContext& context)
{
    boost::optional<TxQ::FeeData> feeData =
        context.app.getTxQ().getFeeRPCData(context.app);

    if (!feeData)
    {
        RPC::inject_error(rpcINTERNAL, context.params);
        return context.params;
    }

    Json::Value ret(Json::objectValue);

    // current ledger data
    ret[jss::ledger_current_index] = feeData->ledger_current_index;
    ret[jss::expected_ledger_size] =
        std::to_string(feeData->expected_ledger_size);
    ret[jss::current_ledger_size] =
        std::to_string(feeData->current_ledger_size);
    ret[jss::current_queue_size] = std::to_string(feeData->current_queue_size);
    if (feeData->max_queue_size)
        ret[jss::max_queue_size] = std::to_string(*feeData->max_queue_size);

    {
        // fee levels data
        auto& levels = ret[jss::levels] = Json::objectValue;

        levels[jss::reference_level] =
            to_string(feeData->levels.reference_level);
        levels[jss::minimum_level] = to_string(feeData->levels.minimum_level);
        levels[jss::median_level] = to_string(feeData->levels.median_level);
        levels[jss::open_ledger_level] =
            to_string(feeData->levels.open_ledger_level);
    }
    {
        // fee data
        auto& drops = ret[jss::drops] = Json::objectValue;

        drops[jss::base_fee] = to_string(feeData->drops.base_fee);
        drops[jss::minimum_fee] = to_string(feeData->drops.minimum_fee);
        drops[jss::median_fee] = to_string(feeData->drops.median_fee);
        drops[jss::open_ledger_fee] = to_string(feeData->drops.open_ledger_fee);
    }
    return ret;
}

//------------------------------------------------------------------------------
// fee gRPC command entry point.
//
// The gRPC command has the following output format if it were converted
// to JSON:
//
// {
//    "currentLedgerSize" : "4",
//    "expectedLedgerSize" : "1000",
//    "fee" : {
//       "baseFee" : {
//          "drops" : "10"
//       },
//       "medianFee" : {
//          "drops" : "5000"
//       },
//       "minimumFee" : {
//          "drops" : "10"
//       },
//       "openLedgerFee" : {
//          "drops" : "10"
//       }
//    },
//    "ledgerCurrentIndex" : 6,
//    "levels" : {
//       "medianLevel" : "128000",
//       "minimumLevel" : "256",
//       "openLedgerLevel" : "256",
//       "referenceLevel" : "256"
//    },
//    "maxQueueSize" : "20000"
// }
std::pair<org::xrpl::rpc::v1::GetFeeResponse, grpc::Status>
doFeeGrpc(RPC::GRPCContext<org::xrpl::rpc::v1::GetFeeRequest>& context)
{
    org::xrpl::rpc::v1::GetFeeResponse reply;
    grpc::Status const status = grpc::Status::OK;

    boost::optional<TxQ::FeeData> feeData =
        context.app.getTxQ().getFeeRPCData(context.app);

    if (!feeData)
    {
        return {reply, status};
    }

    // current ledger data
    reply.set_current_ledger_size(feeData->current_ledger_size);
    reply.set_current_queue_size(feeData->current_queue_size);
    reply.set_expected_ledger_size(feeData->expected_ledger_size);
    reply.set_ledger_current_index(feeData->ledger_current_index);
    if (feeData->max_queue_size)
        reply.set_max_queue_size(*feeData->max_queue_size);

    // fee levels data
    org::xrpl::rpc::v1::FeeLevels& levels = *reply.mutable_levels();
    levels.set_median_level(feeData->levels.median_level.value());
    levels.set_minimum_level(feeData->levels.minimum_level.value());
    levels.set_open_ledger_level(feeData->levels.open_ledger_level.value());
    levels.set_reference_level(feeData->levels.reference_level.value());

    // fee data
    org::xrpl::rpc::v1::Fee& fee = *reply.mutable_fee();
    fee.mutable_base_fee()->set_drops(feeData->drops.base_fee.drops());
    fee.mutable_minimum_fee()->set_drops(feeData->drops.minimum_fee.drops());
    fee.mutable_median_fee()->set_drops(feeData->drops.median_fee.drops());
    fee.mutable_open_ledger_fee()->set_drops(
        feeData->drops.open_ledger_fee.drops());

    return {reply, status};
}
}  // namespace ripple
