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
#include <ripple/protocol/JsonFields.h>     // jss:: definitions
#include <ripple/test/jtx.h>

namespace ripple {
namespace test {

// EnvMulti -- A specialized jtx::Env that supports ledger advance.

class EnvMulti : public jtx::Env
{
public:
    // close_and_advance needs the last closed ledger.
    std::shared_ptr<Ledger const> lastClosedLedger;

    EnvMulti (beast::unit_test::suite& test_)
    : Env (test_)
    , lastClosedLedger (std::make_shared<Ledger>(false, *ledger))
    {
    }

    ~EnvMulti() override = default;
};

// Advance the ledger during testing.
void advance (EnvMulti& env)
{
    jtx::advance (env, env.lastClosedLedger);
}

class MultiSign_test : public beast::unit_test::suite
{
    // Unfunded accounts to use for phantom signing.
    jtx::Account const bogie {"bogie", KeyType::secp256k1};
    jtx::Account const demon {"demon", KeyType::ed25519};
    jtx::Account const ghost {"ghost", KeyType::secp256k1};
    jtx::Account const haunt {"haunt", KeyType::ed25519};
    jtx::Account const jinni {"jinni", KeyType::secp256k1};
    jtx::Account const phase {"phase", KeyType::ed25519};
    jtx::Account const shade {"shade", KeyType::secp256k1};
    jtx::Account const spook {"spook", KeyType::ed25519};

public:
    void test_noReserve()
    {
        using namespace jtx;
        EnvMulti env(*this);
        Account const alice {"alice", KeyType::secp256k1};

        // Pay alice enough to meet the initial reserve, but not enough to
        // meet the reserve for a SignerListSet.
        env.fund(XRP(200), alice);
        advance(env);
        env.require (owners (alice, 0));

        {
            // Attach a signer list to alice.  Should fail.
            Json::Value smallSigners = signers(alice, 1, { { bogie, 1 } });
            env(smallSigners, ter(tecINSUFFICIENT_RESERVE));
            advance(env);
            env.require (owners (alice, 0));

            // Fund alice enough to set the signer list, then attach signers.
            env(pay(env.master, alice, XRP(151)));
            advance(env);
            env(smallSigners);
            advance(env);
            env.require (owners (alice, 3));
        }
        {
            // Replace with the biggest possible signer list.  Should fail.
            Json::Value bigSigners = signers(alice, 1, {
                { bogie, 1 }, { demon, 1 }, { ghost, 1 }, { haunt, 1 },
                { jinni, 1 }, { phase, 1 }, { shade, 1 }, { spook, 1 }});
            env(bigSigners, ter(tecINSUFFICIENT_RESERVE));
            advance(env);
            env.require (owners (alice, 3));

            // Fund alice and succeed.
            env(pay(env.master, alice, XRP(350)));
            advance(env);
            env(bigSigners);
            advance(env);
            env.require (owners (alice, 10));
        }
        // Remove alice's signer list and get the owner count back.
        env(signers(alice, jtx::none));
        advance(env);
        env.require (owners (alice, 0));
    }

