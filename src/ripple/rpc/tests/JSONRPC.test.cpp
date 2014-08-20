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
#include <beast/unit_test.h>

namespace ripple {

namespace RPC {

// Struct used to test calls to transactionSign, transactionSubmit,
// transactionGetSigningAccount, and transactionSubmitMultiSigned
struct TxnTestData
{
    // Gah, without constexpr I can't make this an enum and initialize
    // OR operators at compile time.  Punting with integer constants.
    static unsigned int const allGood         = 0x0;
    static unsigned int const signFail        = 0x1;
    static unsigned int const submitFail      = 0x2;
    static unsigned int const monoFail        = signFail | submitFail;
    static unsigned int const signMultiFail   = 0x4;
    static unsigned int const submitMultiFail = 0x8;
    static unsigned int const multiFail       = signMultiFail | submitMultiFail;
    static unsigned int const allFail         = monoFail | multiFail;

    char const* const description;
    char const* const json;
    unsigned int result;

    TxnTestData () = delete;
    TxnTestData (TxnTestData const&) = delete;
    TxnTestData& operator= (TxnTestData const&) = delete;
    TxnTestData (char const* descIn, char const* jsonIn, unsigned int resultIn)
    : description (descIn)
    , json (jsonIn)
    , result (resultIn)
    { }
};

// Declare storage for statics to avoid link errors.
unsigned int const TxnTestData::allGood;
unsigned int const TxnTestData::signFail;
unsigned int const TxnTestData::submitFail;
unsigned int const TxnTestData::monoFail;
unsigned int const TxnTestData::signMultiFail;
unsigned int const TxnTestData::submitMultiFail;
unsigned int const TxnTestData::multiFail;
unsigned int const TxnTestData::allFail;


static TxnTestData const txnTestArray [] =
{

{ "Minimal payment.",
R"({
    "command": "submit",
    "secret": "masterpassphrase",
    "tx_json": {
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": "1000000000",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "TransactionType": "Payment"
    }
})", TxnTestData::multiFail},

{ "Pass in Fee with minimal payment.",
R"({
    "command": "submit",
    "secret": "masterpassphrase",
    "tx_json": {
        "Fee": 10,
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": "1000000000",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "TransactionType": "Payment"
    }
})", TxnTestData::multiFail},

{ "Pass in Sequence.",
R"({
    "command": "submit",
    "secret": "masterpassphrase",
    "tx_json": {
        "Sequence": 0,
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": "1000000000",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "TransactionType": "Payment"
    }
})", TxnTestData::multiFail},

{ "Pass in Sequence and Fee with minimal payment.",
R"({
    "command": "submit",
    "secret": "masterpassphrase",
    "tx_json": {
        "Sequence": 0,
        "Fee": 10,
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": "1000000000",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "TransactionType": "Payment"
    }
})", TxnTestData::multiFail},

{ "Add 'fee_mult_max' field.",
R"({
    "command": "submit",
    "secret": "masterpassphrase",
    "fee_mult_max": 7,
    "tx_json": {
        "Sequence": 0,
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": "1000000000",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "TransactionType": "Payment"
    }
})", TxnTestData::multiFail},

{ "fee_mult_max is ignored if 'Fee' is present.",
R"({
    "command": "submit",
    "secret": "masterpassphrase",
    "fee_mult_max": 0,
    "tx_json": {
        "Sequence": 0,
        "Fee": 10,
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": "1000000000",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "TransactionType": "Payment"
    }
})", TxnTestData::multiFail},

{ "Invalid 'fee_mult_max' field.",
R"({
    "command": "submit",
    "secret": "masterpassphrase",
    "fee_mult_max": "NotAFeeMultiplier",
    "tx_json": {
        "Sequence": 0,
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": "1000000000",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "TransactionType": "Payment"
    }
})", TxnTestData::allFail},

