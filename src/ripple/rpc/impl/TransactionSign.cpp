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

#include <BeastConfig.h>
#include <ripple/app/paths/FindPaths.h>
#include <ripple/basics/StringUtilities.h>
#include <ripple/json/json_reader.h>
#include <ripple/protocol/TxFlags.h>
#include <ripple/rpc/impl/TransactionSign.h>
#include <ripple/protocol/TxFlags.h>
#include <ripple/basics/StringUtilities.h>
#include <beast/unit_test.h>

namespace ripple {

//------------------------------------------------------------------------------


namespace RPC {
namespace detail {

// A local class used to pass extra parameters used when returning a
// a SigningAccount object.
class SigningAccountParams
{
private:
    RippleAddress* const multiSignPublicKey_;
    Blob* const multiSignature_;
public:
    explicit SigningAccountParams ()
    : multiSignPublicKey_ (nullptr)
    , multiSignature_ (nullptr)
    { }

    SigningAccountParams (
        RippleAddress& multiSignPublicKey,
        Blob& multiSignature)
    : multiSignPublicKey_ (&multiSignPublicKey)
    , multiSignature_ (&multiSignature)
    { }

    bool isMultiSigning () const
    {
        return ((multiSignPublicKey_ != nullptr) &&
                (multiSignature_ != nullptr));
    }

    // When multi-signing we should not edit the tx_json fields.
    bool editFields () const
    {
        return !isMultiSigning();
    }

    void setPublicKey (RippleAddress const& multiSignPublicKey)
    {
        *multiSignPublicKey_ = multiSignPublicKey;
    }