    void test_signerListSet()
    {
        using namespace jtx;
        EnvMulti env(*this);
        Account const alice {"alice", KeyType::ed25519};
        env.fund(XRP(1000), alice);

        // Add alice as a multisigner for herself.  Should fail.
        env(signers(alice, 1, { { alice, 1} }), ter (temBAD_SIGNER));

        // Add a signer with a weight of zero.  Should fail.
        env(signers(alice, 1, { { bogie, 0} }), ter (temBAD_WEIGHT));

        // Add a signer where the weight is too big.  Should fail since
        // the weight field is only 16 bits.  The jtx framework can't do
        // this kind of test, so it's commented out.
//      env(signers(alice, 1, { { bogie, 0x10000} }), ter (temBAD_WEIGHT));

        // Add the same signer twice.  Should fail.
        env(signers(alice, 1, {
            { bogie, 1 }, { demon, 1 }, { ghost, 1 }, { haunt, 1 },
            { jinni, 1 }, { phase, 1 }, { demon, 1 }, { spook, 1 }}),
            ter(temBAD_SIGNER));

        // Set a quorum of zero.  Should fail.
        env(signers(alice, 0, { { bogie, 1} }), ter (temMALFORMED));

        // Make a signer list where the quorum can't be met.  Should fail.
        env(signers(alice, 9, {
            { bogie, 1 }, { demon, 1 }, { ghost, 1 }, { haunt, 1 },
            { jinni, 1 }, { phase, 1 }, { shade, 1 }, { spook, 1 }}),
            ter(temBAD_QUORUM));

        // Make a signer list that's too big.  Should fail.
        Account const spare ("spare", KeyType::secp256k1);
        env(signers(alice, 1, {
            { bogie, 1 }, { demon, 1 }, { ghost, 1 }, { haunt, 1 },
            { jinni, 1 }, { phase, 1 }, { shade, 1 }, { spook, 1 },
            { spare, 1 }}),
            ter(temMALFORMED));

        advance(env);
        env.require (owners (alice, 0));
    }

    void test_phantomSigners()
    {
        using namespace jtx;
        EnvMulti env(*this);
        Account const alice {"alice", KeyType::ed25519};
        env.fund(XRP(1000), alice);
        advance(env);

        // Attach phantom signers to alice and use them for a transaction.
        env(signers(alice, 1, {{bogie, 1}, {demon, 1}}));
        advance(env);
        env.require (owners (alice, 4));

        // This should work.
        std::uint32_t aliceSeq = env.seq (alice);
        env(noop(alice), msig(bogie, demon));
        advance(env);
        expect (env.seq(alice) == aliceSeq + 1);

        // Either signer alone should work.
        aliceSeq = env.seq (alice);
        env(noop(alice), msig(bogie));
        advance(env);
        expect (env.seq(alice) == aliceSeq + 1);

        aliceSeq = env.seq (alice);
        env(noop(alice), msig(demon));
        advance(env);
        expect (env.seq(alice) == aliceSeq + 1);

        // Duplicate signers should fail.
        aliceSeq = env.seq (alice);
        env(noop(alice), msig(demon, demon), ter(temINVALID));
        advance(env);
        expect (env.seq(alice) == aliceSeq);

        // A non-signer should fail.
        aliceSeq = env.seq (alice);
        env(noop(alice), msig(bogie, spook), ter(tefBAD_SIGNATURE));
        advance(env);
        expect (env.seq(alice) == aliceSeq);

        // Multisign, but leave a nonempty sfSigners.  Should fail.
        {
            aliceSeq = env.seq (alice);
            Json::Value multiSig  = env.json (noop (alice), msig(bogie));

            env (env.jt (multiSig), ter (temINVALID));
            advance(env);
            expect (env.seq(alice) == aliceSeq);
        }

        // Don't meet the quorum.  Should fail.
        env(signers(alice, 2, {{bogie, 1}, {demon, 1}}));
        aliceSeq = env.seq (alice);
        env(noop(alice), msig(bogie), ter(tefBAD_QUORUM));
        advance(env);
        expect (env.seq(alice) == aliceSeq);

        // Meet the quorum.  Should succeed.
        aliceSeq = env.seq (alice);
        env(noop(alice), msig(bogie, demon));
        advance(env);
        expect (env.seq(alice) == aliceSeq + 1);
    }

    void test_misorderedSigners()
    {
        using namespace jtx;
        EnvMulti env(*this);
        Account const alice {"alice", KeyType::ed25519};
        env.fund(XRP(1000), alice);
        advance(env);

        // The signatures in a transaction must be submitted in sorted order.
        // Make sure the transaction fails if they are not.
        env(signers(alice, 1, {{bogie, 1}, {demon, 1}}));
        advance(env);
        env.require (owners(alice, 4));

        msig phantoms {bogie, demon};
        std::reverse (phantoms.signers.begin(), phantoms.signers.end());
        std::uint32_t const aliceSeq = env.seq (alice);
        env(noop(alice), phantoms, ter(temINVALID));
        advance(env);
        expect (env.seq(alice) == aliceSeq);
    }

