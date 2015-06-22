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
#include <ripple/test/jtx/multisign.h>
#include <ripple/test/jtx/utility.h>
#include <ripple/protocol/HashPrefix.h>
#include <ripple/protocol/JsonFields.h>
// #include <ripple/protocol/types.h>

namespace ripple {
namespace test {
namespace jtx {

Json::Value
signers (Account const& account,
    std::uint32_t quorum,
        std::vector<signer> const& v)
{
    Json::Value jv;
    jv[jss::Account] = account.human();
    jv[jss::TransactionType] = "SignerListSet";
    jv["SignerQuorum"] = quorum;
    auto& ja = jv["SignerEntries"];
    ja.resize(v.size());
    for(std::size_t i = 0; i < v.size(); ++i)
    {
        auto const& e = v[i];
        auto& je = ja[i]["SignerEntry"];
        je[jss::Account] = e.account.human();
        je["SignerWeight"] = e.weight;
    }
    return jv;
}

Json::Value
signers (Account const& account, none_t)
{
    Json::Value jv;
    jv[jss::Account] = account.human();
    jv[jss::TransactionType] = "SignerListSet";
    jv["SignerQuorum"] = 0;
    return jv;
}

//------------------------------------------------------------------------------

msig::msig (std::vector<msig::Reg> signers_)
        : signers(std::move(signers_))
{
    // Signatures must be applied in sorted order.
    std::sort(signers.begin(), signers.end(),
        [](msig::Reg const& lhs, msig::Reg const& rhs)
        {
            return lhs.acct.id() < rhs.acct.id();
        });
}

void
msig::operator()(Env const& env, JTx& jt) const
{
    auto const mySigners = signers;
    jt.signer = [mySigners, &env](Env&, JTx& jt)
    {
        jt["SigningPubKey"] = "";
        boost::optional<STObject> st;
        try
        {
            st = parse(jt.jv);
        }
        catch(parse_error const&)
        {
            env.test.log << pretty(jt.jv);
            throw;
        }
        auto& js = jt["Signers"];
        js.resize(mySigners.size());
        for(std::size_t i = 0; i < mySigners.size(); ++i)
        {
            auto const& e = mySigners[i];
            auto& jo = js[i]["Signer"];
            jo[jss::Account] = e.acct.human();
            jo[jss::SigningPubKey] = strHex(make_Slice(
                e.sig.pk().getAccountPublic()));

            Serializer ss;
            ss.add32 (HashPrefix::txMultiSign);
            st->addWithoutSigningFields(ss);
            ss.add160(e.acct.id());
            jo["MultiSignature"] = strHex(make_Slice(
                e.sig.sk().accountPrivateSign(ss.getData())));
        }
    };
}

} // jtx
} // test
} // ripple
