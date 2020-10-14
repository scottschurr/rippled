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

#include <grpc/status.h>
#include <org/xrpl/rpc/v1/xrp_ledger.grpc.pb.h>

#include <ripple/app/ledger/LedgerMaster.h>
#include <ripple/app/main/Application.h>
#include <ripple/app/misc/TxQ.h>
#include <ripple/basics/StringUtilities.h>
#include <ripple/json/Object.h>
#include <ripple/json/json_reader.h>
#include <ripple/json/json_value.h>
#include <ripple/ledger/ReadView.h>
#include <ripple/protocol/ErrorCodes.h>
#include <ripple/protocol/Indexes.h>
#include <ripple/protocol/UintTypes.h>
#include <ripple/protocol/jss.h>
#include <ripple/rpc/Context.h>
#include <ripple/rpc/GRPCHandlers.h>
#include <ripple/rpc/impl/GRPCHelpers.h>
#include <ripple/rpc/impl/RPCHelpers.h>

#include <ripple/beast/core/LexicalCast.h>

namespace ripple {

// {
//   account: <ident>,
//   strict: <bool>        // optional (default false)
//                         //   if true only allow public keys and addresses.
//   ledger_hash : <ledger>
//   ledger_index : <ledger_index>
//   signer_lists : <bool> // optional (default false)
//                         //   if true return SignerList(s).
//   queue : <bool>        // optional (default false)
//                         //   if true return information about transactions
//                         //   in the current TxQ, only if the requested
//                         //   ledger is open. Otherwise if true, returns an
//                         //   error.
// }

Json::Value
doAccountInfoOld(RPC::JsonContext& context)
{
    auto& params = context.params;

    std::string strIdent;
    if (params.isMember(jss::account))
        strIdent = params[jss::account].asString();
    else if (params.isMember(jss::ident))
        strIdent = params[jss::ident].asString();
    else
        return RPC::missing_field_error(jss::account);

    std::shared_ptr<ReadView const> ledger;
    auto result = RPC::lookupLedger(ledger, context);

    if (!ledger)
        return result;

    bool bStrict = params.isMember(jss::strict) && params[jss::strict].asBool();
    AccountID accountID;

    // Get info on account.

    auto jvAccepted = RPC::accountFromString(accountID, strIdent, bStrict);

    if (jvAccepted)
        return jvAccepted;

    auto const sleAccepted = ledger->read(keylet::account(accountID));
    if (sleAccepted)
    {
        auto const queue =
            params.isMember(jss::queue) && params[jss::queue].asBool();

        if (queue && !ledger->open())
        {
            // It doesn't make sense to request the queue
            // with any closed or validated ledger.
            RPC::inject_error(rpcINVALID_PARAMS, result);
            return result;
        }

        RPC::injectSLE(jvAccepted, *sleAccepted);
        result[jss::account_data] = jvAccepted;

        // Return SignerList(s) if that is requested.
        if (params.isMember(jss::signer_lists) &&
            params[jss::signer_lists].asBool())
        {
            // We put the SignerList in an array because of an anticipated
            // future when we support multiple signer lists on one account.
            Json::Value jvSignerList = Json::arrayValue;

            // This code will need to be revisited if in the future we support
            // multiple SignerLists on one account.
            auto const sleSigners = ledger->read(keylet::signers(accountID));
            if (sleSigners)
                jvSignerList.append(sleSigners->getJson(JsonOptions::none));

            result[jss::account_data][jss::signer_lists] =
                std::move(jvSignerList);
        }
        // Return queue info if that is requested
        if (queue)
        {
            Json::Value jvQueueData = Json::objectValue;

            auto const txs =
                context.app.getTxQ().getAccountTxs(accountID, *ledger);
            if (!txs.empty())
            {
                jvQueueData[jss::txn_count] =
                    static_cast<Json::UInt>(txs.size());

                auto& jvQueueTx = jvQueueData[jss::transactions];
                jvQueueTx = Json::arrayValue;

                std::uint32_t seqCount = 0;
                std::uint32_t ticketCount = 0;
                boost::optional<std::uint32_t> lowestSeq;
                boost::optional<std::uint32_t> highestSeq;
                boost::optional<std::uint32_t> lowestTicket;
                boost::optional<std::uint32_t> highestTicket;
                bool anyAuthChanged = false;
                XRPAmount totalSpend(0);

                // We expect txs to be returned sorted by SeqProxy.  Verify
                // that with a couple of asserts.
                SeqProxy prevSeqProxy = SeqProxy::sequence(0);
                for (auto const& tx : txs)
                {
                    Json::Value jvTx = Json::objectValue;

                    if (tx.seqProxy.isSeq())
                    {
                        assert(prevSeqProxy < tx.seqProxy);
                        prevSeqProxy = tx.seqProxy;
                        jvTx[jss::seq] = tx.seqProxy.value();
                        ++seqCount;
                        if (!lowestSeq)
                            lowestSeq = tx.seqProxy.value();
                        highestSeq = tx.seqProxy.value();
                    }
                    else
                    {
                        assert(prevSeqProxy < tx.seqProxy);
                        prevSeqProxy = tx.seqProxy;
                        jvTx[jss::ticket] = tx.seqProxy.value();
                        ++ticketCount;
                        if (!lowestTicket)
                            lowestTicket = tx.seqProxy.value();
                        highestTicket = tx.seqProxy.value();
                    }

                    jvTx[jss::fee_level] = to_string(tx.feeLevel);
                    if (tx.lastValid)
                        jvTx[jss::LastLedgerSequence] = *tx.lastValid;

                    jvTx[jss::fee] = to_string(tx.consequences.fee());
                    auto const spend = tx.consequences.potentialSpend() +
                        tx.consequences.fee();
                    jvTx[jss::max_spend_drops] = to_string(spend);
                    totalSpend += spend;
                    bool const authChanged = tx.consequences.isBlocker();
                    if (authChanged)
                        anyAuthChanged = authChanged;
                    jvTx[jss::auth_change] = authChanged;

                    jvQueueTx.append(std::move(jvTx));
                }

                if (seqCount)
                    jvQueueData[jss::sequence_count] = seqCount;
                if (ticketCount)
                    jvQueueData[jss::ticket_count] = ticketCount;
                if (lowestSeq)
                    jvQueueData[jss::lowest_sequence] = *lowestSeq;
                if (highestSeq)
                    jvQueueData[jss::highest_sequence] = *highestSeq;
                if (lowestTicket)
                    jvQueueData[jss::lowest_ticket] = *lowestTicket;
                if (highestTicket)
                    jvQueueData[jss::highest_ticket] = *highestTicket;

                jvQueueData[jss::auth_change_queued] = anyAuthChanged;
                jvQueueData[jss::max_spend_drops_total] = to_string(totalSpend);
            }
            else
                jvQueueData[jss::txn_count] = 0u;

            result[jss::queue_data] = std::move(jvQueueData);
        }
    }
    else
    {
        result[jss::account] = context.app.accountIDCache().toBase58(accountID);
        RPC::inject_error(rpcACT_NOT_FOUND, result);
    }
    return result;
}

std::pair<org::xrpl::rpc::v1::GetAccountInfoResponse, grpc::Status>
doAccountInfoGrpc(
    RPC::GRPCContext<org::xrpl::rpc::v1::GetAccountInfoRequest>& context)
{
    // Return values
    org::xrpl::rpc::v1::GetAccountInfoResponse result;
    grpc::Status status = grpc::Status::OK;

    // input
    org::xrpl::rpc::v1::GetAccountInfoRequest& params = context.params;

    // get ledger
    std::shared_ptr<ReadView const> ledger;
    RPC::Status const lgrStatus = RPC::ledgerFromRequest(ledger, context);
    if (lgrStatus || !ledger)
    {
        // Preserve the error information as JSON in the
        // grpc::Status::error_details
        std::string details;
        if (lgrStatus)
        {
            Json::Value detailsJson(Json::objectValue);
            lgrStatus.inject(detailsJson);
            details = detailsJson.toStyledString();
        }

        grpc::Status errorStatus;
        if (lgrStatus.toErrorCode() == rpcINVALID_PARAMS)
            errorStatus = grpc::Status(
                grpc::StatusCode::INVALID_ARGUMENT,
                lgrStatus.message(),
                details);

        else
            errorStatus = grpc::Status(
                grpc::StatusCode::NOT_FOUND, lgrStatus.message(), details);

        return {result, errorStatus};
    }

    result.set_ledger_index(ledger->info().seq);
    result.set_validated(
        RPC::isValidated(context.ledgerMaster, *ledger, context.app));

    // decode account
    AccountID accountID;
    std::string strIdent = params.account().address();
    error_code_i code =
        RPC::accountFromStringWithCode(accountID, strIdent, params.strict());
    if (code != rpcSUCCESS)
    {
        // Preserve the error information as JSON in the
        // grpc::Status::error_details
        std::string details;
        {
            Json::Value detailsJson(Json::objectValue);
            RPC::inject_error(code, detailsJson);
            details = detailsJson.toStyledString();
        }
        grpc::Status errorStatus{
            grpc::StatusCode::INVALID_ARGUMENT, "invalid account", details};
        return {result, errorStatus};
    }

    // get account data
    auto const sleAccepted = ledger->read(keylet::account(accountID));
    if (sleAccepted)
    {
        RPC::convert(*result.mutable_account_data(), *sleAccepted);

        // signer lists
        if (params.signer_lists())
        {
            auto const sleSigners = ledger->read(keylet::signers(accountID));
            if (sleSigners)
            {
                org::xrpl::rpc::v1::SignerList& signerListProto =
                    *result.mutable_signer_list();
                RPC::convert(signerListProto, *sleSigners);
            }
        }

        // queued transactions
        if (params.queue())
        {
            if (!ledger->open())
            {
                grpc::Status errorStatus{
                    grpc::StatusCode::INVALID_ARGUMENT,
                    "requested queue but ledger is not open"};
                return {result, errorStatus};
            }
            std::vector<TxQ::TxDetails> const txs =
                context.app.getTxQ().getAccountTxs(accountID, *ledger);
            org::xrpl::rpc::v1::QueueData& queueData =
                *result.mutable_queue_data();
            RPC::convert(queueData, txs);
        }
    }
    else
    {
        // Preserve the error information as JSON in the
        // grpc::Status::error_details
        std::string details;
        {
            Json::Value detailsJson(Json::objectValue);
            RPC::inject_error(rpcACT_NOT_FOUND, detailsJson);
            details = detailsJson.toStyledString();
        }
        grpc::Status errorStatus{
            grpc::StatusCode::INVALID_ARGUMENT, "invalid account", details};
        return {result, errorStatus};
    }

    return {result, status};
}

//------------------------------------------------------------------------------
// Helper functions for doAccountInfo

// Google's MessageToJsonString() returns some fields as base64 when we want
// to return them in hex.  Perform that conversion.
//
// On error an empty string is returned.
[[nodiscard]] std::string
base64toHex(std::string const& base64in)
{
    static constexpr std::uint8_t base64inv[]{
        // 0     1     2     3     4     5     6     7
        0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0,  // NUL - BEL
        0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0,  // BS - SI
        0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0,  // DLE - ETB
        0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0,  // CAN - US
        0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0,  // Space - '
        0xC0, 0xC0, 0xC0, 0x3E, 0xC0, 0xC0, 0xC0, 0x3F,  // ( - /
        0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3A, 0x3B,  // 0 - 7
        0x3C, 0x3D, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0,  // 8 - ?
        0xC0, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06,  // @ - G
        0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E,  // H - O
        0x0F, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16,  // P - W
        0x17, 0x18, 0x19, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0,  // X - _
        0xC0, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F, 0x20,  // ` - g
        0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28,  // h - o
        0x29, 0x2A, 0x2B, 0x2C, 0x2D, 0x2E, 0x2F, 0x30,  // p - w
        0x31, 0x32, 0x33, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0,  // x - DEL
        0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0,  // 0x80 - 0x87
        0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0,  // 0x88 - 0x8F
        0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0,  // 0x90 - 0x87
        0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0,  // 0x98 - 0x8F
        0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0,  // 0xA0 - 0xA7
        0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0,  // 0xA8 - 0xAF
        0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0,  // 0xB0 - 0xB7
        0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0,  // 0xB8 - 0xBF
        0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0,  // 0xC0 - 0xC7
        0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0,  // 0xC8 - 0xCF
        0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0,  // 0xD0 - 0xD7
        0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0,  // 0xD8 - 0xDF
        0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0,  // 0xE0 - 0xE7
        0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0,  // 0xE8 - 0xEF
        0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0,  // 0xF0 - 0xF7
        0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0,  // 0xF8 - 0xFF
    };
    // clang-format off
    static constexpr char hexFwd[]{
        '0', '1', '2', '3', '4', '5', '6', '7',  // 0x0 - 0x7
        '8', '9', 'A', 'B', 'C', 'D', 'E', 'F',  // 0x8 - 0xF
    };
    // clang-format on

    std::string out;
    out.reserve(base64in.size() + (base64in.size() / 2) + 1);

    std::size_t i = 0;
    while (i < base64in.size())
    {
        static_assert(sizeof(char) == sizeof(std::uint8_t));

        char const b64a = base64in[i++];
        if (b64a == '=')
            break;

        std::uint8_t const va = base64inv[b64a];
        if (va & 0xC0)
            return {};

        out.push_back(hexFwd[(va >> 2) & 0xF]);

        if (i >= base64in.size())
            break;

        char const b64b = base64in[i++];
        if (b64b == '=')
            break;

        std::uint8_t const vb = base64inv[b64b];
        if (vb & 0xC0)
            return {};

        out.push_back(hexFwd[((va & 0x3) << 2) | ((vb >> 4) & 0x3)]);
        out.push_back(hexFwd[vb & 0xF]);
    }
    return out;
}

template <typename Request>
[[nodiscard]] static RPC::Status
JsonLedgerToLedgerSpecifier(Request& req, RPC::JsonContext const& context)
{
    auto const& params = context.params;

    // The ledger may be specified using one of the following three
    // mutually exclusive fields:
    //   o jss::ledger (deprecated)
    //   o jss::ledger_index (uint32 or "current", "validated", or "closed")
    //   o jss::ledger_hash (uint256)
    auto hashValue = params[jss::ledger_hash];
    auto indexValue = params[jss::ledger_index];

    // Can only specify one of "ledger_hash" or "ledger_index".
    if (hashValue && indexValue)
        return {rpcINVALID_PARAMS, "IncompatibleArguments"};

    // We need to support the legacy "ledger" field.
    if (auto& legacyLedger = params[jss::ledger]; legacyLedger)
    {
        // Can't specify both "ledger" and "ledger_hash" or "ledger_index".
        if (hashValue || indexValue)
            return {rpcINVALID_PARAMS, "IncompatibleArguments"};

        if (legacyLedger.asString().size() > 12)
            hashValue = legacyLedger;
        else
            indexValue = legacyLedger;
    }

    std::string hashString;
    if (hashValue)
    {
        if (!hashValue.isString())
            return {rpcINVALID_PARAMS, "ledgerHashNotString"};

        uint256 ledgerHash;
        if (!ledgerHash.SetHex(hashValue.asString()))
            return {rpcINVALID_PARAMS, "ledgerHashMalformed"};

        hashString = hashValue.asString();
    }

    boost::optional<org::xrpl::rpc::v1::LedgerSpecifier::Shortcut> shortcut;
    boost::optional<std::uint32_t> index;
    if (indexValue)
    {
        std::string ledgerIndex = indexValue.asString();
        if (ledgerIndex == jss::current)
            shortcut = org::xrpl::rpc::v1::LedgerSpecifier::SHORTCUT_CURRENT;

        else if (ledgerIndex == jss::validated)
            shortcut = org::xrpl::rpc::v1::LedgerSpecifier::SHORTCUT_VALIDATED;

        else if (ledgerIndex == jss::closed)
            shortcut = org::xrpl::rpc::v1::LedgerSpecifier::SHORTCUT_CLOSED;

        else if (std::uint32_t iVal;
                 beast::lexicalCastChecked(iVal, ledgerIndex))
            index = iVal;

        else
            return {rpcINVALID_PARAMS, "ledgerIndexMalformed"};
    }

    if (shortcut || index || !hashString.empty())
    {
        org::xrpl::rpc::v1::LedgerSpecifier* const ledgerSpecifier =
            req.params.mutable_ledger();

        if (shortcut)
            ledgerSpecifier->set_shortcut(*shortcut);

        if (index)
            ledgerSpecifier->set_sequence(*index);

        if (!hashString.empty())
            ledgerSpecifier->set_hash(hashString);
    }
    return {rpcSUCCESS};
}

// Unwrap grpc-formatted JSON to produce old-style JSON.
[[nodiscard]] static bool
unwrap(Json::Value& where, char const* to, char const* from)
{
    // Remove "from", then insert its unlayered value into "to".
    Json::Value removed = where.removeMember(from);
    if (!removed)
        return false;

    Json::Value& layer = removed;
    while (layer.isObject())
    {
        std::vector<std::string> members = layer.getMemberNames();
        if (members.size() != 1)
            return false;

        layer = layer[members[0]];
    }
    if (!layer)
        return false;

    where[to] = layer;
    return true;
};

//------------------------------------------------------------------------------
Json::Value
doAccountInfo(RPC::JsonContext& context)
{
    // In order to keep the RPC and gRPC responses synchronized, generate
    // the RPC response by generating JSON from the gRPC output.  In this
    // way if the gRPC response changes then so will the RPC response.
    //
    // NOTE: DON'T JUST HACK SOMETHING INTO THE RPC RESPONSE.  Please.
    //
    // If you must hack, then hack it into the gRPC response.  This will
    // maintain the parallel output between gRPC and RPC.  Thanks.

    RPC::GRPCContext<org::xrpl::rpc::v1::GetAccountInfoRequest> grpcContext{
        {context}, org::xrpl::rpc::v1::GetAccountInfoRequest{}};

    auto const& params = context.params;

    grpcContext.params.set_strict(
        params.isMember(jss::strict) && params[jss::strict].asBool());

    grpcContext.params.set_queue(
        params.isMember(jss::queue) && params[jss::queue].asBool());

    grpcContext.params.set_signer_lists(
        params.isMember(jss::signer_lists) &&
        params[jss::signer_lists].asBool());

    AccountID accountID{};
    uint256 acctIndex{};
    {
        std::string strIdent;
        if (params.isMember(jss::account))
            strIdent = params[jss::account].asString();
        else if (params.isMember(jss::ident))
            strIdent = params[jss::ident].asString();
        else
            return RPC::missing_field_error(jss::account);

        grpcContext.params.mutable_account()->set_address(strIdent);

        // We're going to need the account index later.  Get that now.
        // in case there's an error in the account we can bail out early.
        auto jvAccepted = RPC::accountFromString(
            accountID, strIdent, grpcContext.params.strict());

        if (jvAccepted)
            return jvAccepted;

        acctIndex = keylet::account(accountID).key;
    }
    {
        RPC::Status const status =
            JsonLedgerToLedgerSpecifier(grpcContext, context);

        if (status)
        {
            Json::Value ret(Json::objectValue);
            status.inject(ret);
            return ret;
        }
    }

    auto [grpcReply, grpcStatus] = doAccountInfoGrpc(grpcContext);

    // Lambda to call if there's something unexpected in the conversion
    // from grpc format to old-style.
    auto internalFail = [&context]() {
        RPC::inject_error(rpcINTERNAL, context.params);
        return context.params;
    };

    if (!grpcStatus.ok())
    {
        std::string const details = grpcStatus.error_details();

        if (Json::Value detailsJson;
            !details.empty() && Json::Reader().parse(details, detailsJson))
            copyFrom(context.params, detailsJson);

        else
            return internalFail();

        return context.params;
    }

    Json::Value rpcAccountInfo;
    {
        // Convert the gRPC response into JSON text.  Then convert the text
        // to a Json::Value.
        std::string jsonTxt;

        using namespace google::protobuf::util;
        JsonPrintOptions options;
        options.always_print_primitive_fields = true;
        options.preserve_proto_field_names = true;

        MessageToJsonString(grpcReply, &jsonTxt, options);
        if (!Json::Reader().parse(jsonTxt, rpcAccountInfo))
            return internalFail();
    }

    // If the returned ledger_index is an open ledger then rename the field
    // to ledger_current_index.  That's how we match the historic RPC behavior.
    if (rpcAccountInfo.isMember(jss::ledger_index) &&
        context.ledgerMaster.getCurrentLedger()->seq() ==
            rpcAccountInfo[jss::ledger_index].asUInt())
    {
        Json::Value ledgerIndex =
            rpcAccountInfo.removeMember(jss::ledger_index);
        rpcAccountInfo[jss::ledger_current_index] = std::move(ledgerIndex);
    }

    Json::Value& acctData = rpcAccountInfo[jss::account_data];
    if (!acctData)
        return internalFail();

    // The JSON formatting is way different between grpc and the old code.
    // Convert from grpc-style to old-style.
    //
    // This lambda converts most of the object fields in a Json::Value
    // from gRPC-style to old-style
    auto convertObj =
        [](Json::Value& obj,
           std::set<std::string> const& ignores,
           std::map<std::string, char const*> const& irregulars) -> bool {
        // Extract the field names in obj and map them to old-style fields.
        std::vector<std::string> fieldNames = obj.getMemberNames();
        for (std::string const& fieldName : fieldNames)
        {
            // We want to ignore some gRPC fields because they are just fine
            // as they are or too complicated to handle in a generic way.
            if (ignores.count(fieldName) > 0)
                continue;

            // Handle the irregular fields and treat the remainder as SFields.
            char const* toField = nullptr;
            if (auto irreg = irregulars.find(fieldName);
                irreg != irregulars.end())
            {
                toField = irreg->second;
            }
            else
            {
                // Remove the underscores from the field name and then locate
                // the corresponding SField.
                std::string noUnders = fieldName;
                noUnders.erase(
                    std::remove(noUnders.begin(), noUnders.end(), '_'),
                    noUnders.end());
                SField const& foundField = SField::getFieldCI(noUnders);
                if (foundField != sfInvalid)
                    toField = foundField.jsonName.c_str();
            }

            if (!toField)
                return false;

            if (!unwrap(obj, toField, fieldName.c_str()))
                return false;
        }
        return true;
    };

    {
        // Convert the account_data object from gRPC-style to old-style.

        // Ignore these gRPC fields because they are too complicated to
        // handle in a generic way.
        static const std::set<std::string> ignores{
            jss::signer_list.c_str(),
        };

        // Fields returned by gRPC that don't map nicely to SField names.
        static const std::map<std::string, char const*> irregulars{
            {"account_transaction_id", sfAccountTxnID.jsonName.c_str()},
            {"previous_transaction_id", sfPreviousTxnID.jsonName.c_str()},
            {"previous_transaction_ledger_sequence",
             sfPreviousTxnLgrSeq.jsonName.c_str()}};

        if (!convertObj(acctData, ignores, irregulars))
            return internalFail();
    }

    // gRPC does not include the LedgerEntryType field.
    acctData[sfLedgerEntryType.jsonName] = jss::AccountRoot.c_str();

    // grpc does not return the index.  So generate that locally.
    acctData[jss::index] = to_string(acctIndex);

    // gRPC returns AccountTxnID and PreviousTxnID as base64.  We need to
    // return those values as hex.  Perform that conversion now.
    auto replaceBase64WithHex = [](Json::Value& obj, char const* field) {
        if (obj.isMember(field))
        {
            std::string hex = base64toHex(obj[field].asString());
            if (!hex.empty())
                obj[field] = hex;
        }
    };
    replaceBase64WithHex(acctData, sfAccountTxnID.jsonName);
    replaceBase64WithHex(acctData, sfPreviousTxnID.jsonName);

    // Deal with converting the SignerList.  Note that gRPC calls it
    // signer_list and RPC calls it signer_lists.
    if (grpcContext.params.signer_lists())
        acctData[jss::signer_lists] = Json::arrayValue;

    if (rpcAccountInfo.isMember(jss::signer_list))
    {
        Json::Value& signerList = rpcAccountInfo[jss::signer_list];

        {
            // Convert the signer_list object from gRPC-style to old-style.

            // Ignore these gRPC fields because they are too complicated to
            // handle in a generic way.
            static const std::set<std::string> ignores{
                "signer_entries",
            };

            // Fields returned by gRPC that don't map nicely to SField names.
            static const std::map<std::string, char const*> irregulars{
                {"previous_transaction_id", sfPreviousTxnID.jsonName.c_str()},
                {"previous_transaction_ledger_sequence",
                 sfPreviousTxnLgrSeq.jsonName.c_str()}};

            if (!convertObj(signerList, ignores, irregulars))
                return internalFail();
        }

        // gRPC does not include the LedgerEntryType field.
        signerList[sfLedgerEntryType.jsonName] = jss::SignerList.c_str();

        // gRPC returns sfPreviousTxnID as base64.  The old RPC command
        // returns the value as hex.  Convert the gRPC value to hex.
        replaceBase64WithHex(signerList, sfPreviousTxnID.jsonName);

        // gRPC does not return the signer list index, so generate that locally.
        signerList[jss::index] = to_string(keylet::signers(accountID).key);

        // Now fill in the array of SignerEntries.
        Json::Value& newEntries = signerList[sfSignerEntries.jsonName] =
            Json::arrayValue;

        for (std::uint32_t i = 0; i < signerList["signer_entries"].size(); ++i)
        {
            Json::Value& oldEntry = signerList["signer_entries"][i];
            if (!oldEntry)
                return internalFail();

            // Convert this SignerEntry from gRPC-style to old-style.
            if (!convertObj(oldEntry, {}, {}))
                return internalFail();

            Json::Value& newEntry = newEntries[i][sfSignerEntry.jsonName] =
                Json::objectValue;
            Json::copyFrom(newEntry, oldEntry);
        }
        signerList.removeMember("signer_entries");

        // Lift the signer_list into position inside signer_lists.
        Json::Value sigList = rpcAccountInfo.removeMember(jss::signer_list);
        Json::copyFrom(acctData[jss::signer_lists][0u], sigList);
    }

    // Remove queue_data if appropriate.
    if (!grpcContext.params.queue())
    {
        rpcAccountInfo.removeMember(jss::queue_data);
    }
    else
    {
        Json::Value& queueData = rpcAccountInfo[jss::queue_data];

        // Ignore these gRPC fields because they are just fine as they are
        // or too messy to deal with in an automated way.
        static const std::set<std::string> ignores{
            jss::auth_change_queued.c_str(),
            jss::highest_sequence.c_str(),
            jss::highest_ticket.c_str(),
            jss::lowest_sequence.c_str(),
            jss::lowest_ticket.c_str(),
            jss::sequence_count.c_str(),
            jss::ticket_count.c_str(),
            jss::transactions.c_str(),
            jss::txn_count.c_str()};

        // Fields returned by gRPC that don't map nicely to SField names.
        static const std::map<std::string, char const*> irregulars{
            {jss::max_spend_drops_total.c_str(),
             jss::max_spend_drops_total.c_str()}};

        if (!convertObj(queueData, ignores, irregulars))
            return internalFail();

        // Lambda that removes selected fields if they have default values.
        auto removeDefaulted =
            [](Json::Value& obj,
               std::set<std::string> const& removeIfDefault) -> bool {
            if (!obj.isObject())
                return false;

            // Extract the field names in obj.
            std::vector<std::string> fieldNames = obj.getMemberNames();
            for (std::string const& fieldName : fieldNames)
            {
                if (removeIfDefault.count(fieldName) == 0)
                    continue;

                switch (obj[fieldName].type())
                {
                    case Json::nullValue:
                        obj.removeMember(fieldName);
                        break;

                    case Json::booleanValue:
                        if (obj[fieldName] == false)
                            obj.removeMember(fieldName);
                        break;

                    case Json::intValue:
                    case Json::uintValue:
                    case Json::realValue:
                        if (obj[fieldName] == 0)
                            obj.removeMember(fieldName);
                        break;

                    case Json::stringValue:
                        if (obj[fieldName] == "")
                            obj.removeMember(fieldName);
                        break;

                    case Json::arrayValue:
                    case Json::objectValue:
                        if (obj[fieldName].size() == 0)
                            obj.removeMember(fieldName);
                        break;
                }
            }
            return true;
        };

        {
            static const std::set<std::string> removeIfDefault{
                jss::lowest_sequence.c_str(),
                jss::lowest_ticket.c_str(),
                jss::highest_sequence.c_str(),
                jss::highest_ticket.c_str(),
                jss::max_spend_drops_total.c_str(),
                jss::sequence_count.c_str(),
                jss::ticket_count.c_str(),
                jss::transactions.c_str()};
            if (!removeDefaulted(queueData, removeIfDefault))
                return internalFail();
        }

        // Deal with "auth_change_queued".
        if (!queueData.isMember(jss::transactions) ||
            queueData[jss::transactions].size() == 0)
        {
            queueData.removeMember(jss::auth_change_queued);
        }

        // Deal with the transactions array if present.
        if (queueData.isMember(jss::transactions))
        {
            Json::Value& transactions = queueData[jss::transactions];
            if (!transactions.isArray())
                return internalFail();

            for (Json::Value& tx : transactions)
            {
                if (!tx.isObject())
                    return internalFail();

                // Ignore these gRPC fields because they are fine as they are.
                static const std::set<std::string> txIgnores{
                    jss::auth_change.c_str(), jss::fee_level.c_str()};

                // Fields returned by gRPC that don't map nicely to SField
                // names.
                static const std::map<std::string, char const*> txIrregulars{
                    {jss::fee.c_str(), jss::fee.c_str()},
                    {jss::max_spend_drops.c_str(),
                     jss::max_spend_drops.c_str()},
                    {"sequence", jss::seq.c_str()},
                    {jss::ticket.c_str(), jss::ticket.c_str()},
                };

                if (!convertObj(tx, txIgnores, txIrregulars))
                    return internalFail();
            }
        }
    }
    return rpcAccountInfo;
}

}  // namespace ripple