    void test_masterSigners()
    {
        using namespace jtx;
        EnvMulti env(*this);
        Account const alice {"alice", KeyType::ed25519};
        Account const becky {"becky", KeyType::secp256k1};
        Account const cheri {"cheri", KeyType::ed25519};
        env.fund(XRP(1000), alice, becky, cheri);
        advance(env);

        // For a different situation, give alice a regular key but don't use it.
        Account const alie {"alie", KeyType::secp256k1};
        env(regkey (alice, alie));
        advance(env);
        std::uint32_t aliceSeq = env.seq (alice);
        env(noop(alice), sig(alice));
        env(noop(alice), sig(alie));
        advance(env);
        expect (env.seq(alice) == aliceSeq + 2);

        //Attach signers to alice
        env(signers(alice, 4, {{becky, 3}, {cheri, 4}}), sig (alice));
        advance(env);
        env.require (owners (alice, 4));

        // Attempt a multisigned transaction that meets the quorum.
        aliceSeq = env.seq (alice);
        env(noop(alice), msig(cheri));
        advance(env);
        expect (env.seq(alice) == aliceSeq + 1);

        // If we don't meet the quorum the transaction should fail.
        aliceSeq = env.seq (alice);
        env(noop(alice), msig(becky), ter(tefBAD_QUORUM));
        advance(env);
        expect (env.seq(alice) == aliceSeq);

        // Give becky and cheri regular keys.
        Account const beck {"beck", KeyType::ed25519};
        env(regkey (becky, beck));
        Account const cher {"cher", KeyType::ed25519};
        env(regkey (cheri, cher));
        advance(env);

        // becky's and cheri's master keys should still work.
        aliceSeq = env.seq (alice);
        env(noop(alice), msig(becky, cheri));
        advance(env);
        expect (env.seq(alice) == aliceSeq + 1);
    }

    void test_regularSigners()
    {
        using namespace jtx;
        EnvMulti env(*this);
        Account const alice {"alice", KeyType::secp256k1};
        Account const becky {"becky", KeyType::ed25519};
        Account const cheri {"cheri", KeyType::secp256k1};
        env.fund(XRP(1000), alice, becky, cheri);
        advance(env);

        // Attach signers to alice.
        env(signers(alice, 1, {{becky, 1}, {cheri, 1}}), sig (alice));

        // Give everyone regular keys.
        Account const alie {"alie", KeyType::ed25519};
        env(regkey (alice, alie));
        Account const beck {"beck", KeyType::secp256k1};
        env(regkey (becky, beck));
        Account const cher {"cher", KeyType::ed25519};
        env(regkey (cheri, cher));
        advance(env);

        // Disable cheri's master key to mix things up.
        env(fset (cheri, asfDisableMaster), sig(cheri));
        advance(env);

        // Attempt a multisigned transaction that meets the quorum.
        std::uint32_t aliceSeq = env.seq (alice);
        env(noop(alice), msig(msig::Reg{cheri, cher}));
        advance(env);
        expect (env.seq(alice) == aliceSeq + 1);

        // cheri should not be able to multisign using her master key.
        aliceSeq = env.seq (alice);
        env(noop(alice), msig(cheri), ter(tefMASTER_DISABLED));
        advance(env);
        expect (env.seq(alice) == aliceSeq);

        // becky should be able to multisign using either of her keys.
        aliceSeq = env.seq (alice);
        env(noop(alice), msig(becky));
        advance(env);
        expect (env.seq(alice) == aliceSeq + 1);

        aliceSeq = env.seq (alice);
        env(noop(alice), msig(msig::Reg{becky, beck}));
        advance(env);
        expect (env.seq(alice) == aliceSeq + 1);

        // Both becky and cheri should be able to sign using regular keys.
        aliceSeq = env.seq (alice);
        env(noop(alice),
            msig(msig::Reg{becky, beck}, msig::Reg{cheri, cher}));
        advance(env);
        expect (env.seq(alice) == aliceSeq + 1);

    }

