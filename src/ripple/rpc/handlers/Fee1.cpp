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
// Overview
//
// The goal here is to use common code to serialize responses to RPC and
// gPRC commands.  This is complicated because
//
//   o RPC commands use Json::Value for their responses and
//   o gRPC commands use google::protobuf::Message for their response.
//
// However it was possible to find common ground.  By using the protobuf
// Reflection interface it is possible to set values in gRPC messages
// identifying the field by (text) name.  Using the name allows both RPC and
// gRPC to specify the field in an identical way.
//
// The bulk of the code in this file is aimed at making the Reflection work
// correctly.  Because Reflection is a runtime facility all of the usual
// compile-time benefits of gRPC are lost.

namespace detail {

// Declare variable templates for google::protobuf::Reflection setting
// of a field.
//
// Note that the primary template is char* to intentionally cause a
// compile error if used as a member pointer.
template <typename T>
[[maybe_unused]] constexpr char* pbufReflectSet = nullptr;

template <>
[[maybe_unused]] constexpr decltype(
    &google::protobuf::Reflection::SetInt32) pbufReflectSet<std::int32_t> =
    &google::protobuf::Reflection::SetInt32;

template <>
[[maybe_unused]] constexpr decltype(
    &google::protobuf::Reflection::SetUInt32) pbufReflectSet<std::uint32_t> =
    &google::protobuf::Reflection::SetUInt32;

template <>
[[maybe_unused]] constexpr decltype(
    &google::protobuf::Reflection::SetInt64) pbufReflectSet<std::int64_t> =
    &google::protobuf::Reflection::SetInt64;

template <>
[[maybe_unused]] constexpr decltype(
    &google::protobuf::Reflection::SetUInt64) pbufReflectSet<std::uint64_t> =
    &google::protobuf::Reflection::SetUInt64;

template <>
[[maybe_unused]] constexpr decltype(
    &google::protobuf::Reflection::SetDouble) pbufReflectSet<double> =
    &google::protobuf::Reflection::SetDouble;

template <>
[[maybe_unused]] constexpr decltype(&google::protobuf::Reflection::SetString)
    pbufReflectSet<std::string const&> =
        &google::protobuf::Reflection::SetString;

// Declare variable templates that map from std::types to
// FieldDescriptor::Type.
//
// Note that the primary template is char* to intentionally cause a
// compile error if compared to an enum or integer.
template <typename T>
[[maybe_unused]] constexpr char* pbufFieldDescType = nullptr;

template <>
[[maybe_unused]] constexpr google::protobuf::FieldDescriptor::Type
    pbufFieldDescType<std::int32_t> =
        google::protobuf::FieldDescriptor::Type::TYPE_INT32;

template <>
[[maybe_unused]] constexpr google::protobuf::FieldDescriptor::Type
    pbufFieldDescType<std::uint32_t> =
        google::protobuf::FieldDescriptor::Type::TYPE_UINT32;

template <>
[[maybe_unused]] constexpr google::protobuf::FieldDescriptor::Type
    pbufFieldDescType<std::int64_t> =
        google::protobuf::FieldDescriptor::Type::TYPE_INT64;

template <>
[[maybe_unused]] constexpr google::protobuf::FieldDescriptor::Type
    pbufFieldDescType<std::uint64_t> =
        google::protobuf::FieldDescriptor::Type::TYPE_UINT64;

template <>
[[maybe_unused]] constexpr google::protobuf::FieldDescriptor::Type
    pbufFieldDescType<double> =
        google::protobuf::FieldDescriptor::Type::TYPE_DOUBLE;

template <>
[[maybe_unused]] constexpr google::protobuf::FieldDescriptor::Type
    pbufFieldDescType<std::string> =
        google::protobuf::FieldDescriptor::Type::TYPE_STRING;
}  // namespace detail

template <typename T>
bool
setOneGRPCFieldByDescriptor(
    google::protobuf::Message* msg,
    google::protobuf::Descriptor const* const d,
    T const& value)
{
    if (!d)
        return false;

    if (d->field_count() != 1)
        return false;

    google::protobuf::FieldDescriptor const* const f = d->field(0);
    if (!f)
        return false;

    google::protobuf::Reflection const* const r = msg->GetReflection();
    if (!r)
        return false;

    if (f->type() == google::protobuf::FieldDescriptor::TYPE_MESSAGE)
    {
        // We can't actually set this message.  We need to recurse one level
        // lower to see if we can set the next layer down.
        return setOneGRPCFieldByDescriptor(msg, f->message_type(), value);
    }

    if (detail::pbufFieldDescType<T> != f->type())
    {
        assert(detail::pbufFieldDescType<T> == f->type());
        return false;
    }

    std::invoke(detail::pbufReflectSet<T>, r, msg, f, value);
    return true;
}

template <typename T>
bool
setFieldByText(
    google::protobuf::Message* msg,
    std::string const& field,
    T const& value)
{
    google::protobuf::Descriptor const* const d = msg->GetDescriptor();
    if (!d)
        return false;

    google::protobuf::FieldDescriptor const* const f =
        d->FindFieldByName(field);
    if (!f)
        return false;

    google::protobuf::Reflection const* const r = msg->GetReflection();
    if (!r)
        return false;

    if (f->type() == google::protobuf::FieldDescriptor::TYPE_MESSAGE)
    {
        // We can't actually set this message.  We need to recurse one level
        // lower to see if we can set the next layer down.
        return setOneGRPCFieldByDescriptor(msg, f->message_type(), value);
    }

    if (detail::pbufFieldDescType<T> != f->type())
    {
        assert(detail::pbufFieldDescType<T> == f->type());
        return false;
    }

    std::invoke(detail::pbufReflectSet<T>, r, msg, f, value);
    return true;
}