{ "Invalid value for 'fee_mult_max' field.",
R"({
    "command": "submit",
    "secret": "masterpassphrase",
    "fee_mult_max": 0,
    "tx_json": {
        "Sequence": 0,
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": "1000000000",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "TransactionType": "Payment"
    }
})", TxnTestData::allFail},

{ "Missing 'Amount'.",
R"({
    "command": "submit",
    "secret": "masterpassphrase",
    "tx_json": {
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "TransactionType": "Payment"
    }
})", TxnTestData::allFail},

{ "Invalid 'Amount'.",
R"({
    "command": "submit",
    "secret": "masterpassphrase",
    "tx_json": {
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": "NotAnAmount",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "TransactionType": "Payment"
    }
})", TxnTestData::allFail},

{ "Missing 'Destination'.",
R"({
    "command": "submit",
    "secret": "masterpassphrase",
    "tx_json": {
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": "1000000000",
        "TransactionType": "Payment"
    }
})", TxnTestData::allFail},

{ "Invalid 'Destination'.",
R"({
    "command": "submit",
    "secret": "masterpassphrase",
    "tx_json": {
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": "1000000000",
        "Destination": "NotADestination",
        "TransactionType": "Payment"
    }
})", TxnTestData::allFail},

{ "Cannot create XRP to XRP paths.",
R"({
    "command": "submit",
    "secret": "masterpassphrase",
    "build_path": 1,
    "tx_json": {
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": "1000000000",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "TransactionType": "Payment"
    }
})", TxnTestData::allFail},

{ "Successful 'build_path'.",
R"({
    "command": "submit",
    "secret": "masterpassphrase",
    "build_path": 1,
    "tx_json": {
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": {
            "value": "10",
            "currency": "USD",
            "issuer": "0123456789012345678901234567890123456789"
        },
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "TransactionType": "Payment"
    }
})", TxnTestData::multiFail},

{ "Not valid to include both 'Paths' and 'build_path'.",
R"({
    "command": "submit",
    "secret": "masterpassphrase",
    "build_path": 1,
    "tx_json": {
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": {
            "value": "10",
            "currency": "USD",
            "issuer": "0123456789012345678901234567890123456789"
        },
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "Paths": "",
        "TransactionType": "Payment"
    }
})", TxnTestData::allFail},

{ "Successful 'SendMax'.",
R"({
    "command": "submit",
    "secret": "masterpassphrase",
    "build_path": 1,
    "tx_json": {
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": {
            "value": "10",
            "currency": "USD",
            "issuer": "0123456789012345678901234567890123456789"
        },
        "SendMax": {
            "value": "5",
            "currency": "USD",
            "issuer": "0123456789012345678901234567890123456789"
        },
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "TransactionType": "Payment"
    }
})", TxnTestData::multiFail},

{ "Even though 'Amount' may not be XRP for pathfinding, 'SendMax' may be XRP.",
R"({
    "command": "submit",
    "secret": "masterpassphrase",
    "build_path": 1,
    "tx_json": {
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": {
            "value": "10",
            "currency": "USD",
            "issuer": "0123456789012345678901234567890123456789"
        },
        "SendMax": 10000,
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "TransactionType": "Payment"
    }
})", TxnTestData::multiFail},

{ "'secret' must be present.",
R"({
    "command": "submit",
    "tx_json": {
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": "1000000000",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "TransactionType": "Payment"
    }
})", TxnTestData::allFail},

{ "'secret' must be non-empty.",
R"({
    "command": "submit",
    "secret": "",
    "tx_json": {
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": "1000000000",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "TransactionType": "Payment"
    }
})", TxnTestData::allFail},

{ "'tx_json' must be present.",
R"({
    "command": "submit",
    "secret": "masterpassphrase",
    "rx_json": {
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": "1000000000",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "TransactionType": "Payment"
    }
})", TxnTestData::allFail},

{ "'TransactionType' must be present.",
R"({
    "command": "submit",
    "secret": "masterpassphrase",
    "tx_json": {
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": "1000000000",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
    }
})", TxnTestData::allFail},