    void test_heterogeneousSigners()
    {
        using namespace jtx;
        EnvMulti env(*this);
        Account const alice {"alice", KeyType::secp256k1};
        Account const becky {"becky", KeyType::ed25519};
        Account const cheri {"cheri", KeyType::secp256k1};
        Account const daria {"daria", KeyType::ed25519};
        env.fund(XRP(1000), alice, becky, cheri, daria);
        advance(env);

        // alice uses a regular key with the master disabled.
        Account const alie {"alie", KeyType::secp256k1};
        env(regkey (alice, alie));
        env(fset (alice, asfDisableMaster), sig(alice));

        // becky is master only without a regular key.

        // cheri has a regular key, but leaves the master key enabled.
        Account const cher {"cher", KeyType::secp256k1};
        env(regkey (cheri, cher));

        // daria has a regular key and disables her master key.
        Account const dari {"dari", KeyType::ed25519};
        env(regkey (daria, dari));
        env(fset (daria, asfDisableMaster), sig(daria));
        advance(env);

        // Attach signers to alice.
        env(signers(alice, 1,
            {{becky, 1}, {cheri, 1}, {daria, 1}, {jinni, 1}}), sig (alie));
        advance(env);
        env.require (owners (alice, 6));

        // Each type of signer should succeed individually.
        std::uint32_t aliceSeq = env.seq (alice);
        env(noop(alice), msig(becky));
        advance(env);
        expect (env.seq(alice) == aliceSeq + 1);

        aliceSeq = env.seq (alice);
        env(noop(alice), msig(cheri));
        advance(env);
        expect (env.seq(alice) == aliceSeq + 1);

        aliceSeq = env.seq (alice);
        env(noop(alice), msig(msig::Reg{cheri, cher}));
        advance(env);
        expect (env.seq(alice) == aliceSeq + 1);

        aliceSeq = env.seq (alice);
        env(noop(alice), msig(msig::Reg{daria, dari}));
        advance(env);
        expect (env.seq(alice) == aliceSeq + 1);

        aliceSeq = env.seq (alice);
        env(noop(alice), msig(jinni));
        advance(env);
        expect (env.seq(alice) == aliceSeq + 1);

        //  Should also work if all signers sign.
        aliceSeq = env.seq (alice);
        env(noop(alice),
            msig(becky, msig::Reg{cheri, cher}, msig::Reg{daria, dari}, jinni));
        advance(env);
        expect (env.seq(alice) == aliceSeq + 1);

        // Require all signers to sign.
        env(signers(alice, 0x3FFFC, {{becky, 0xFFFF},
            {cheri, 0xFFFF}, {daria, 0xFFFF}, {jinni, 0xFFFF}}), sig (alie));
        advance(env);
        env.require (owners (alice, 6));

        aliceSeq = env.seq (alice);
        env(noop(alice),
            msig(becky, msig::Reg{cheri, cher}, msig::Reg{daria, dari}, jinni));
        advance(env);
        expect (env.seq(alice) == aliceSeq + 1);

        // Try cheri with both key types.
        aliceSeq = env.seq (alice);
        env(noop(alice), msig(becky, cheri, msig::Reg{daria, dari}, jinni));
        advance(env);
        expect (env.seq(alice) == aliceSeq + 1);

        // Makes sure the maximum allowed number of signers works.
        env(signers(alice, 0x7FFF8, {{becky, 0xFFFF}, {cheri, 0xFFFF},
            {daria, 0xFFFF}, {haunt, 0xFFFF}, {jinni, 0xFFFF},
            {phase, 0xFFFF}, {shade, 0xFFFF}, {spook, 0xFFFF}}), sig (alie));
        advance(env);
        env.require (owners (alice, 10));

        aliceSeq = env.seq (alice);
        env(noop(alice), msig(becky, msig::Reg{cheri, cher},
            msig::Reg{daria, dari}, haunt, jinni, phase, shade, spook));
        advance(env);
        expect (env.seq(alice) == aliceSeq + 1);

        // One signer short should fail.
        aliceSeq = env.seq (alice);
        env(noop(alice), msig(becky, cheri, haunt, jinni, phase, shade, spook),
            ter (tefBAD_QUORUM));
        advance(env);
        expect (env.seq(alice) == aliceSeq);

        // Remove alice's signer list and get the owner count back.
        env(signers(alice, jtx::none), sig(alie));
        advance(env);
        env.require (owners (alice, 0));
    }

