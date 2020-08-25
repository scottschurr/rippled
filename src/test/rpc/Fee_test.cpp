//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2020 Ripple Labs Inc.

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

#include <ripple/app/ledger/LedgerMaster.h>
#include <ripple/app/misc/TxQ.h>
#include <ripple/basics/mulDiv.h>
#include <ripple/core/DatabaseCon.h>
#include <ripple/json/json_reader.h>
#include <ripple/protocol/ErrorCodes.h>
#include <ripple/protocol/jss.h>
#include <ripple/rpc/GRPCHandlers.h>
#include <ripple/rpc/impl/RPCHelpers.h>
#include <test/jtx.h>
#include <test/jtx/Env.h>
#include <test/jtx/envconfig.h>
#include <test/rpc/GRPCTestClientBase.h>

#include <google/protobuf/util/json_util.h>

namespace ripple {
namespace test {

class Fee_test : public beast::unit_test::suite
{
    class GrpcFeeClient : public GRPCTestClientBase
    {
    public:
        org::xrpl::rpc::v1::GetFeeRequest request;
        org::xrpl::rpc::v1::GetFeeResponse reply;

        explicit GrpcFeeClient(std::string const& grpcPort)
            : GRPCTestClientBase(grpcPort)
        {
        }

        void
        GetFee()
        {
            status = stub_->GetFee(&context, request, &reply);
        }
    };

    std::pair<bool, org::xrpl::rpc::v1::GetFeeResponse>
    grpcGetFee(std::string const& grpcPort)
    {
        GrpcFeeClient client(grpcPort);
        client.GetFee();
        return std::pair<bool, org::xrpl::rpc::v1::GetFeeResponse>(
            client.status.ok(), client.reply);
    }

    void
    testFeeGrpc()
    {
        testcase("Test Fee Grpc");

        using namespace test::jtx;
        std::unique_ptr<Config> config = envconfig(addGrpcConfig);
        std::string grpcPort = *(*config)["port_grpc"].get<std::string>("port");
        Env env(*this, std::move(config));
        Account A1{"A1"};
        Account A2{"A2"};
        env.fund(XRP(10000), A1);
        env.fund(XRP(10000), A2);
        env.close();
        env.trust(A2["USD"](1000), A1);
        env.close();
        for (int i = 0; i < 7; ++i)
        {
            env(pay(A2, A1, A2["USD"](100)));
            if (i == 4)
                env.close();
        }

        auto view = env.current();

        auto const metrics = env.app().getTxQ().getMetrics(*env.current());

        auto const result = grpcGetFee(grpcPort);

        BEAST_EXPECT(result.first == true);

        auto reply = result.second;

        // current ledger data
        BEAST_EXPECT(reply.current_ledger_size() == metrics.txInLedger);
        BEAST_EXPECT(reply.current_queue_size() == metrics.txCount);
        BEAST_EXPECT(reply.expected_ledger_size() == metrics.txPerLedger);
        BEAST_EXPECT(reply.ledger_current_index() == view->info().seq);
        BEAST_EXPECT(reply.max_queue_size() == *metrics.txQMaxSize);

        // fee levels data
        org::xrpl::rpc::v1::FeeLevels& levels = *reply.mutable_levels();
        BEAST_EXPECT(levels.median_level() == metrics.medFeeLevel);
        BEAST_EXPECT(levels.minimum_level() == metrics.minProcessingFeeLevel);
        BEAST_EXPECT(levels.open_ledger_level() == metrics.openLedgerFeeLevel);
        BEAST_EXPECT(levels.reference_level() == metrics.referenceFeeLevel);

        // fee data
        org::xrpl::rpc::v1::Fee& fee = *reply.mutable_fee();
        auto const baseFee = view->fees().base;
        BEAST_EXPECT(
            fee.base_fee().drops() ==
            toDrops(metrics.referenceFeeLevel, baseFee));
        BEAST_EXPECT(
            fee.minimum_fee().drops() ==
            toDrops(metrics.minProcessingFeeLevel, baseFee));
        BEAST_EXPECT(
            fee.median_fee().drops() == toDrops(metrics.medFeeLevel, baseFee));
        auto openLedgerFee =
            toDrops(metrics.openLedgerFeeLevel - FeeLevel64{1}, baseFee) + 1;
        BEAST_EXPECT(fee.open_ledger_fee().drops() == openLedgerFee.drops());
    }