    void moveMultiSignature (Blob&& multiSignature)
    {
        *multiSignature_ = std::move (multiSignature);
    }
};

//------------------------------------------------------------------------------

// TxnSignApiFacade methods

void TxnSignApiFacade::snapshotAccountState (RippleAddress const& accountID)
{
    if (!netOPs_) // Unit testing.
        return;

    ledger_ = netOPs_->getCurrentLedger ();
    accountID_ = accountID;
    accountState_ = netOPs_->getAccountState (ledger_, accountID_);
}

bool TxnSignApiFacade::isValidAccount () const
{
    if (!ledger_) // Unit testing.
        return true;

    return static_cast <bool> (accountState_);
}

std::uint32_t TxnSignApiFacade::getSeq () const
{
    if (!ledger_) // Unit testing.
        return 0;

    return accountState_->getSeq ();
}

Transaction::pointer TxnSignApiFacade::processTransaction (
    Transaction::ref tpTrans,
    bool bAdmin,
    bool bLocal,
    NetworkOPs::FailHard failType)
{
    if (!netOPs_) // Unit testing.
        return tpTrans;

    return netOPs_->processTransaction (tpTrans, bAdmin, bLocal, failType);
}

bool TxnSignApiFacade::findPathsForOneIssuer (
    RippleAddress const& dstAccountID,
    Issue const& srcIssue,
    STAmount const& dstAmount,
    int searchLevel,
    unsigned int const maxPaths,
    STPathSet& pathsOut,
    STPath& fullLiquidityPath) const
{
    if (!ledger_) // Unit testing.
        // Note that unit tests don't (yet) need pathsOut or fullLiquidityPath.
        return true;

    auto cache = std::make_shared<RippleLineCache> (ledger_);
    return ripple::findPathsForOneIssuer (
        cache,
        accountID_.getAccountID (),
        dstAccountID.getAccountID (),
        srcIssue,
        dstAmount,
        searchLevel,
        maxPaths,
        pathsOut,
        fullLiquidityPath);
}

std::uint64_t TxnSignApiFacade::scaleFeeBase (std::uint64_t fee) const
{
    if (!ledger_) // Unit testing.
        return fee;

    return ledger_->scaleFeeBase (fee);
}

std::uint64_t
TxnSignApiFacade::scaleFeeLoad (std::uint64_t fee, bool bAdmin) const
{
    if (!ledger_) // Unit testing.
        return fee;

    return ledger_->scaleFeeLoad (fee, bAdmin);
}

bool TxnSignApiFacade::hasAccountRoot () const
{
    if (!netOPs_) // Unit testing.
        return true;

    SLE::pointer const sleAccountRoot =
        netOPs_->getSLEi (ledger_, getAccountRootIndex (accountID_));

    return static_cast <bool> (sleAccountRoot);
}

error_code_i acctMatchesPubKey (
    AccountState::pointer accountState,
    RippleAddress const& accountID,
    RippleAddress const& publicKey)
{
    Account const publicKeyAcctID = publicKey.getAccountID ();
    bool const isMasterKey = publicKeyAcctID == accountID.getAccountID ();

    // If we can't get the accountRoot, but the accountIDs match, that's
    // good enough.
    if (!accountState)
    {
        if (isMasterKey)
            return rpcSUCCESS;
        return rpcBAD_SECRET;
    }

    // If we *can* get to the accountRoot, check for MASTER_DISABLED
    STLedgerEntry const& sle = accountState->peekSLE ();
    if (isMasterKey)
    {
        if (sle.isFlag(lsfDisableMaster))
            return rpcMASTER_DISABLED;
        return rpcSUCCESS;
    }

    // The last gasp is that we have public Regular key.
    if ((sle.isFieldPresent (sfRegularKey)) &&
        (publicKeyAcctID == sle.getFieldAccount160 (sfRegularKey)))
    {
        return rpcSUCCESS;
    }
    return rpcBAD_SECRET;
}

error_code_i TxnSignApiFacade::singleAcctMatchesPubKey (
    RippleAddress const& publicKey) const
{
    if (!netOPs_) // Unit testing.
        return rpcSUCCESS;

    return acctMatchesPubKey (accountState_, accountID_, publicKey);
}

error_code_i TxnSignApiFacade::multiAcctMatchesPubKey (
    RippleAddress const& accountID,
    RippleAddress const& publicKey) const
{
    AccountState::pointer accountState;
    if (netOPs_ && ledger_)
        // If it's available, get the AccountState for the multi-signer's
        // accountID.  It's okay if the AccountState is not available,
        // since they might be signing with a phantom (unfunded) account.
        accountState = netOPs_->getAccountState (ledger_, accountID);

    return acctMatchesPubKey (accountState, accountID, publicKey);
}

int TxnSignApiFacade::getValidatedLedgerAge () const
{
    if (!netOPs_) // Unit testing.
        return 0;

    return getApp( ).getLedgerMaster ().getValidatedLedgerAge ();
}

bool TxnSignApiFacade::isLoadedCluster () const
{
    if (!netOPs_) // Unit testing.
        return false;

    return getApp().getFeeTrack().isLoadedCluster();
}

//------------------------------------------------------------------------------

/** Fill in the fee on behalf of the client.
    This is called when the client does not explicitly specify the fee.
    The client may also put a ceiling on the amount of the fee. This ceiling
    is expressed as a multiplier based on the current ledger's fee schedule.

    JSON fields

    "Fee"   The fee paid by the transaction. Omitted when the client
            wants the fee filled in.

    "fee_mult_max"  A multiplier applied to the current ledger's transaction
                    fee that caps the maximum the fee server should auto fill.
                    If this optional field is not specified, then a default
                    multiplier is used.

    @param tx       The JSON corresponding to the transaction to fill in.
    @param ledger   A ledger for retrieving the current fee schedule.
    @param roll     Identifies if this is called by an administrative endpoint.

    @return         A JSON object containing the error results, if any
*/

static Json::Value checkFee (
    Json::Value& request,
    TxnSignApiFacade& apiFacade,
    Role const role,
    AutoFill const doAutoFill)
{
    Json::Value& tx (request["tx_json"]);
    if (tx.isMember ("Fee"))
        return Json::Value();

    if (doAutoFill == AutoFill::dont)
        return RPC::missing_field_error ("tx_json.Fee");

    int mult = Tuning::defaultAutoFillFeeMultiplier;
    if (request.isMember ("fee_mult_max"))
    {
        if (request["fee_mult_max"].isNumeric ())
        {
            mult = request["fee_mult_max"].asInt();
        }
        else
        {
            return RPC::make_error (rpcHIGH_FEE,
                RPC::expected_field_message ("fee_mult_max", "a number"));
        }
    }

    // Default fee in fee units.
    std::uint64_t const feeDefault = getConfig().TRANSACTION_FEE_BASE;

    // Administrative endpoints are exempt from local fees.
    std::uint64_t const fee =
        apiFacade.scaleFeeLoad (feeDefault, role == Role::ADMIN);

    std::uint64_t const limit = mult * apiFacade.scaleFeeBase (feeDefault);

    if (fee > limit)
    {
        std::stringstream ss;
        ss <<
            "Fee of " << fee <<
            " exceeds the requested tx limit of " << limit;
        return RPC::make_error (rpcHIGH_FEE, ss.str());
    }

    tx ["Fee"] = static_cast<int>(fee);
    return Json::Value();
}

enum class PathFind : unsigned char
{
    dont,
    might
};

static Json::Value checkPayment(
    Json::Value const& params,
    Json::Value& tx_json,
    RippleAddress const& raSrcAddressID,
    TxnSignApiFacade const& apiFacade,
    Role const role,
    PathFind const doPath)
{
    // Only path find for Payments.
    if (tx_json["TransactionType"].asString () != "Payment")
        return Json::Value();

    RippleAddress dstAccountID;

    if (!tx_json.isMember ("Amount"))
        return RPC::missing_field_error ("tx_json.Amount");

    STAmount amount;

    if (! amountFromJsonNoThrow (amount, tx_json ["Amount"]))
        return RPC::invalid_field_error ("tx_json.Amount");

    if (!tx_json.isMember ("Destination"))
        return RPC::missing_field_error ("tx_json.Destination");

    if (!dstAccountID.setAccountID (tx_json["Destination"].asString ()))
        return RPC::invalid_field_error ("tx_json.Destination");

    if ((doPath == PathFind::dont) && params.isMember ("build_path"))
        return RPC::make_error (rpcINVALID_PARAMS,
            "Field 'build_path' not allowed in this context.");

    if (tx_json.isMember ("Paths") && params.isMember ("build_path"))
        return RPC::make_error (rpcINVALID_PARAMS,
            "Cannot specify both 'tx_json.Paths' and 'build_path'");

    if (!tx_json.isMember ("Paths") && params.isMember ("build_path"))
    {
        STAmount    saSendMax;

        if (tx_json.isMember ("SendMax"))
        {
            if (! amountFromJsonNoThrow (saSendMax, tx_json ["SendMax"]))
                return RPC::invalid_field_error ("tx_json.SendMax");
        }
        else
        {
            // If no SendMax, default to Amount with sender as issuer.
            saSendMax = amount;
            saSendMax.setIssuer (raSrcAddressID.getAccountID ());
        }

        if (saSendMax.isNative () && amount.isNative ())
            return RPC::make_error (rpcINVALID_PARAMS,
                "Cannot build XRP to XRP paths.");

        {
            LegacyPathFind lpf (role == Role::ADMIN);
            if (!lpf.isOk ())
                return rpcError (rpcTOO_BUSY);

            STPathSet spsPaths;
            STPath fullLiquidityPath;
            bool valid = apiFacade.findPathsForOneIssuer (
                dstAccountID,
                saSendMax.issue (),
                amount,
                getConfig ().PATH_SEARCH_OLD,
                4,  // iMaxPaths
                spsPaths,
                fullLiquidityPath);

            if (!valid)
            {
                WriteLog (lsDEBUG, RPCHandler)
                        << "transactionSign: build_path: No paths found.";
                return rpcError (rpcNO_PATH);
            }
            WriteLog (lsDEBUG, RPCHandler)
                    << "transactionSign: build_path: "
                    << spsPaths.getJson (0);

            if (!spsPaths.empty ())
                tx_json["Paths"] = spsPaths.getJson (0);
        }
    }
    return Json::Value();
}

//------------------------------------------------------------------------------

// Validate (but don't modify) the contents of the tx_json.
//
// Returns a pair<Json::Value, RippleAddress>.  The Json::Value is non-empty
// and contains as error if there was an error.  The returned RippleAddress
// is the "Account" addressID if there was no error.
//
// This code does not check the "Sequence" field, since the expectations
// for that field are particularly context sensitive.
static std::pair<Json::Value, RippleAddress>
checkTxJsonFields (
    Json::Value const& tx_json,
    TxnSignApiFacade const& apiFacade,
    Role const role,
    bool const verify)
{
    std::pair<Json::Value, RippleAddress> ret;

    if (! tx_json.isObject ())
    {
        ret.first = RPC::object_field_error ("tx_json");
        return ret;
    }

    if (! tx_json.isMember ("TransactionType"))
    {
        ret.first = RPC::missing_field_error ("tx_json.TransactionType");
        return ret;
    }

    if (! tx_json.isMember ("Account"))
    {
        ret.first = RPC::make_error (rpcSRC_ACT_MISSING,
            RPC::missing_field_message ("tx_json.Account"));
        return ret;
    }

    RippleAddress srcAddressID;

    if (! srcAddressID.setAccountID (tx_json["Account"].asString ()))
    {
        ret.first = RPC::make_error (rpcSRC_ACT_MALFORMED,
            RPC::invalid_field_message ("tx_json.Account"));
        return ret;
    }

    // Check for current ledger.
    if (verify && !getConfig ().RUN_STANDALONE &&
        (apiFacade.getValidatedLedgerAge() > 120))
    {
        ret.first = rpcError (rpcNO_CURRENT);
        return ret;
    }

    // Check for load.
    if (apiFacade.isLoadedCluster() && (role != Role::ADMIN))
    {
        ret.first = rpcError(rpcTOO_BUSY);
        return ret;
    }

    // It's all good.  Return the AddressID.
    ret.second = std::move (srcAddressID);
    return ret;
}

//------------------------------------------------------------------------------

// A move-only struct that makes it easy to return either a Json::Value or a
// STTx::pointer from transactionPreProcessImpl ().
struct transactionPreProcessResult
{
    Json::Value const first;
    STTx::pointer const second;

