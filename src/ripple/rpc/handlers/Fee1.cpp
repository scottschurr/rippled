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

#include <org/xrpl/rpc/v1/xrp_ledger.grpc.pb.h>

#include <ripple/app/ledger/OpenLedger.h>
#include <ripple/app/main/Application.h>
#include <ripple/app/misc/TxQ.h>
#include <ripple/basics/mulDiv.h>
#include <ripple/json/json_reader.h>
#include <ripple/protocol/ErrorCodes.h>
#include <ripple/protocol/Feature.h>
#include <ripple/rpc/Context.h>
#include <ripple/rpc/GRPCHandlers.h>

namespace ripple {

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
    // In order to keep the RPC and gRPC responses synchronized, generate
    // the RPC response by generating JSON from the gRPC output.  In this
    // way if the gRPC response changes then so will the RPC response.
    //
    // NOTE: DON'T JUST HACK SOMETHING INTO THE RPC RESPONSE.  Please.
    //
    // If you must hack, then hack it into the gRPC response.  This will
    // maintain the parallel output between gRPC and RPC.  Thanks.

    RPC::GRPCContext<org::xrpl::rpc::v1::GetFeeRequest> grpcContext{
        {context}, org::xrpl::rpc::v1::GetFeeRequest{}};

    auto [grpcReply, grpcStatus] = doFeeGrpc(grpcContext);

    auto fail = [&context]() {
        RPC::inject_error(rpcINTERNAL, context.params);
        return context.params;
    };

    if (!grpcStatus.ok())
        return fail();

    Json::Value rpcFee;
    {
        // Convert the gRPC response into JSON text.  Then convert the text
        // to a Json::Value.
        std::string jsonTxt;

        using namespace google::protobuf::util;
        JsonPrintOptions options;
        options.always_print_primitive_fields = true;
        options.preserve_proto_field_names = true;

        MessageToJsonString(grpcReply, &jsonTxt, options);
        Json::Reader().parse(jsonTxt, rpcFee);
    }

    // Unfortunately the gRPC and historic RPC outputs are not aligned on all
    // fronts.  In particular the gRPC "fee" object must be converted into the
    // RPC "drops" object.  Do that now.
    {
        Json::Value grpcFees = rpcFee.removeMember(jss::fee);
        std::vector<std::string> const names = grpcFees.getMemberNames();

        auto& rpcDrops = rpcFee[jss::drops] = Json::objectValue;
        for (std::string const& name : names)
        {
            Json::Value const& drops = grpcFees[name][jss::drops];
            if (!drops.isString())
                return fail();

            rpcDrops[name] = drops;
        }
    }

    // Additionally, we may need to remove the max_queue_size entry.  Since
    // we have
    //    options.always_print_primitive_fields = true
    // a max_queue_size that is supposed to be missing may come back as zero.
    //
    // To accommodate that, if max_queue_size is zero then remove the field.
    if (rpcFee[jss::max_queue_size] == "0")
        rpcFee.removeMember(jss::max_queue_size);

    // Indicate success and we're good to go.
    rpcFee[jss::status] = jss::success;
    return rpcFee;
}

}  // namespace ripple