    // Helper function for testCompareFeeGrpcToRpc().  This can't be declared
    // as a lambda, since a lambda cannot recurse into itself.
    int
    compareFields(Json::Value& grpcObj, Json::Value& rpcObj)
    {
        BEAST_EXPECT(grpcObj.isObject());
        BEAST_EXPECT(rpcObj.isObject());

        Json::Value::Members memberNames = grpcObj.getMemberNames();
        for (auto const& memberName : memberNames)
        {
            if (grpcObj[memberName].isObject())
            {
                if (compareFields(grpcObj[memberName], rpcObj[memberName]) != 0)
                {
                    fail(
                        std::string(
                            "Fee inner object `" + memberName +
                            "had different field counts between gRPC and RPC"),
                        __FILE__,
                        __LINE__);
                }
                else
                    pass();
            }
            else
            {
                if (grpcObj[memberName] != rpcObj[memberName])
                    fail(
                        std::string("gRPC did not match RPC value for '") +
                            memberName + "'",
                        __FILE__,
                        __LINE__);
                else
                    pass();
            }
            grpcObj.removeMember(memberName);
            rpcObj.removeMember(memberName);
        }
        // Inner objects should be the same size.  Return the size delta
        // so the caller can determine whether to check.
        return (static_cast<int>(rpcObj.size() - grpcObj.size()));
    };

    void
    testCompareFeeGrpcToRpc()
    {
        testcase("Test Compare Fee Grpc To Rpc");

        using namespace test::jtx;
        std::unique_ptr<Config> config = envconfig(addGrpcConfig);
        std::string grpcPort = *(*config)["port_grpc"].get<std::string>("port");
        Env env(*this, std::move(config));
        Account A1{"A1"};
        Account A2{"A2"};
        env.fund(XRP(10000), A1);
        env.fund(XRP(10000), A2);
        env.close();
        env.trust(A2["USD"](1000), A1);
        env.close();
        for (int i = 0; i < 9; ++i)
        {
            env(pay(A2, A1, A2["USD"](100)));
            if (i == 4)
                env.close();
        }

        // Examine RPC and gRPC output as JSON so they are easier to compare.
        Json::Value rpcFee = env.rpc("fee")["result"];
        Json::Value grpcFee;
        {
            auto const grpcResult = grpcGetFee(grpcPort);
            if (!BEAST_EXPECT(grpcResult.first == true))
                return;

            std::string jsonTxt;

            using namespace google::protobuf::util;
            JsonPrintOptions options;
            options.always_print_primitive_fields = true;
            options.preserve_proto_field_names = true;

            MessageToJsonString(grpcResult.second, &jsonTxt, options);
            Json::Reader().parse(jsonTxt, grpcFee);
        }

        // The structures of the "fee" / "drops" results are different
        // between gRPC and RPC.  Get that comparison over with before
        // moving on to simpler stuff.
        {
            Json::Value grpcFees = grpcFee.removeMember(jss::fee);
            Json::Value rpcDrops = rpcFee.removeMember(jss::drops);
            BEAST_EXPECT(grpcFees.size() == rpcDrops.size());

            Json::Value::Members feeNames = grpcFees.getMemberNames();
            for (auto const& feeName : feeNames)
            {
                if (grpcFees[feeName][jss::drops] != rpcDrops[feeName])
                {
                    fail(
                        std::string("gRPC did not match RPC value for '") +
                            feeName + "'",
                        __FILE__,
                        __LINE__);
                }
                else
                {
                    pass();
                }
                grpcFees.removeMember(feeName);
                rpcDrops.removeMember(feeName);
            }
            BEAST_EXPECT(grpcFees.size() == 0);
            BEAST_EXPECT(rpcDrops.size() == 0);
        }

        // Invoke the comparison.
        compareFields(grpcFee, rpcFee);

        // We expect all fields in grpcFee to be accounted for.
        BEAST_EXPECT(grpcFee.size() == 0);

        // We expect one leftover field in rpcFee.
        BEAST_EXPECT(rpcFee.size() == 1);
        BEAST_EXPECT(rpcFee[jss::status] == jss::success);
    }

public:
    void
    run() override
    {
        testFeeGrpc();
        testCompareFeeGrpcToRpc();
    }
};

BEAST_DEFINE_TESTSUITE(Fee, app, ripple);

}  // namespace test
}  // namespace ripple