    transactionPreProcessResult () = delete;
    transactionPreProcessResult (transactionPreProcessResult const&) = delete;
    transactionPreProcessResult (transactionPreProcessResult&&) = default;

    transactionPreProcessResult& operator=
        (transactionPreProcessResult const&) = delete;
     transactionPreProcessResult& operator=
        (transactionPreProcessResult&&) = delete;


    transactionPreProcessResult (Json::Value&& json)
    : first (std::move (json))
    , second ()
    { }

    transactionPreProcessResult (STTx::pointer&& st)
    : first ()
    , second (std::move (st))
    { }
};

static transactionPreProcessResult transactionPreProcessImpl (
    Json::Value& params,
    TxnSignApiFacade& apiFacade,
    Role role,
    SigningAccountParams& signingArgs)
{
    if (! params.isMember ("secret"))
        return RPC::missing_field_error ("secret");

    {
        RippleAddress naSeed;

        if (! naSeed.setSeedGeneric (params["secret"].asString ()))
            return RPC::make_error (rpcBAD_SEED,
                RPC::invalid_field_message ("secret"));
    }

    bool const verify = !(params.isMember ("offline")
                          && params["offline"].asBool ());

    if (! params.isMember ("tx_json"))
        return RPC::missing_field_error ("tx_json");

    Json::Value& tx_json (params ["tx_json"]);

    // Check tx_json fields, but don't add any.
    auto txJsonResult = checkTxJsonFields (tx_json, apiFacade, role, verify);
    if (RPC::contains_error (txJsonResult.first))
        return std::move (txJsonResult.first);

    RippleAddress const raSrcAddressID = std::move (txJsonResult.second);

    // This test covers the case where we're offline so the sequence number
    // cannot be determined locally.  If we're offline then the caller must
    // provide the sequence number.
    if (!verify && !tx_json.isMember ("Sequence"))
        return RPC::missing_field_error ("tx_json.Sequence");

    apiFacade.snapshotAccountState (raSrcAddressID);

    if (verify) {
        if (!apiFacade.isValidAccount ())
        {
            // If not offline and did not find account, error.
            WriteLog (lsDEBUG, RPCHandler)
                << "transactionSign: Failed to find source account "
                << "in current ledger: "
                << raSrcAddressID.humanAccountID ();

            return rpcError (rpcSRC_ACT_NOT_FOUND);
        }
    }

    {
        Json::Value err = checkFee (
            params,
            apiFacade,
            role,
            signingArgs.editFields() ? AutoFill::might : AutoFill::dont);

        if (RPC::contains_error (err))
            return std::move (err);

        err = checkPayment (
            params,
            tx_json,
            raSrcAddressID,
            apiFacade,
            role,
            signingArgs.editFields() ? PathFind::might : PathFind::dont);

        if (RPC::contains_error(err))
            return std::move (err);
    }

    if (signingArgs.editFields())
    {
        if (!tx_json.isMember ("Sequence"))
            tx_json["Sequence"] = apiFacade.getSeq ();

        if (!tx_json.isMember ("Flags"))
            tx_json["Flags"] = tfFullyCanonicalSig;
    }

    if (verify)
    {
        if (!apiFacade.hasAccountRoot ())
            // XXX Ignore transactions for accounts not created.
            return rpcError (rpcSRC_ACT_NOT_FOUND);
    }

    RippleAddress secret = RippleAddress::createSeedGeneric (
        params["secret"].asString ());
    RippleAddress masterGenerator = RippleAddress::createGeneratorPublic (
        secret);
    RippleAddress masterAccountPublic = RippleAddress::createAccountPublic (
        masterGenerator, 0);

    if (verify)
    {
        WriteLog (lsTRACE, RPCHandler) <<
            "verify: " << masterAccountPublic.humanAccountID () <<
            " : " << raSrcAddressID.humanAccountID ();

        // If multisigning then we need to return the public key.
        if (signingArgs.isMultiSigning ())
        {
            signingArgs.setPublicKey (masterAccountPublic);
        }
        else
        {
            // Make sure the account and secret belong together.
            error_code_i const err =
                apiFacade.singleAcctMatchesPubKey (masterAccountPublic);

            if (err != rpcSUCCESS)
                return rpcError (err);
        }
    }

    STParsedJSONObject parsed ("tx_json", tx_json);
    if (!parsed.object.get())
    {
        Json::Value err;
        err ["error"] = parsed.error ["error"];
        err ["error_code"] = parsed.error ["error_code"];
        err ["error_message"] = parsed.error ["error_message"];
        return std::move (err);
    }

    STTx::pointer stpTrans;
    try
    {
        // If we're generating a multi-signature the SigningPubKey must be
        // empty:
        //  o If we're multi-signing, use an empty blob for the signingPubKey
        //  o Otherwise use the master account's public key.
        Blob emptyBlob;
        Blob const& signingPubKey = signingArgs.isMultiSigning () ?
            emptyBlob : masterAccountPublic.getAccountPublic ();

        std::unique_ptr<STObject> sopTrans = std::move (parsed.object);
        sopTrans->setFieldVL (sfSigningPubKey, signingPubKey);

        stpTrans = std::make_shared<STTx> (*sopTrans);
    }
    catch (std::exception&)
    {
        return RPC::make_error (rpcINTERNAL,
            "Exception occurred constructing serialized transaction");
    }

    std::string reason;
    if (!passesLocalChecks (*stpTrans, reason))
        return RPC::make_error (rpcINVALID_PARAMS, reason);

    RippleAddress naAccountPrivate = RippleAddress::createAccountPrivate (
        masterGenerator, secret, 0);

    // If multisign then return multiSignature, else set TxnSignature field.
    if (signingArgs.isMultiSigning ())
    {
        uint256 const hash = stpTrans->getSigningHash ();
        Blob multiSignature;
        naAccountPrivate.accountPrivateSign (hash, multiSignature);
        signingArgs.moveMultiSignature (std::move (multiSignature));
    }
    else
    {
        stpTrans->sign (naAccountPrivate);
    }

    return std::move (stpTrans);
}

static
std::pair <Json::Value, Transaction::pointer>
transactionConstructImpl (STTx::pointer stpTrans)
{
    std::pair <Json::Value, Transaction::pointer> ret;

    // Turn the passed in STTx into a Transaction
    Transaction::pointer tpTrans;
    try
    {
        tpTrans = std::make_shared<Transaction> (stpTrans, Validate::NO);
    }
    catch (std::exception&)
    {
        ret.first = RPC::make_error (rpcINTERNAL,
            "Exception occurred constructing transaction");
        return ret;
    }

    try
    {
        // Make sure the Transaction we just built is legit by serializing it
        // and then de-serializing it.  If the result isn't equivalent
        // to the initial transaction then there's something wrong with the
        // passed-in STTx.
        {
            Serializer s;
            tpTrans->getSTransaction ()->add (s);

            Transaction::pointer tpTransNew =
                Transaction::sharedTransaction (s.getData (), Validate::YES);

            if (tpTransNew && (
                !tpTransNew->getSTransaction ()->isEquivalent (
                    *tpTrans->getSTransaction ())))
            {
                tpTransNew.reset ();
            }
            tpTrans = std::move (tpTransNew);
        }
    }
    catch (std::exception&)
    {
        // Assume that any exceptions are related to transaction sterilization.
        tpTrans.reset ();
    }

    if (!tpTrans)
    {
        ret.first = RPC::make_error (rpcINTERNAL,
            "Unable to sterilize transaction.");
        return ret;
    }
    ret.second = std::move (tpTrans);
    return ret;
}

Json::Value transactionFormatResultImpl (Transaction::pointer tpTrans)
{
    Json::Value jvResult;
    try
    {
        jvResult["tx_json"] = tpTrans->getJson (0);
        jvResult["tx_blob"] = strHex (
            tpTrans->getSTransaction ()->getSerializer ().peekData ());

        if (temUNCERTAIN != tpTrans->getResult ())
        {
            std::string sToken;
            std::string sHuman;

            transResultInfo (tpTrans->getResult (), sToken, sHuman);

            jvResult["engine_result"]           = sToken;
            jvResult["engine_result_code"]      = tpTrans->getResult ();
            jvResult["engine_result_message"]   = sHuman;
        }
    }
    catch (std::exception&)
    {
        jvResult = RPC::make_error (rpcINTERNAL,
            "Exception occurred during JSON handling.");
    }
    return jvResult;
}

void insertSigningAccount (
    Json::Value& jvResult,
    RippleAddress const& accountID,
    RippleAddress const& accountPublic,
    Blob const& signature)
{
    // Build a SigningAccount object to inject into jvResult.
    Json::Value signingAccount (Json::objectValue);

    signingAccount[sfAccount.getJsonName ()] = accountID.humanAccountID ();

    signingAccount[sfPublicKey.getJsonName ()] =
        strHex (accountPublic.getAccountPublic ());

    signingAccount[sfMultiSignature.getJsonName ()] = strHex (signature);

    // Inject the SigningAccount object into jvResult.
    jvResult[sfSigningAccount.getName ()] = signingAccount;
}

} // detail

//------------------------------------------------------------------------------

/** Returns a Json::objectValue. */
Json::Value transactionSign (
    Json::Value jvRequest,
    NetworkOPs::FailHard failType,
    detail::TxnSignApiFacade& apiFacade,
    Role role)
{
    WriteLog (lsDEBUG, RPCHandler) << "transactionSign: " << jvRequest;

    using namespace detail;

    // Add and amend fields based on the transaction type.
    SigningAccountParams multiSignParams;
    transactionPreProcessResult preprocResult =
        transactionPreProcessImpl (jvRequest, apiFacade, role, multiSignParams);

    if (!preprocResult.second)
        return preprocResult.first;

    // Make sure the STTx makes a legitimate Transaction.
    std::pair <Json::Value, Transaction::pointer> txn =
        transactionConstructImpl (preprocResult.second);

    if (!txn.second)
        return txn.first;

    return transactionFormatResultImpl (txn.second);
}

/** Returns a Json::objectValue. */
Json::Value transactionSubmit (
    Json::Value jvRequest,
    NetworkOPs::FailHard failType,
    detail::TxnSignApiFacade& apiFacade,
    Role role)
{
    WriteLog (lsDEBUG, RPCHandler) << "transactionSubmit: " << jvRequest;

    using namespace detail;

    // Add and amend fields based on the transaction type.
    SigningAccountParams multiSignParams;
    transactionPreProcessResult preprocResult =
        transactionPreProcessImpl (jvRequest, apiFacade, role, multiSignParams);

    if (!preprocResult.second)
        return preprocResult.first;

    // Make sure the STTx makes a legitimate Transaction.
    std::pair <Json::Value, Transaction::pointer> txn =
        transactionConstructImpl (preprocResult.second);

    if (!txn.second)
        return txn.first;

    // Finally, submit the transaction.
    try
    {
        // FIXME: For performance, should use asynch interface
        apiFacade.processTransaction (
            txn.second, role == Role::ADMIN, true, failType);
    }
    catch (std::exception&)
    {
        return RPC::make_error (rpcINTERNAL,
            "Exception occurred during transaction submission.");
    }

    return transactionFormatResultImpl (txn.second);
}

namespace detail
{
// There are a some field checks shared by transactionGetSigningAccount
// and transactionSubmitMultiSigned.  Gather them together here.
Json::Value checkMultiSignFields (Json::Value const& jvRequest)
{
   if (! jvRequest.isMember ("tx_json"))
        return RPC::missing_field_error ("tx_json");

    Json::Value const& tx_json (jvRequest ["tx_json"]);

    // There are a couple of additional fields we need to check before
    // we serialize.  If we serialize first then we generate less useful
    //error messages.
    if (!tx_json.isMember ("Sequence"))
        return RPC::missing_field_error ("tx_json.Sequence");

    if (!tx_json.isMember ("SigningPubKey"))
        return RPC::missing_field_error ("tx_json.SigningPubKey");

    if (!tx_json["SigningPubKey"].asString().empty())
        return RPC::make_error (rpcINVALID_PARAMS,
            "When multi-signing 'tx_json.SigningPubKey' must be empty.");

    return Json::Value ();
}

} // detail

/** Returns a Json::objectValue. */
Json::Value transactionGetSigningAccount (
    Json::Value jvRequest,
    NetworkOPs::FailHard failType,
    detail::TxnSignApiFacade& apiFacade,
    Role role)
{
    WriteLog (lsDEBUG, RPCHandler) <<
        "transactionGetSigningAccount: " << jvRequest;

    // Verify presence of the signer's account field
    const char accountField[] = "account";

    if (! jvRequest.isMember (accountField))
        return RPC::missing_field_error (accountField);

    // Turn the signer's account into a RippleAddress for multisign
    RippleAddress multiSignAddrID;
    if (! multiSignAddrID.setAccountID (
        jvRequest[accountField].asString ()))
    {
        return RPC::make_error (rpcSRC_ACT_MALFORMED,
            RPC::invalid_field_message (accountField));
    }

    // When multi-signing, the "Sequence" and "SigningPubKey" fields must
    // be passed in by the caller.
    using namespace detail;
    {
        Json::Value err = checkMultiSignFields (jvRequest);
        if (RPC::contains_error (err))
            return std::move (err);
    }

    // Add and amend fields based on the transaction type.
    Blob multiSignature;
    RippleAddress multiSignPubKey;
    SigningAccountParams multiSignParams (multiSignPubKey, multiSignature);

    transactionPreProcessResult preprocResult =
        transactionPreProcessImpl (
            jvRequest,
            apiFacade,
            role,
            multiSignParams);

    if (!preprocResult.second)
        return preprocResult.first;

    // Make sure the multiSignAddrID can legitimately multisign.
    {
        error_code_i const err =
            apiFacade.multiAcctMatchesPubKey (multiSignAddrID, multiSignPubKey);

        if (err != rpcSUCCESS)
            return rpcError (err);
    }

    // Make sure the STTx makes a legitimate Transaction.
    std::pair <Json::Value, Transaction::pointer> txn =
        transactionConstructImpl (preprocResult.second);

    if (!txn.second)
        return txn.first;

    Json::Value json = transactionFormatResultImpl (txn.second);
    if (RPC::contains_error (json))
        return json;

    // Finally, do what we were called for: return a SigningAccount object.
    insertSigningAccount (
        json, multiSignAddrID, multiSignPubKey, multiSignature);

    return json;
}

/** Returns a Json::objectValue. */
Json::Value transactionSubmitMultiSigned (
    Json::Value jvRequest,
    NetworkOPs::FailHard failType,
    detail::TxnSignApiFacade& apiFacade,
    Role role)
{
    WriteLog (lsDEBUG, RPCHandler)
        << "transactionSubmitMultiSigned: " << jvRequest;

    // When multi-signing, the "Sequence" and "SigningPubKey" fields must
    // be passed in by the caller.
    using namespace detail;
    {
        Json::Value err = checkMultiSignFields (jvRequest);
        if (RPC::contains_error (err))
            return std::move (err);
    }

    Json::Value& tx_json (jvRequest ["tx_json"]);

    auto const txJsonResult = checkTxJsonFields(tx_json, apiFacade, role, true);
    if (RPC::contains_error (txJsonResult.first))
        return std::move (txJsonResult.first);

    {
        Json::Value err = checkFee (jvRequest, apiFacade, role, AutoFill::dont);
        if (RPC::contains_error(err))
            return std::move (err);

        err = checkPayment (
            jvRequest,
            tx_json,
            txJsonResult.second,
            apiFacade,
            role,
            PathFind::dont);

        if (RPC::contains_error(err))
            return std::move (err);
    }

    // Grind through the JSON in tx_json to produce a STTx
    STTx::pointer stpTrans;
    {
        STParsedJSONObject parsedTx_json ("tx_json", tx_json);
        if (!parsedTx_json.object)
        {
            Json::Value jvResult;
            jvResult ["error"] = parsedTx_json.error ["error"];
            jvResult ["error_code"] = parsedTx_json.error ["error_code"];
            jvResult ["error_message"] = parsedTx_json.error ["error_message"];
            return jvResult;
        }

        std::unique_ptr <STObject> sopTrans = std::move (parsedTx_json.object);

        try
        {
            stpTrans = std::make_shared<STTx> (*sopTrans);
        }
        catch (std::exception& ex)
        {
            std::string reason (ex.what ());
            return RPC::make_error (rpcINTERNAL,
                "Exception while serializing transaction: " + reason);
        }
        std::string reason;
        if (!passesLocalChecks (*stpTrans, reason))
            return RPC::make_error (rpcINVALID_PARAMS, reason);
    }

    // Validate the fields in the serialized transaction.
    {
        // We now have the transaction text serialized and in the right format.
        // Verify the values of select fields.
        //
        // The SigningPubKey must be present but empty.
        if (!stpTrans->getFieldVL (sfSigningPubKey).empty ())
        {
            std::ostringstream err;
            err << "Invalid  " << sfSigningPubKey.fieldName
                << " field.  Field must be empty when multi-signing.";
            return RPC::make_error (rpcINVALID_PARAMS, err.str ());
        }

        // The Fee field must be greater than zero.
        if (!stpTrans->getFieldAmount (sfFee) > 0)
        {
            std::ostringstream err;
            err << "Invalid " << sfFee.fieldName
                << " field.  Value must be greater than zero.";
            return RPC::make_error (rpcINVALID_PARAMS, err.str ());
        }
    }

    // Check SigningAccounts for valid entries.
    const char* signingAccocuntsArrayName {
        sfSigningAccounts.getJsonName ().c_str ()};

    std::unique_ptr <STArray> signingAccounts;
    {
        // Verify that the SigningAccounts field is present and an array.
        if (! jvRequest.isMember (signingAccocuntsArrayName))
            return RPC::missing_field_error (signingAccocuntsArrayName);

        Json::Value& signingAccountsValue (
            jvRequest [signingAccocuntsArrayName]);

        if (! signingAccountsValue.isArray ())
        {
            std::ostringstream err;
                err << "Expected "
                << signingAccocuntsArrayName << " to be an array";
            return RPC::make_param_error (err.str ());
        }

        // Convert the SigningAccounts into SerializedTypes.
        STParsedJSONArray parsedSigningAccounts (
            signingAccocuntsArrayName, signingAccountsValue);

        if (!parsedSigningAccounts.array)
        {
            Json::Value jvResult;
            jvResult ["error"] = parsedSigningAccounts.error ["error"];
            jvResult ["error_code"] =
                parsedSigningAccounts.error ["error_code"];
            jvResult ["error_message"] =
                parsedSigningAccounts.error ["error_message"];
            return jvResult;
        }
        signingAccounts = std::move (parsedSigningAccounts.array);
    }

    for (STObject const& signingAccount : *signingAccounts)
    {
        // Only allow SigningAccount objects in the SigingAccounts array.
        if (signingAccount.getFName () != sfSigningAccount)
        {
            return RPC::make_param_error (
                "SigningAccounts array has a non-SigningAccount entry");
        }
    }

    // SigningAccounts are submitted sorted in Account order.  This
    // assures that the same list will always have the same hash.
    signingAccounts->sort (
        [] (STObject const& a, STObject const& b) {
            return (a.getFieldAccount (sfAccount).getAccountID () <
                b.getFieldAccount (sfAccount).getAccountID ()); });

    // There may be no duplicate Accounts in SigningAccounts
    auto const signingAccountsEnd = signingAccounts->end ();

    auto const dupAccountItr = std::adjacent_find (
        signingAccounts->begin (), signingAccountsEnd,
            [] (STObject const& a, STObject const& b) {
                return (a.getFieldAccount (sfAccount).getAccountID () ==
                    b.getFieldAccount (sfAccount).getAccountID ()); });

    if (dupAccountItr != signingAccountsEnd)
    {
        std::ostringstream err;
        err << "Duplicate multi-signing AccountIDs ("
            << dupAccountItr->getFieldAccount (sfAccount).humanAccountID ()
            << ") are not allowed.";
        return RPC::make_param_error(err.str ());
    }

    // Insert the SigningAccounts into the transaction.
    stpTrans->setFieldArray (sfSigningAccounts, *signingAccounts);

    // Make sure the SerializedTransaction makes a legitimate Transaction.
    std::pair <Json::Value, Transaction::pointer> txn =
        transactionConstructImpl (stpTrans);

    if (!txn.second)
        return txn.first;

    // Finally, submit the transaction.
    try
    {
        // FIXME: For performance, should use asynch interface
        apiFacade.processTransaction (
            txn.second, role == Role::ADMIN, true, failType);
    }
    catch (std::exception&)
    {
        return RPC::make_error (rpcINTERNAL,
            "Exception occurred during transaction submission.");
    }

    return transactionFormatResultImpl (txn.second);
}

} // RPC
} // ripple