template <typename T>
bool
setFieldByJSS(
    google::protobuf::Message* msg,
    Json::StaticString const& field,
    T const& value)
{
    return setFieldByText(msg, field.c_str(), value);
}

google::protobuf::Message*
getObjectByJSS(
    google::protobuf::Message* msg,
    Json::StaticString const& objName)
{
    google::protobuf::Descriptor const* const d = msg->GetDescriptor();
    if (!d)
        return nullptr;

    google::protobuf::FieldDescriptor const* const f =
        d->FindFieldByName(std::string(objName));
    if (!f)
        return nullptr;

    if (f->type() != google::protobuf::FieldDescriptor::TYPE_MESSAGE)
        return nullptr;

    google::protobuf::Reflection const* const r = msg->GetReflection();
    if (!r)
        return nullptr;

    return r->MutableMessage(msg, f);
}

//------------------------------------------------------------------------------
// JSON support for generic RPC handling.

template <typename T>
bool
setFieldByJSS(Json::Value* msg, Json::StaticString const& field, T const& value)
{
    if (!msg->isObject())
    {
        assert(msg->isObject());
        return false;
    }

    // Represent 64-bit values in JSON as strings.
    if constexpr (std::is_integral_v<T> && std::numeric_limits<T>::digits > 32)
        (*msg)[field] = std::to_string(value);
    else
        (*msg)[field] = value;

    return true;
}

Json::Value*
getObjectByJSS(Json::Value* msg, Json::StaticString const& objName)
{
    Json::Value& obj = (*msg)[objName] = Json::objectValue;
    return &obj;
}

//------------------------------------------------------------------------------
// Serialization routine shared by RPC and gRPC.

template <typename R>
bool
doFeeCommonHandling(R reply, TxQ::FeeData const& feeData)
{
    bool success = true;

    // current ledger data
    success &= setFieldByJSS(
        reply, jss::current_ledger_size, feeData.current_ledger_size);

    success &= setFieldByJSS(
        reply, jss::current_queue_size, feeData.current_queue_size);

    success &= setFieldByJSS(
        reply, jss::expected_ledger_size, feeData.expected_ledger_size);

    success &= setFieldByJSS(
        reply, jss::ledger_current_index, feeData.ledger_current_index);

    if (feeData.max_queue_size)
        success &=
            setFieldByJSS(reply, jss::max_queue_size, *feeData.max_queue_size);

    // fee levels data
    {
        auto levels = getObjectByJSS(reply, jss::levels);

        success &= setFieldByJSS(
            levels, jss::median_level, feeData.levels.median_level.value());

        success &= setFieldByJSS(
            levels, jss::minimum_level, feeData.levels.minimum_level.value());

        success &= setFieldByJSS(
            levels,
            jss::open_ledger_level,
            feeData.levels.open_ledger_level.value());

        success &= setFieldByJSS(
            levels,
            jss::reference_level,
            feeData.levels.reference_level.value());
    }
    return success;
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
    boost::optional<TxQ::FeeData> feeData =
        context.app.getTxQ().getFeeRPCData(context.app);

    if (!feeData)
    {
        RPC::inject_error(rpcINTERNAL, context.params);
        return context.params;
    }

    Json::Value reply(Json::objectValue);

    // Fill in fields that are the same between RPC and gRPC.
    if (!doFeeCommonHandling(&reply, *feeData))
    {
        RPC::inject_error(rpcINTERNAL, context.params);
        return context.params;
    }

    // Handle fields that are different between RPC and gRPC.
    {
        // fee data
        auto& drops = reply[jss::drops] = Json::objectValue;

        drops[jss::base_fee] = to_string(feeData->drops.base_fee);
        drops[jss::minimum_fee] = to_string(feeData->drops.minimum_fee);
        drops[jss::median_fee] = to_string(feeData->drops.median_fee);
        drops[jss::open_ledger_fee] = to_string(feeData->drops.open_ledger_fee);
    }
    return reply;
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
    boost::optional<TxQ::FeeData> feeData =
        context.app.getTxQ().getFeeRPCData(context.app);

    org::xrpl::rpc::v1::GetFeeResponse reply;
    if (!feeData)
    {
        return {reply, grpc::Status::OK};
    }

    // Fill in fields that are the same between RPC and gRPC.
    if (!doFeeCommonHandling(&reply, *feeData))
    {
        org::xrpl::rpc::v1::GetFeeResponse emptyReply;
        return {emptyReply, grpc::Status::OK};
    }

    // Handle fields that are different between RPC and gRPC.
    org::xrpl::rpc::v1::Fee* fee = reply.mutable_fee();
    fee->mutable_base_fee()->set_drops(feeData->drops.base_fee.drops());
    fee->mutable_minimum_fee()->set_drops(feeData->drops.minimum_fee.drops());
    fee->mutable_median_fee()->set_drops(feeData->drops.median_fee.drops());
    fee->mutable_open_ledger_fee()->set_drops(
        feeData->drops.open_ledger_fee.drops());

    return {reply, grpc::Status::OK};
}

}  // namespace ripple