{ "The 'TransactionType' must be one of the pre-established transaction types.",
R"({
    "command": "submit",
    "secret": "masterpassphrase",
    "tx_json": {
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": "1000000000",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "TransactionType": "tt"
    }
})", TxnTestData::allFail},

{ "The 'TransactionType', however, may be represented with an integer.",
R"({
    "command": "submit",
    "secret": "masterpassphrase",
    "tx_json": {
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": "1000000000",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "TransactionType": 0
    }
})", TxnTestData::multiFail},

{ "'Account' must be present.",
R"({
    "command": "submit",
    "secret": "masterpassphrase",
    "tx_json": {
        "Amount": "1000000000",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "TransactionType": "Payment"
    }
})", TxnTestData::allFail},

{ "'Account' must be well formed.",
R"({
    "command": "submit",
    "secret": "masterpassphrase",
    "tx_json": {
        "Account": "NotAnAccount",
        "Amount": "1000000000",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "TransactionType": "Payment"
    }
})", TxnTestData::allFail},

{ "The 'offline' tag may be added to the transaction.",
R"({
    "command": "submit",
    "secret": "masterpassphrase",
    "offline": 0,
    "tx_json": {
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": "1000000000",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "TransactionType": "Payment"
    }
})", TxnTestData::multiFail},

{ "If 'offline' is true then a 'Sequence' field must be supplied.",
R"({
    "command": "submit",
    "secret": "masterpassphrase",
    "offline": 1,
    "tx_json": {
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": "1000000000",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "TransactionType": "Payment"
    }
})", TxnTestData::allFail},

{ "Valid transaction if 'offline' is true.",
R"({
    "command": "submit",
    "secret": "masterpassphrase",
    "offline": 1,
    "tx_json": {
        "Sequence": 0,
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": "1000000000",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "TransactionType": "Payment"
    }
})", TxnTestData::multiFail},

{ "A 'Flags' field may be specified.",
R"({
    "command": "submit",
    "secret": "masterpassphrase",
    "tx_json": {
        "Flags": 0,
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": "1000000000",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "TransactionType": "Payment"
    }
})", TxnTestData::multiFail},

{ "The 'Flags' field must be numeric.",
R"({
    "command": "submit",
    "secret": "masterpassphrase",
    "tx_json": {
        "Flags": "NotGoodFlags",
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": "1000000000",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "TransactionType": "Payment"
    }
})", TxnTestData::allFail},

{ "It's okay to add a 'debug_signing' field.",
R"({
    "command": "submit",
    "secret": "masterpassphrase",
    "debug_signing": 0,
    "tx_json": {
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": "1000000000",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "TransactionType": "Payment"
    }
})", TxnTestData::multiFail},

{ "Minimal get_signingaccount.",
R"({
    "command": "get_signingaccount",
    "account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
    "publickey": "aBQG8RQAzjs1eTKFEAQXr2gS4utcDiEC9wmi7pfUPTi27VCahwgw",
    "secret": "masterpassphrase",
    "tx_json": {
        "Account": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "Amount": "1000000000",
        "Destination": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Fee": 50,
        "Flags": 2147483648,
        "Sequence": 0,
        "SigningPubKey": "",
        "TransactionType": "Payment"
    }
})", TxnTestData::submitMultiFail},

{ "Minimal submit_multisigned.",
R"({
    "command": "submit_multisigned",
    "SigningAccounts": [
        {
            "SigningAccount": {
                "Account": "rPcNzota6B8YBokhYtcTNqQVCngtbnWfux",
                "MultiSignature": "3045022100F9ED357606932697A4FAB2BE7F222C21DD93CA4CFDD90357AADD07465E8457D6022038173193E3DFFFB5D78DD738CC0905395F885DA65B98FDB9793901FE3FD26ECE",
                "PublicKey": "02FE36A690D6973D55F88553F5D2C4202DE75F2CF8A6D0E17C70AC223F044501F8"
            }
        }
    ],
    "tx_json": {
        "Account": "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
        "Amount": "1000000000",
        "Destination": "rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
        "Fee": 50,
        "Flags": 2147483648,
        "Sequence": 0,
        "SigningPubKey": "",
        "TransactionType": "Payment"
    }
})", TxnTestData::monoFail | TxnTestData::signMultiFail},

};