    // See if every kind of transaction can be successfully multi-signed.
    void test_txTypes()
    {
        using namespace jtx;
        EnvMulti env(*this);
        Account const alice {"alice", KeyType::secp256k1};
        Account const becky {"becky", KeyType::ed25519};
        Account const zelda {"zelda", KeyType::secp256k1};
        Account const gw {"gw"};
        auto const USD = gw["USD"];
        env.fund(XRP(1000), alice, becky, zelda, gw);
        advance(env);

        // alice uses a regular key with the master disabled.
        Account const alie {"alie", KeyType::secp256k1};
        env(regkey (alice, alie));
        env(fset (alice, asfDisableMaster), sig(alice));

        // Attach signers to alice.
        env(signers(alice, 2, {{becky, 1}, {bogie, 1}}), sig (alie));
        advance(env);
        env.require (owners (alice, 4));

        // Multisign a ttPAYMENT.
        std::uint32_t aliceSeq = env.seq (alice);
        env(pay(alice, env.master, XRP(1)), msig(becky, bogie));
        advance(env);
        expect (env.seq(alice) == aliceSeq + 1);

        // Multisign a ttACCOUNT_SET.
        aliceSeq = env.seq (alice);
        env(noop(alice), msig(becky, bogie));
        advance(env);
        expect (env.seq(alice) == aliceSeq + 1);

        // Multisign a ttREGULAR_KEY_SET.
        aliceSeq = env.seq (alice);
        Account const ace {"ace", KeyType::secp256k1};
        env(regkey (alice, ace), msig(becky, bogie));
        advance(env);
        expect (env.seq(alice) == aliceSeq + 1);

        // Multisign a ttTRUST_SET
        env(trust("alice", USD(100)),
            msig(becky, bogie), require (lines("alice", 1)));
        advance(env);
        env.require (owners (alice, 5));

        // Multisign a ttOFFER_CREATE transaction.
        env(pay(gw, alice, USD(50)));
        advance(env);
        env.require(balance(alice, USD(50)));
        env.require(balance(gw, alice["USD"](-50)));

        std::uint32_t const offerSeq = env.seq (alice);
        env(offer(alice, XRP(50), USD(50)), msig (becky, bogie));
        advance(env);
        env.require(owners(alice, 6));

        // Now multisign a ttOFFER_CANCEL canceling the offer we just created.
        {
            aliceSeq = env.seq (alice);
            Json::Value cancelOffer;
            cancelOffer[jss::Account] = alice.human();
            cancelOffer[jss::OfferSequence] = offerSeq;
            cancelOffer[jss::TransactionType] = "OfferCancel";
            env (cancelOffer, seq (aliceSeq), msig (becky, bogie));
            advance(env);
            expect (env.seq(alice) == aliceSeq + 1);
            env.require(owners(alice, 5));
        }

        // Multisign a ttSIGNER_LIST_SET.
        env(signers(alice, 3, {{becky, 1}, {bogie, 1}, {demon, 1}}),
            msig (becky, bogie));
        advance(env);
        env.require (owners (alice, 6));
    }

    void run() override
    {
        test_noReserve();
        test_signerListSet();
        test_phantomSigners();
        test_misorderedSigners();
        test_masterSigners();
        test_regularSigners();
        test_heterogeneousSigners();
        test_txTypes();
    }
};

#if RIPPLE_ENABLE_MULTI_SIGN
BEAST_DEFINE_TESTSUITE(MultiSign, app, ripple);
#endif // RIPPLE_ENABLE_MULTI_SIGN

} // test
} // ripple