class JSONRPC_test : public beast::unit_test::suite
{
public:
    void testAutoFillFees ()
    {
        std::string const secret = "masterpassphrase";
        RippleAddress rootSeedMaster
                = RippleAddress::createSeedGeneric (secret);

        RippleAddress rootGeneratorMaster
                = RippleAddress::createGeneratorPublic (rootSeedMaster);

        RippleAddress rootAddress
                = RippleAddress::createAccountPublic (rootGeneratorMaster, 0);

        std::uint64_t startAmount (100000);
        Ledger::pointer ledger (std::make_shared <Ledger> (
            rootAddress, startAmount));

        using namespace detail;
        TxnSignApiFacade apiFacade (TxnSignApiFacade::noNetOPs, ledger);

        {
            Json::Value req;
            Json::Value result;
            Json::Reader ().parse (
                "{ \"fee_mult_max\" : 1, \"tx_json\" : { } } "
                , req);
            detail::autofill_fee (req, apiFacade, result, true);

            expect (! contains_error (result), "Legal autofill_fee");
        }

        {
            Json::Value req;
            Json::Value result;
            Json::Reader ().parse (
                "{ \"fee_mult_max\" : 0, \"tx_json\" : { } } "
                , req);
            detail::autofill_fee (req, apiFacade, result, true);

            expect (contains_error (result), "Invalid autofill_fee");
        }
    }

    void testTransactionRPC ()
    {
        // A list of all the functions we want to test and their fail bits.
        using transactionFunc = Json::Value (*) (
            Json::Value params,
            NetworkOPs::FailHard failType,
            detail::TxnSignApiFacade& apiFacade,
            Role role);

        using TestStuff =
            std::tuple <transactionFunc, char const*, unsigned int>;

        static TestStuff const testFuncs [] =
        {
            TestStuff {transactionSign,              "sign",               TxnTestData::signFail},
            TestStuff {transactionSubmit,            "submit",             TxnTestData::submitFail},
#if RIPPLE_ENABLE_MULTI_SIGN
            TestStuff {transactionGetSigningAccount, "get_signingaccount", TxnTestData::signMultiFail},
            TestStuff {transactionSubmitMultiSigned, "submit_multisigned", TxnTestData::submitMultiFail}
#endif // RIPPLE_ENABLE_MULTI_SIGN
        };

        for (auto testFunc : testFuncs)
        {
            // For each JSON test.
            for (auto const& txnTest : txnTestArray)
            {
                Json::Value req;
                Json::Reader ().parse (txnTest.json, req);
                if (contains_error (req))
                    throw std::runtime_error (
                        "Internal JSONRPC_test error.  Bad test JSON.");

                static Role const testedRoles[] =
                    {Role::GUEST, Role::USER, Role::ADMIN, Role::FORBID};

                for (Role testRole : testedRoles)
                {
                    // Mock so we can run without a ledger.
                    detail::TxnSignApiFacade apiFacade (
                        detail::TxnSignApiFacade::noNetOPs);

                    Json::Value result = get<0>(testFunc) (
                        req,
                        NetworkOPs::FailHard::yes,
                        apiFacade,
                        testRole);

                    bool const expectPass = txnTest.result & get<2>(testFunc);
                    bool const pass = (contains_error (result) == expectPass);
                    expect (pass,
                        std::string (get<1>(testFunc)) +
                            ": " + txnTest.description);
                }
            }
        }
    }

    void run ()
    {
        testAutoFillFees ();
        testTransactionRPC ();
    }
};

BEAST_DEFINE_TESTSUITE(JSONRPC,ripple_app,ripple);

} // RPC
} // ripple
