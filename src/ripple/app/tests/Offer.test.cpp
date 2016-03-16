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

#include <BeastConfig.h>
#include <ripple/app/main/Application.h>
#include <ripple/app/ledger/Ledger.h>
#include <ripple/basics/Log.h>
#include <ripple/test/jtx.h>
#include <ripple/test/jtx/Account.h>
#include <ripple/ledger/tests/PathSet.h>
#include <ripple/protocol/Feature.h>
#include <ripple/protocol/SystemParameters.h>

namespace ripple {
namespace test {

class Offer_test : public beast::unit_test::suite
{
    XRPAmount reserve(jtx::Env& env, std::uint32_t count)
    {
        return env.current()->fees().accountReserve (count);
    }

    std::uint32_t lastClose (jtx::Env& env)
    {
        return env.current()->info().parentCloseTime.time_since_epoch().count();
    }

    static auto
    xrpMinusFee (jtx::Env const& env, std::int64_t xrpAmount)
        -> jtx::PrettyAmount
    {
        using namespace jtx;
        auto feeDrops = env.current ()->fees ().base;
        return drops (dropsPerXRP<std::int64_t>::value * xrpAmount - feeDrops);
    };

public:
    void testRmFundedOffer ()
    {
        testcase ("Incorrect Removal of Funded Offers");

        // We need at least two paths. One at good quality and one at bad quality.
        // The bad quality path needs two offer books in a row. Each offer book
        // should have two offers at the same quality, the offers should be
        // completely consumed, and the payment should should require both offers to
        // be satisified. The first offer must be "taker gets" XRP. Old, broken
        // would remove the first "taker gets" xrp offer, even though the offer is
        // still funded and not used for the payment.

        using namespace jtx;
        Env env (*this);

        // ledger close times have a dynamic resolution depending on network
        // conditions it appears the resolution in test is 10 seconds
        env.close ();

        auto const gw = Account ("gateway");
        auto const USD = gw["USD"];
        auto const BTC = gw["BTC"];
        Account const alice ("alice");
        Account const bob ("bob");
        Account const carol ("carol");

        env.fund (XRP (10000), alice, bob, carol, gw);
        env.trust (USD (1000), alice, bob, carol);
        env.trust (BTC (1000), alice, bob, carol);

        env (pay (gw, alice, BTC (1000)));

        env (pay (gw, carol, USD (1000)));
        env (pay (gw, carol, BTC (1000)));

        // Must be two offers at the same quality
        // "taker gets" must be XRP
        // (Different amounts so I can distinguish the offers)
        env (offer (carol, BTC (49), XRP (49)));
        env (offer (carol, BTC (51), XRP (51)));

        // Offers for the poor quality path
        // Must be two offers at the same quality
        env (offer (carol, XRP (50), USD (50)));
        env (offer (carol, XRP (50), USD (50)));

        // Offers for the good quality path
        env (offer (carol, BTC (1), USD (100)));

        PathSet paths (Path (XRP, USD), Path (USD));

        env (pay ("alice", "bob", USD (100)), json (paths.json ()),
            sendmax (BTC (1000)), txflags (tfPartialPayment));

        env.require (balance ("bob", USD (100)));
        expect (!isOffer (env, "carol", BTC (1), USD (100)) &&
            isOffer (env, "carol", BTC (49), XRP (49)));
    }

    void testCanceledOffer ()
    {
        testcase ("Removing Canceled Offers");

        using namespace jtx;
        Env env (*this);
        auto const gw = Account ("gateway");
        auto const USD = gw["USD"];

        env.fund (XRP (10000), "alice", gw);
        env.trust (USD (100), "alice");

        env (pay (gw, "alice", USD (50)));

        auto const firstOfferSeq = env.seq ("alice");
        Json::StaticString const osKey ("OfferSequence");

        env (offer ("alice", XRP (500), USD (100)),
            require (offers ("alice", 1)));

        expect (isOffer (env, "alice", XRP (500), USD (100)));

        // cancel the offer above and replace it with a new offer
        env (offer ("alice", XRP (300), USD (100)), json (osKey, firstOfferSeq),
            require (offers ("alice", 1)));

        expect (isOffer (env, "alice", XRP (300), USD (100)) &&
            !isOffer (env, "alice", XRP (500), USD (100)));

        // Test canceling non-existant offer.
        env (offer ("alice", XRP (400), USD (200)), json (osKey, firstOfferSeq),
            require (offers ("alice", 2)));

        expect (isOffer (env, "alice", XRP (300), USD (100)) &&
            isOffer (env, "alice", XRP (400), USD (200)));
    }

    void testTinyPayment ()
    {
        testcase ("Tiny payments");

        // Regression test for tiny payments
        using namespace jtx;
        using namespace std::chrono_literals;
        auto const alice = Account ("alice");
        auto const bob = Account ("bob");
        auto const carol = Account ("carol");
        auto const gw = Account ("gw");

        auto const USD = gw["USD"];
        auto const EUR = gw["EUR"];

        Env env (*this);


        env.fund (XRP (10000), alice, bob, carol, gw);
        env.trust (USD (1000), alice, bob, carol);
        env.trust (EUR (1000), alice, bob, carol);
        env (pay (gw, alice, USD (100)));
        env (pay (gw, carol, EUR (100)));

        // Create more offers than the loop max count in DeliverNodeReverse
        for (int i=0;i<101;++i)
            env (offer (carol, USD (1), EUR (2)));

        for (auto timeDelta : {
            - env.closed()->info().closeTimeResolution,
                env.closed()->info().closeTimeResolution} )
        {
            auto const closeTime = STAmountSO::soTime + timeDelta;
            env.close (closeTime);
            *stAmountCalcSwitchover = closeTime > STAmountSO::soTime;
            // Will fail without the underflow fix
            auto expectedResult = *stAmountCalcSwitchover ?
                tesSUCCESS : tecPATH_PARTIAL;
            env (pay ("alice", "bob", EUR (epsilon)), path (~EUR),
                sendmax (USD (100)), ter (expectedResult));
        }
    }

    void testXRPTinyPayment ()
    {
        testcase ("XRP Tiny payments");

        // Regression test for tiny xrp payments
        // In some cases, when the payment code calculates
        // the amount of xrp needed as input to an xrp->iou offer
        // it would incorrectly round the amount to zero (even when
        // round-up was set to true).
        // The bug would cause funded offers to be incorrectly removed
        // because the code thought they were unfunded.
        // The conditions to trigger the bug are:
        // 1) When we calculate the amount of input xrp needed for an offer from
        //    xrp->iou, the amount is less than 1 drop (after rounding up the float
        //    representation).
        // 2) There is another offer in the same book with a quality sufficiently bad that
        //    when calculating the input amount needed the amount is not set to zero.

        using namespace jtx;
        using namespace std::chrono_literals;
        auto const alice = Account ("alice");
        auto const bob = Account ("bob");
        auto const carol = Account ("carol");
        auto const dan = Account ("dan");
        auto const erin = Account ("erin");
        auto const gw = Account ("gw");

        auto const USD = gw["USD"];

        for (auto withFix : {false, true})
        {
            Env env (*this);

            auto closeTime = [&]
            {
                auto const delta =
                    100 * env.closed ()->info ().closeTimeResolution;
                if (withFix)
                    return STAmountSO::soTime2 + delta;
                else
                    return STAmountSO::soTime2 - delta;
            }();

            env.fund (XRP (10000), alice, bob, carol, dan, erin, gw);
            env.trust (USD (1000), alice, bob, carol, dan, erin);
            env (pay (gw, carol, USD (0.99999)));
            env (pay (gw, dan, USD (1)));
            env (pay (gw, erin, USD (1)));

            // Carol doesn't quite have enough funds for this offer
            // The amount left after this offer is taken will cause
            // STAmount to incorrectly round to zero when the next offer
            // (at a good quality) is considered. (when the
            // stAmountCalcSwitchover2 patch is inactive)
            env (offer (carol, drops (1), USD (1)));
            // Offer at a quality poor enough so when the input xrp is calculated
            // in the reverse pass, the amount is not zero.
            env (offer (dan, XRP (100), USD (1)));

            env.close (closeTime);
            // This is the funded offer that will be incorrectly removed.
            // It is considered after the offer from carol, which leaves a
            // tiny amount left to pay. When calculating the amount of xrp
            // needed for this offer, it will incorrectly compute zero in both
            // the forward and reverse passes (when the stAmountCalcSwitchover2 is
            // inactive.)
            env (offer (erin, drops (1), USD (1)));

            {
                env (pay (alice, bob, USD (1)), path (~USD),
                    sendmax (XRP (102)),
                    txflags (tfNoRippleDirect | tfPartialPayment));

                env.require (
                    offers (carol, 0),
                    offers (dan, 1));
                if (!withFix)
                {
                    // funded offer was removed
                    env.require (
                        balance ("erin", USD (1)),
                        offers (erin, 0));
                }
                else
                {
                    // offer was correctly consumed. There is still some
                    // liquidity left on that offer.
                    env.require (
                        balance ("erin", USD (0.99999)),
                        offers (erin, 1));
                }
            }
        }
    }

    void testEnforceNoRipple ()
    {
        testcase ("Enforce No Ripple");

        using namespace jtx;

        auto const gw = Account ("gateway");
        auto const USD = gw["USD"];
        auto const BTC = gw["BTC"];
        auto const EUR = gw["EUR"];
        Account const alice ("alice");
        Account const bob ("bob");
        Account const carol ("carol");
        Account const dan ("dan");

        {
            // No ripple with an implied account step after an offer
            Env env (*this);
            auto const gw1 = Account ("gw1");
            auto const USD1 = gw1["USD"];
            auto const gw2 = Account ("gw2");
            auto const USD2 = gw2["USD"];

            env.fund (XRP (10000), alice, noripple (bob), carol, dan, gw1, gw2);
            env.trust (USD1 (1000), alice, carol, dan);
            env(trust (bob, USD1 (1000), tfSetNoRipple));
            env.trust (USD2 (1000), alice, carol, dan);
            env(trust (bob, USD2 (1000), tfSetNoRipple));

            env (pay (gw1, dan, USD1 (50)));
            env (pay (gw1, bob, USD1 (50)));
            env (pay (gw2, bob, USD2 (50)));

            env (offer (dan, XRP (50), USD1 (50)));

            env (pay (alice, carol, USD2 (50)), path (~USD1, bob), ter(tecPATH_DRY),
                sendmax (XRP (50)), txflags (tfNoRippleDirect));
        }
        {
            // Make sure payment works with default flags
            Env env (*this);
            auto const gw1 = Account ("gw1");
            auto const USD1 = gw1["USD"];
            auto const gw2 = Account ("gw2");
            auto const USD2 = gw2["USD"];

            env.fund (XRP (10000), alice, bob, carol, dan, gw1, gw2);
            env.trust (USD1 (1000), alice, bob, carol, dan);
            env.trust (USD2 (1000), alice, bob, carol, dan);

            env (pay (gw1, dan, USD1 (50)));
            env (pay (gw1, bob, USD1 (50)));
            env (pay (gw2, bob, USD2 (50)));

            env (offer (dan, XRP (50), USD1 (50)));

            env (pay (alice, carol, USD2 (50)), path (~USD1, bob),
                sendmax (XRP (50)), txflags (tfNoRippleDirect));

            env.require (balance (alice, xrpMinusFee (env, 10000 - 50)));
            env.require (balance (bob, USD1 (100)));
            env.require (balance (bob, USD2 (0)));
            env.require (balance (carol, USD2 (50)));
        }
    }

    void
    testInsufficientReserve ()
    {
        testcase ("Insufficient Reserve");

        // If an account places an offer and its balance
        // *before* the transaction began isn't high enough
        // to meet the reserve *after* the transaction runs,
        // then no offer should go on the books but if the
        // offer partially or fully crossed the tx succeeds.

        using namespace jtx;

        auto const gw = Account("gateway");
        auto const USD = gw["USD"];

        auto const usdOffer = USD(1000);
        auto const xrpOffer = XRP(1000);

        // No crossing:
        {
            Env env(*this);
            env.fund (XRP(1000000), gw);

            auto const f = env.current ()->fees ().base;
            auto const r = reserve (env, 0);

            env.fund (r + f, "alice");

            env (trust ("alice", usdOffer),           ter(tesSUCCESS));
            env (pay (gw, "alice", usdOffer),         ter(tesSUCCESS));
            env (offer ("alice", xrpOffer, usdOffer), ter(tecINSUF_RESERVE_OFFER));

            env.require (
                balance ("alice", r - f),
                owners ("alice", 1));
        }

        // Partial cross:
        {
            Env env(*this);
            env.fund (XRP(1000000), gw);

            auto const f = env.current ()->fees ().base;
            auto const r = reserve (env, 0);

            auto const usdOffer2 = USD(500);
            auto const xrpOffer2 = XRP(500);

            env.fund (r + f + xrpOffer, "bob");
            env (offer ("bob", usdOffer2, xrpOffer2),   ter(tesSUCCESS));
            env.fund (r + f, "alice");
            env (trust ("alice", usdOffer),             ter(tesSUCCESS));
            env (pay (gw, "alice", usdOffer),           ter(tesSUCCESS));
            env (offer ("alice", xrpOffer, usdOffer),   ter(tesSUCCESS));

            env.require (
                balance ("alice", r - f + xrpOffer2),
                balance ("alice", usdOffer2),
                owners ("alice", 1),
                balance ("bob", r + xrpOffer2),
                balance ("bob", usdOffer2),
                owners ("bob", 1));
        }

        // Account has enough reserve as is, but not enough
        // if an offer were added. Attempt to sell IOUs to
        // buy XRP. If it fully crosses, we succeed.
        {
            Env env(*this);
            env.fund (XRP(1000000), gw);

            auto const f = env.current ()->fees ().base;
            auto const r = reserve (env, 0);

            auto const usdOffer2 = USD(500);
            auto const xrpOffer2 = XRP(500);

            env.fund (r + f + xrpOffer, "bob", "carol");
            env (offer ("bob", usdOffer2, xrpOffer2),    ter(tesSUCCESS));
            env (offer ("carol", usdOffer, xrpOffer),    ter(tesSUCCESS));

            env.fund (r + f, "alice");
            env (trust ("alice", usdOffer),              ter(tesSUCCESS));
            env (pay (gw, "alice", usdOffer),            ter(tesSUCCESS));
            env (offer ("alice", xrpOffer, usdOffer),    ter(tesSUCCESS));

            env.require (
                balance ("alice", r - f + xrpOffer),
                balance ("alice", USD(0)),
                owners ("alice", 1),
                balance ("bob", r + xrpOffer2),
                balance ("bob", usdOffer2),
                owners ("bob", 1),
                balance ("carol", r + xrpOffer2),
                balance ("carol", usdOffer2),
                owners ("carol", 2));
        }
    }

    // Helper function that returns the Offers on an account.
    static std::vector<std::shared_ptr<SLE const>>
    offersOnAccount (jtx::Env& env, jtx::Account account)
    {
        std::vector<std::shared_ptr<SLE const>> result;
        forEachItem (*env.current (), account,
            [&env, &result](std::shared_ptr<SLE const> const& sle)
            {
                if (sle->getType() == ltOFFER)
                     result.push_back (sle);
            });
        return result;
    }

    void
    testFillModes ()
    {
        testcase ("Fill Modes");

        using namespace jtx;

        auto const startBalance = XRP(1000000);
        auto const gw = Account("gateway");
        auto const USD = gw["USD"];

        // Fill or Kill - unless we fully cross, just charge
        // a fee and not place the offer on the books:
        {
            Env env(*this);
            env.fund (startBalance, gw);

            auto const f = env.current ()->fees ().base;

            env.fund (startBalance, "alice", "bob");
            env (offer ("bob", USD(500), XRP(500)),  ter(tesSUCCESS));
            env (trust ("alice", USD(1000)),         ter(tesSUCCESS));
            env (pay (gw, "alice", USD(1000)),       ter(tesSUCCESS));

            // Order that can't be filled:
            env (offer ("alice", XRP(1000), USD(1000)),
                txflags (tfFillOrKill),              ter(tesSUCCESS));

            env.require (
                balance ("alice", startBalance - f - f),
                balance ("alice", USD(1000)),
                owners ("alice", 1),
                offers ("alice", 0),
                balance ("bob", startBalance - f),
                balance ("bob", USD(none)),
                owners ("bob", 1),
                offers ("bob", 1));

            // Order that can be filled
            env (offer ("alice", XRP(500), USD(500)),
                txflags (tfFillOrKill),              ter(tesSUCCESS));

            env.require (
                balance ("alice", startBalance - f - f - f + XRP(500)),
                balance ("alice", USD(500)),
                owners ("alice", 1),
                offers ("alice", 0),
                balance ("bob", startBalance - f - XRP(500)),
                balance ("bob", USD(500)),
                owners ("bob", 1),
                offers ("bob", 0));
        }

        // Immediate or Cancel - cross as much as possible
        // and add nothing on the books:
        {
            Env env(*this);
            env.fund (startBalance, gw);

            auto const f = env.current ()->fees ().base;

            env.fund (startBalance, "alice", "bob");

            env (trust ("alice", USD(1000)),                 ter(tesSUCCESS));
            env (pay (gw, "alice", USD(1000)),               ter(tesSUCCESS));

            // No cross:
            env (offer ("alice", XRP(1000), USD(1000)),
                txflags (tfImmediateOrCancel),               ter(tesSUCCESS));

            env.require (
                balance ("alice", startBalance - f - f),
                balance ("alice", USD(1000)),
                owners ("alice", 1),
                offers ("alice", 0));

            // Partially cross:
            env (offer ("bob", USD(50), XRP(50)),            ter(tesSUCCESS));
            env (offer ("alice", XRP(1000), USD(1000)),
                txflags (tfImmediateOrCancel),               ter(tesSUCCESS));

            env.require (
                balance ("alice", startBalance - f - f - f + XRP(50)),
                balance ("alice", USD(950)),
                owners ("alice", 1),
                offers ("alice", 0),
                balance ("bob", startBalance - f - XRP(50)),
                balance ("bob", USD(50)),
                owners ("bob", 1),
                offers ("bob", 0));

            // Fully cross:
            env (offer ("bob", USD(50), XRP(50)),            ter(tesSUCCESS));
            env (offer ("alice", XRP(50), USD(50)),
                txflags (tfImmediateOrCancel),               ter(tesSUCCESS));

            env.require (
                balance ("alice", startBalance - f - f - f - f + XRP(100)),
                balance ("alice", USD(900)),
                owners ("alice", 1),
                offers ("alice", 0),
                balance ("bob", startBalance - f - f - XRP(100)),
                balance ("bob", USD(100)),
                owners ("bob", 1),
                offers ("bob", 0));
        }

        // tfPassive -- place the offer without crossing it.
        {
            Env env(*this);
            env.fund (startBalance, gw);

            auto const f = env.current ()->fees ().base;

            env.fund (startBalance, "alice", "bob");
            env.close();

            env (trust ("bob", USD(1000)));
            env.close();

            env (pay (gw, "bob", USD(1000)));
            env.close();

            env (offer ("alice", USD(1000), XRP(2000)));
            env.close();

            auto const aliceOffers = offersOnAccount (env, "alice");
            expect (aliceOffers.size() == 1);
            for (auto offerPtr : aliceOffers)
            {
                auto const& offer = *offerPtr;
                expect (offer[sfTakerGets] == XRP (2000));
                expect (offer[sfTakerPays] == USD (1000));
            }

            // bob creates a passive offer that could cross alice's.
            // bob's offer should stay in the ledger.
            env (offer ("bob", XRP(2000), USD(1000), tfPassive));
            env.close();
            env.require (offers ("alice", 1));

            auto const bobOffers = offersOnAccount (env, "bob");
            expect (bobOffers.size() == 1);
            for (auto offerPtr : bobOffers)
            {
                auto const& offer = *offerPtr;
                expect (offer[sfTakerGets] == USD (1000));
                expect (offer[sfTakerPays] == XRP (2000));
            }

            // It should be possible for gw to cross both of those offers.
            env (offer (gw, XRP(2000), USD(1000)));
            env.close();
            env.require (offers ("alice", 0));
            env.require (offers ("gw", 0));
            env.require (offers ("bob", 1));

            env (offer (gw, USD(1000), XRP(2000)));
            env.close();
            env.require (offers ("bob", 0));
            env.require (offers ("gw", 0));
        }
    }

    void
    testMalformed()
    {
        testcase ("Malformed Detection");

        using namespace jtx;

        auto const startBalance = XRP(1000000);
        auto const gw = Account("gateway");
        auto const USD = gw["USD"];

        Env env(*this);
        env.fund (startBalance, gw);

        env.fund (startBalance, "alice");

        // Order that has invalid flags
        env (offer ("alice", USD(1000), XRP(1000)),
            txflags (tfImmediateOrCancel + 1),            ter(temINVALID_FLAG));
        env.require (
            balance ("alice", startBalance),
            owners ("alice", 0),
            offers ("alice", 0));

        // Order with incompatible flags
        env (offer ("alice", USD(1000), XRP(1000)),
            txflags (tfImmediateOrCancel | tfFillOrKill), ter(temINVALID_FLAG));
        env.require (
            balance ("alice", startBalance),
            owners ("alice", 0),
            offers ("alice", 0));

        // Sell and buy the same asset
        {
            // Alice tries an XRP to XRP order:
            env (offer ("alice", XRP(1000), XRP(1000)),   ter(temBAD_OFFER));
            env.require (
                owners ("alice", 0),
                offers ("alice", 0));

            // Alice tries an IOU to IOU order:
            env (trust ("alice", USD(1000)),              ter(tesSUCCESS));
            env (pay (gw, "alice", USD(1000)),            ter(tesSUCCESS));
            env (offer ("alice", USD(1000), USD(1000)),   ter(temREDUNDANT));
            env.require (
                owners ("alice", 1),
                offers ("alice", 0));
        }

        // Offers with negative amounts
        {
            env (offer ("alice", -USD(1000), XRP(1000)),  ter(temBAD_OFFER));
            env.require (
                owners ("alice", 1),
                offers ("alice", 0));

            env (offer ("alice", USD(1000), -XRP(1000)),  ter(temBAD_OFFER));
            env.require (
                owners ("alice", 1),
                offers ("alice", 0));
        }

        // Offer with a bad expiration
        {
            Json::StaticString const key ("Expiration");

            env (offer ("alice", USD(1000), XRP(1000)),
                json (key, std::uint32_t (0)),            ter(temBAD_EXPIRATION));
            env.require (
                owners ("alice", 1),
                offers ("alice", 0));
        }

        // Offer with a bad offer sequence
        {
            Json::StaticString const key ("OfferSequence");

            env (offer ("alice", USD(1000), XRP(1000)),
                json (key, std::uint32_t (0)),            ter(temBAD_SEQUENCE));
            env.require (
                owners ("alice", 1),
                offers ("alice", 0));
        }

        // Use XRP as a currency code
        {
            auto const BAD = IOU(gw, badCurrency());

            env (offer ("alice", XRP(1000), BAD(1000)),   ter(temBAD_CURRENCY));
            env.require (
                owners ("alice", 1),
                offers ("alice", 0));
        }
    }

    void
    testExpiration ()
    {
        testcase ("Offer Expiration");

        using namespace jtx;

        auto const gw = Account("gateway");
        auto const USD = gw["USD"];

        auto const startBalance = XRP(1000000);
        auto const usdOffer = USD(1000);
        auto const xrpOffer = XRP(1000);

        Json::StaticString const key ("Expiration");

        Env env(*this);
        env.fund (startBalance, gw, "alice", "bob");
        env.close();

        auto const f = env.current ()->fees ().base;

        // Place an offer that should have already expired
        env (trust ("alice", usdOffer),             ter(tesSUCCESS));
        env (pay (gw, "alice", usdOffer),           ter(tesSUCCESS));
        env.close();
        env.require (
            balance ("alice", startBalance - f),
            balance ("alice", usdOffer),
            offers ("alice", 0),
            owners ("alice", 1));

        env (offer ("alice", xrpOffer, usdOffer),
            json (key, lastClose(env)),             ter(tesSUCCESS));
        env.require (
            balance ("alice", startBalance - f - f),
            balance ("alice", usdOffer),
            offers ("alice", 0),
            owners ("alice", 1));
        env.close();

        // Add an offer that's expires before the next ledger close
        env (offer ("alice", xrpOffer, usdOffer),
            json (key, lastClose(env) + 1),         ter(tesSUCCESS));
        env.require (
            balance ("alice", startBalance - f - f - f),
            balance ("alice", usdOffer),
            offers ("alice", 1),
            owners ("alice", 2));

        // The offer expires (it's not removed yet)
        env.close ();
        env.require (
            balance ("alice", startBalance - f - f - f),
            balance ("alice", usdOffer),
            offers ("alice", 1),
            owners ("alice", 2));

        // Add offer - the expired offer is removed
        env (offer ("bob", usdOffer, xrpOffer),     ter(tesSUCCESS));
        env.require (
            balance ("alice", startBalance - f - f - f),
            balance ("alice", usdOffer),
            offers ("alice", 0),
            owners ("alice", 1));
        env.require (
            balance ("bob", startBalance - f),
            balance ("bob", USD(none)),
            offers ("bob", 1),
            owners ("bob", 1));
    }

    // Helper function that validates a *defaulted* trustline.  If the
    // trustline is not defaulted then the tests will not pass.
    void
    verifyDefaultTrustline (jtx::Env& env,
        jtx::Account const& account, jtx::PrettyAmount const& expectBalance)
    {
        auto const sleTrust =
            env.le (keylet::line(account.id(), expectBalance.value().issue()));
        expect (sleTrust);
        if (sleTrust)
        {
            Issue const issue = expectBalance.value().issue();
            bool const accountLow = account.id() < issue.account;

            STAmount low (issue);
            STAmount high (issue);

            low.setIssuer (accountLow ? account.id() : issue.account);
            high.setIssuer (accountLow ? issue.account : account.id());

            expect (sleTrust->getFieldAmount (sfLowLimit) == low);
            expect (sleTrust->getFieldAmount (sfHighLimit) == high);

            STAmount actualBalance = sleTrust->getFieldAmount (sfBalance);
            if (! accountLow)
                actualBalance.negate();

            expect (actualBalance == expectBalance);
        }
    }

    void testPartialCross()
    {
        // Test a number of different corner cases regarding adding a
        // possibly crossable offer to an account.  The test is table
        // driven so it should be easy to add or remove tests.
        testcase ("Partial Crossing");

        using namespace jtx;

        auto const gw = Account("gateway");
        auto const USD = gw["USD"];

        Env env(*this);
        env.fund (XRP(10000000), gw);

        // The fee that's charged for transactions
        auto const f = env.current ()->fees ().base;

        // To keep things simple all offers are 1 : 1 for XRP : USD.
        enum preTrustType {noPreTrust, gwPreTrust, acctPreTrust};
        struct TestData
        {
            std::string account;       // Account operated on
            STAmount fundXrp;          // Account funded with
            int bookAmount;            // USD -> XRP offer on the books
            preTrustType preTrust;     // If true, pre-establish trust line
            int offerAmount;           // Account offers this much XRP -> USD
            TER tec;                   // Returned tec code
            STAmount spentXrp;         // Amount removed from fundXrp
            PrettyAmount balanceUsd;   // Balance on account end
            int offers;                // Offers on account
            int owners;                // Owners on account
        };

        TestData const tests[]
        {
//acct                             fundXrp  bookAmt      preTrust  offerAmt                     tec       spentXrp    balanceUSD  offers  owners
{"ann",             reserve (env, 0) + 0*f,       1,   noPreTrust,     1000,      tecUNFUNDED_OFFER,             f, USD(      0),      0,     0}, // Account is at the reserve, and will dip below once fees are subtracted.
{"bev",             reserve (env, 0) + 1*f,       1,   noPreTrust,     1000,      tecUNFUNDED_OFFER,             f, USD(      0),      0,     0}, // Account has just enough for the reserve and the fee.
{"cam",             reserve (env, 0) + 2*f,       0,   noPreTrust,     1000, tecINSUF_RESERVE_OFFER,             f, USD(      0),      0,     0}, // Account has enough for the reserve, the fee and the offer, and a bit more, but not enough for the reserve after the offer is placed.
{"deb",             reserve (env, 0) + 2*f,       1,   noPreTrust,     1000,             tesSUCCESS,           2*f, USD(0.00001),      0,     1}, // Account has enough to buy a little USD then the offer runs dry.
{"eve",             reserve (env, 1) + 0*f,       0,   noPreTrust,     1000,             tesSUCCESS,             f, USD(      0),      1,     1}, // No offer to cross
{"flo",             reserve (env, 1) + 0*f,       1,   noPreTrust,     1000,             tesSUCCESS, XRP(   1) + f, USD(      1),      0,     1},
{"gay",             reserve (env, 1) + 1*f,    1000,   noPreTrust,     1000,             tesSUCCESS, XRP(  50) + f, USD(     50),      0,     1},
{"hye", XRP(1000)                    + 1*f,    1000,   noPreTrust,     1000,             tesSUCCESS, XRP( 800) + f, USD(    800),      0,     1},
{"ivy", XRP(   1) + reserve (env, 1) + 1*f,       1,   noPreTrust,     1000,             tesSUCCESS, XRP(   1) + f, USD(      1),      0,     1},
{"joy", XRP(   1) + reserve (env, 2) + 1*f,       1,   noPreTrust,     1000,             tesSUCCESS, XRP(   1) + f, USD(      1),      1,     2},
{"kim", XRP( 900) + reserve (env, 2) + 1*f,     999,   noPreTrust,     1000,             tesSUCCESS, XRP( 999) + f, USD(    999),      0,     1},
{"liz", XRP( 998) + reserve (env, 0) + 1*f,     999,   noPreTrust,     1000,             tesSUCCESS, XRP( 998) + f, USD(    998),      0,     1},
{"meg", XRP( 998) + reserve (env, 1) + 1*f,     999,   noPreTrust,     1000,             tesSUCCESS, XRP( 999) + f, USD(    999),      0,     1},
{"nia", XRP( 998) + reserve (env, 2) + 1*f,     999,   noPreTrust,     1000,             tesSUCCESS, XRP( 999) + f, USD(    999),      1,     2},
{"ova", XRP( 999) + reserve (env, 0) + 1*f,    1000,   noPreTrust,     1000,             tesSUCCESS, XRP( 999) + f, USD(    999),      0,     1},
{"pam", XRP( 999) + reserve (env, 1) + 1*f,    1000,   noPreTrust,     1000,             tesSUCCESS, XRP(1000) + f, USD(   1000),      0,     1},
{"rae", XRP( 999) + reserve (env, 2) + 1*f,    1000,   noPreTrust,     1000,             tesSUCCESS, XRP(1000) + f, USD(   1000),      0,     1},
{"sue", XRP(1000) + reserve (env, 2) + 1*f,       0,   noPreTrust,     1000,             tesSUCCESS,             f, USD(      0),      1,     1},

//---------------------Pre-established trust lines -----------------------------
{"abe",             reserve (env, 0) + 0*f,       1,   gwPreTrust,     1000,      tecUNFUNDED_OFFER,             f, USD(      0),      0,     0},
{"bud",             reserve (env, 0) + 1*f,       1,   gwPreTrust,     1000,      tecUNFUNDED_OFFER,             f, USD(      0),      0,     0},
{"che",             reserve (env, 0) + 2*f,       0,   gwPreTrust,     1000, tecINSUF_RESERVE_OFFER,             f, USD(      0),      0,     0},
{"dan",             reserve (env, 0) + 2*f,       1,   gwPreTrust,     1000,             tesSUCCESS,           2*f, USD(0.00001),      0,     0},
{"eli", XRP(  20) + reserve (env, 0) + 1*f,    1000,   gwPreTrust,     1000,             tesSUCCESS, XRP(20) + 1*f, USD(     20),      0,     0},
{"fee",             reserve (env, 1) + 0*f,       0,   gwPreTrust,     1000,             tesSUCCESS,             f, USD(      0),      1,     1},
{"gar",             reserve (env, 1) + 0*f,       1,   gwPreTrust,     1000,             tesSUCCESS, XRP(   1) + f, USD(      1),      1,     1},
{"hal",             reserve (env, 1) + 1*f,       1,   gwPreTrust,     1000,             tesSUCCESS, XRP(   1) + f, USD(      1),      1,     1},

{"ned",             reserve (env, 1) + 0*f,       1, acctPreTrust,     1000,      tecUNFUNDED_OFFER,           2*f, USD(      0),      0,     1},
{"ole",             reserve (env, 1) + 1*f,       1, acctPreTrust,     1000,      tecUNFUNDED_OFFER,           2*f, USD(      0),      0,     1},
{"pat",             reserve (env, 1) + 2*f,       0, acctPreTrust,     1000,      tecUNFUNDED_OFFER,           2*f, USD(      0),      0,     1},
{"quy",             reserve (env, 1) + 2*f,       1, acctPreTrust,     1000,      tecUNFUNDED_OFFER,           2*f, USD(      0),      0,     1},
{"ron",             reserve (env, 1) + 3*f,       0, acctPreTrust,     1000, tecINSUF_RESERVE_OFFER,           2*f, USD(      0),      0,     1},
{"syd",             reserve (env, 1) + 3*f,       1, acctPreTrust,     1000,             tesSUCCESS,           3*f, USD(0.00001),      0,     1},
{"ted", XRP(  20) + reserve (env, 1) + 2*f,    1000, acctPreTrust,     1000,             tesSUCCESS, XRP(20) + 2*f, USD(     20),      0,     1},
{"uli",             reserve (env, 2) + 0*f,       0, acctPreTrust,     1000, tecINSUF_RESERVE_OFFER,           2*f, USD(      0),      0,     1},
{"vic",             reserve (env, 2) + 0*f,       1, acctPreTrust,     1000,             tesSUCCESS, XRP( 1) + 2*f, USD(      1),      0,     1},
{"wes",             reserve (env, 2) + 1*f,       0, acctPreTrust,     1000,             tesSUCCESS,           2*f, USD(      0),      1,     2},
{"xan",             reserve (env, 2) + 1*f,       1, acctPreTrust,     1000,             tesSUCCESS, XRP( 1) + 2*f, USD(      1),      1,     2},
};

        for (auto const& t : tests)
        {
            auto const acct = Account(t.account);
            env.fund (t.fundXrp, acct);
            env.close();

            // Make sure gateway has no current offers.
            env.require (offers (gw, 0));

            // The gateway optionally creates an offer that would be crossed.
            auto const book = t.bookAmount;
            if (book)
                env (offer (gw, XRP (book), USD (book)));
            env.close();
            std::uint32_t const gwOfferSeq = env.seq (gw) - 1;

            // Optionally pre-establish a trustline between gw and acct.
            if (t.preTrust == gwPreTrust)
                env (trust (gw, acct["USD"] (1)));

            // Optionally pre-establish a trustline between acct and gw.
            // Note this is not really part of the test, so we expect there
            // to be enough XRP reserve for acct to create the trust line.
            if (t.preTrust == acctPreTrust)
                env (trust (acct, USD (1)));

            env.close();

            // Acct creates an offer.  This is the heart of the test.
            auto const acctOffer = t.offerAmount;
            env (offer (acct, USD (acctOffer), XRP (acctOffer)), ter (t.tec));
            env.close();
            std::uint32_t const acctOfferSeq = env.seq (acct) - 1;

            expect (env.balance (acct, USD.issue()) == t.balanceUsd);
            expect (env.balance (acct, xrpIssue()) == t.fundXrp - t.spentXrp);
            env.require (offers (acct, t.offers));
            env.require (owners (acct, t.owners));

            auto acctOffers = offersOnAccount (env, acct);
            expect (acctOffers.size() == t.offers);
            if (acctOffers.size() && t.offers)
            {
                auto const& acctOffer = *(acctOffers.front());

                auto const leftover = t.offerAmount - t.bookAmount;
                expect (acctOffer[sfTakerGets] == XRP (leftover));
                expect (acctOffer[sfTakerPays] == USD (leftover));
            }

            if (t.preTrust == noPreTrust)
            {
                if (t.balanceUsd.value().signum())
                {
                    // Verify the correct contents of the trustline
                    verifyDefaultTrustline (env, acct, t.balanceUsd);
                }
                else
                {
                    // Verify that no trustline was created.
                    auto const sleTrust =
                        env.le (keylet::line(acct, USD.issue()));
                    expect (! sleTrust);
                }
            }

            // Give the next loop a clean slate by canceling any left-overs
            // in the offers.
            env (offer_cancel (acct, acctOfferSeq));
            env (offer_cancel (gw, gwOfferSeq));
            env.close();
        }
    }

    // I believe that the cases in the previous test covers all of these
    // tests.  Do you agree?????
    void
    testUnfundedCross()
    {
        testcase ("Unfunded Crossing");

        using namespace jtx;

        auto const gw = Account("gateway");
        auto const USD = gw["USD"];

        auto const usdOffer = USD(1000);
        auto const xrpOffer = XRP(1000);

        Env env(*this);
        env.fund (XRP(1000000), gw);

        // The fee that's charged for transactions
        auto const f = env.current ()->fees ().base;

        // Account is at the reserve, and will dip below once
        // fees are subtracted.
        env.fund (reserve (env, 0), "alice");
        env (offer ("alice", usdOffer, xrpOffer),     ter(tecUNFUNDED_OFFER));
        env.require (
            balance ("alice", reserve (env, 0) - f),
            offers ("alice", 0),
            owners ("alice", 0));

        // Account has just enough for the reserve and the
        // fee.
        env.fund (reserve (env, 0) + f, "bob");
        env (offer ("bob", usdOffer, xrpOffer),       ter(tecUNFUNDED_OFFER));
        env.require (
            balance ("bob", reserve (env, 0)),
            offers ("bob", 0),
            owners ("bob", 0));

        // Account has enough for the reserve, the fee and
        // the offer, and a bit more, but not enough for the
        // reserve after the offer is placed.
        env.fund (reserve (env, 0) + f + XRP(1), "carol");
        env (offer ("carol", usdOffer, xrpOffer),     ter(tecINSUF_RESERVE_OFFER));
        env.require (
            balance ("carol", reserve (env, 0) + XRP(1)),
            offers ("carol", 0),
            owners ("carol", 0));

        // Account has enough for the reserve plus one
        // offer, and the fee.
        env.fund (reserve (env, 1) + f, "dan");
        env (offer ("dan", usdOffer, xrpOffer),       ter(tesSUCCESS));
        env.require (
            balance ("dan", reserve (env, 1)),
            offers ("dan", 1),
            owners ("dan", 1));

        // Account has enough for the reserve plus one
        // offer, the fee and part of the offer amount.
        env.fund (reserve (env, 1) + f + xrpOffer, "eve");
        env (offer ("eve", usdOffer, xrpOffer),       ter(tesSUCCESS));
        env.require (
            balance ("eve", reserve (env, 1) + xrpOffer),
            offers ("eve", 1),
            owners ("eve", 1));
    }

    void
    testXRPDirectCross()
    {
        testcase ("XRP Direct Crossing");

        using namespace jtx;

        auto const gw = Account("gateway");
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const USD = gw["USD"];

        auto const usdOffer = USD(1000);
        auto const xrpOffer = XRP(1000);

        Env env(*this);
        env.fund (XRP(1000000), gw, bob);
        env.close();

        // The fee that's charged for transactions.
        auto const fee = env.current ()->fees ().base;

        // alice's account has enough for the reserve, one trust line plus two
        // offers, and two fees.
        env.fund (reserve (env, 2) + (fee * 2), alice);
        env.close();

        env (trust(alice, usdOffer));

        env.close();

        env (pay(gw, alice, usdOffer));
        env.close();
        env.require (
            balance (alice, usdOffer),
            offers (alice, 0),
            offers (bob, 0));

        // The scenario:
        //   o alice has USD but wants XRP.
        //   o bob has XRP but wants USD.
        auto const alicesXRP = env.balance (alice);
        auto const bobsXRP = env.balance (bob);

        env (offer (alice, xrpOffer, usdOffer));
        env.close();
        env (offer (bob, usdOffer, xrpOffer));

        env.close();
        env.require (
            balance (alice, USD(0)),
            balance (bob, usdOffer),
            balance (alice, alicesXRP + xrpOffer - fee),
            balance (bob,   bobsXRP   - xrpOffer - fee),
            offers (alice, 0),
            offers (bob, 0));

        verifyDefaultTrustline (env, bob, usdOffer);

        // Make two more offers that leave one of the offers non-dry.
        env (offer (alice, USD(999), XRP(999)));
        env (offer (bob, xrpOffer, usdOffer));

        env.close();
        env.require (balance (alice, USD(999)));
        env.require (balance (bob, USD(1)));
        env.require (offers (alice, 0));
        verifyDefaultTrustline (env, bob, USD(1));
        {
            auto bobsOffers = offersOnAccount (env, bob);
            expect (bobsOffers.size() == 1);
            auto const& bobsOffer = *(bobsOffers.front());

            expect (bobsOffer[sfLedgerEntryType] == ltOFFER);
            expect (bobsOffer[sfTakerGets] == USD (1));
            expect (bobsOffer[sfTakerPays] == XRP (1));
        }
   }

    void
    testDirectCross()
    {
        testcase ("Direct Crossing");

        using namespace jtx;

        auto const gw = Account("gateway");
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const USD = gw["USD"];
        auto const EUR = gw["EUR"];

        auto const usdOffer = USD(1000);
        auto const eurOffer = EUR(1000);

        Env env(*this);
        env.fund (XRP(1000000), gw);
        env.close();

        // The fee that's charged for transactions.
        auto const fee = env.current ()->fees ().base;

        // Each account has enough for the reserve, two trust lines, one
        // offer, and two fees.
        env.fund (reserve (env, 3) + (fee * 3), alice);
        env.fund (reserve (env, 3) + (fee * 2), bob);
        env.close();
        env (trust(alice, usdOffer));
        env (trust(bob, eurOffer));
        env.close();

        env (pay(gw, alice, usdOffer));
        env (pay(gw, bob, eurOffer));
        env.close();
        env.require (
            balance (alice, usdOffer),
            balance (bob, eurOffer));

        // The scenario:
        //   o alice has USD but wants EUR.
        //   o bob has EUR but wants USD.
        env (offer (alice, eurOffer, usdOffer));
        env (offer (bob, usdOffer, eurOffer));

        env.close();
        env.require (
            balance (alice, eurOffer),
            balance (bob, usdOffer),
            offers (alice, 0),
            offers (bob, 0));
        verifyDefaultTrustline (env, alice, eurOffer);
        verifyDefaultTrustline (env, bob, usdOffer);

        // Make two more offers that leave one of the offers non-dry.
        env (offer (alice, USD(999), eurOffer));
        env (offer (bob, eurOffer, usdOffer));

        env.close();
        env.require (
            balance (alice, USD(999)),
            balance (alice, EUR(1)),
            balance (bob, USD(1)),
            balance (bob, EUR(999)),
            offers (alice, 0));
        verifyDefaultTrustline (env, alice, EUR(1));
        verifyDefaultTrustline (env, bob, USD(1));
        {
            auto bobsOffers = offersOnAccount (env, bob);
            expect (bobsOffers.size() == 1);
            auto const& bobsOffer = *(bobsOffers.front());

            expect (bobsOffer[sfTakerGets] == USD (1));
            expect (bobsOffer[sfTakerPays] == EUR (1));
        }

        // alice makes one more offer that cleans out bob's offer.
        env (offer (alice, USD(1), EUR(1)));

        env.close();
        env.require (balance (alice, USD(1000)));
        env.require (balance (alice, EUR(none)));
        env.require (balance (bob, USD(none)));
        env.require (balance (bob, EUR(1000)));
        env.require (offers (alice, 0));
        env.require (offers (bob, 0));

        // The two trustlines that were generated by offers should be gone.
        expect (! env.le (keylet::line (alice.id(), EUR.issue())));
        expect (! env.le (keylet::line (bob.id(), USD.issue())));
    }

    void
    testBridgedCross()
    {
        testcase ("Bridged Crossing");

        using namespace jtx;

        auto const gw    = Account("gateway");
        auto const alice = Account("alice");
        auto const bob   = Account("bob");
        auto const carol = Account("carol");
        auto const USD = gw["USD"];
        auto const EUR = gw["EUR"];

        auto const usdOffer = USD(1000);
        auto const eurOffer = EUR(1000);

        Env env(*this);

        env.fund (XRP(1000000), gw, alice, bob, carol);
        env.close();

        env (trust(alice, usdOffer));
        env (trust(carol, eurOffer));
        env.close();
        env (pay(gw, alice, usdOffer));
        env (pay(gw, carol, eurOffer));
        env.close();

        // The scenario:
        //   o alice has USD but wants XPR.
        //   o bob has XRP but wants EUR.
        //   o carol has EUR but wants USD.
        // Note that carol's offer must come last.  If carol's offer is placed
        // before bob's or alice's, then autobridging will not occur.
        env (offer (alice, XRP(1000), usdOffer));
        env (offer (bob, eurOffer, XRP(1000)));
        auto const bobXrpBalance = env.balance (bob);
        env.close();

        // carol makes an offer that partially consumes alice and bob's offers.
        env (offer (carol, USD(400), EUR(400)));
        env.close();

        env.require (
            balance (alice, USD(600)),
            balance (bob,   EUR(400)),
            balance (carol, USD(400)),
            balance (bob, bobXrpBalance - XRP(400)),
            offers (carol, 0));
        verifyDefaultTrustline (env, bob, EUR(400));
        verifyDefaultTrustline (env, carol, USD(400));
        {
            auto const alicesOffers = offersOnAccount (env, alice);
            expect (alicesOffers.size() == 1);
            auto const& alicesOffer = *(alicesOffers.front());

            expect (alicesOffer[sfLedgerEntryType] == ltOFFER);
            expect (alicesOffer[sfTakerGets] == USD (600));
            expect (alicesOffer[sfTakerPays] == XRP (600));
        }
        {
            auto const bobsOffers = offersOnAccount (env, bob);
            expect (bobsOffers.size() == 1);
            auto const& bobsOffer = *(bobsOffers.front());

            expect (bobsOffer[sfLedgerEntryType] == ltOFFER);
            expect (bobsOffer[sfTakerGets] == XRP (600));
            expect (bobsOffer[sfTakerPays] == EUR (600));
        }

        // carol makes an offer that exactly consumes alice and bob's offers.
        env (offer (carol, USD(600), EUR(600)));
        env.close();

        env.require (
            balance (alice, USD(0)),
            balance (bob, eurOffer),
            balance (carol, usdOffer),
            balance (bob, bobXrpBalance - XRP(1000)),
            offers (bob, 0),
            offers (carol, 0));
        verifyDefaultTrustline (env, bob, EUR(1000));
        verifyDefaultTrustline (env, carol, USD(1000));

        // In pre-flow code alice's offer is left empty in the ledger.
        auto const alicesOffers = offersOnAccount (env, alice);
        if (alicesOffers.size() != 0)
        {
            expect (alicesOffers.size() == 1);
            auto const& alicesOffer = *(alicesOffers.front());

            expect (alicesOffer[sfLedgerEntryType] == ltOFFER);
            expect (alicesOffer[sfTakerGets] == USD (0));
            expect (alicesOffer[sfTakerPays] == XRP (0));
        }
    }

    void
    testSellOffer()
    {
        // Test a number of different corner cases regarding offer crossing
        // when the tfSell flag is set.  The test is table driven so it
        // should be easy to add or remove tests.
        testcase ("Sell Offer");

        using namespace jtx;

        auto const gw = Account("gateway");
        auto const USD = gw["USD"];

        Env env(*this);
        env.fund (XRP(10000000), gw);

        // The fee that's charged for transactions
        auto const f = env.current ()->fees ().base;

        // To keep things simple all offers are 1 : 1 for XRP : USD.
        enum preTrustType {noPreTrust, gwPreTrust, acctPreTrust};
        struct TestData
        {
            std::string account;       // Account operated on
            STAmount fundXrp;          // XRP acct funded with
            STAmount fundUSD;          // USD acct funded with
            STAmount gwGets;           // gw's offer
            STAmount gwPays;           //
            STAmount acctGets;         // acct's offer
            STAmount acctPays;         //
            TER tec;                   // Returned tec code
            STAmount spentXrp;         // Amount removed from fundXrp
            STAmount finalUsd;         // Final USD balance on acct
            int offers;                // Offers on acct
            int owners;                // Owners on acct
            STAmount takerGets;        // Remainder of acct's offer
            STAmount takerPays;        //

            // Constructor with takerGets/takerPays
            TestData (
                std::string&& account_,         // Account operated on
                STAmount const& fundXrp_,       // XRP acct funded with
                STAmount const& fundUSD_,       // USD acct funded with
                STAmount const& gwGets_,        // gw's offer
                STAmount const& gwPays_,        //
                STAmount const& acctGets_,      // acct's offer
                STAmount const& acctPays_,      //
                TER tec_,                       // Returned tec code
                STAmount const& spentXrp_,      // Amount removed from fundXrp
                STAmount const& finalUsd_,      // Final USD balance on acct
                int offers_,                    // Offers on acct
                int owners_,                    // Owners on acct
                STAmount const& takerGets_,     // Remainder of acct's offer
                STAmount const& takerPays_)     //
                : account (std::move(account_))
                , fundXrp (fundXrp_)
                , fundUSD (fundUSD_)
                , gwGets (gwGets_)
                , gwPays (gwPays_)
                , acctGets (acctGets_)
                , acctPays (acctPays_)
                , tec (tec_)
                , spentXrp (spentXrp_)
                , finalUsd (finalUsd_)
                , offers (offers_)
                , owners (owners_)
                , takerGets (takerGets_)
                , takerPays (takerPays_)
            { }

            // Constructor without takerGets/takerPays
            TestData (
                std::string&& account_,         // Account operated on
                STAmount const& fundXrp_,       // XRP acct funded with
                STAmount const& fundUSD_,       // USD acct funded with
                STAmount const& gwGets_,        // gw's offer
                STAmount const& gwPays_,        //
                STAmount const& acctGets_,      // acct's offer
                STAmount const& acctPays_,      //
                TER tec_,                       // Returned tec code
                STAmount const& spentXrp_,      // Amount removed from fundXrp
                STAmount const& finalUsd_,      // Final USD balance on acct
                int offers_,                    // Offers on acct
                int owners_)                    // Owners on acct
                : TestData (std::move(account_), fundXrp_, fundUSD_, gwGets_,
                  gwPays_, acctGets_, acctPays_, tec_, spentXrp_, finalUsd_,
                  offers_, owners_, STAmount::saZero, STAmount::saZero)
            { }
        };

        TestData const tests[]
        {
// acct pays XRP
//acct                           fundXrp  fundUSD   gwGets   gwPays  acctGets  acctPays                      tec         spentXrp  finalUSD  offers  owners  takerGets  takerPays
{"ann", XRP(10) + reserve (env, 0) + 1*f, USD( 0), XRP(10), USD( 5),  USD(10),  XRP(10), tecINSUF_RESERVE_OFFER, XRP( 0) + (1*f),  USD( 0),      0,     0},
{"bev", XRP(10) + reserve (env, 1) + 1*f, USD( 0), XRP(10), USD( 5),  USD(10),  XRP(10),             tesSUCCESS, XRP( 0) + (1*f),  USD( 0),      1,     1,     XRP(10),  USD(10)},
{"cam", XRP(10) + reserve (env, 0) + 1*f, USD( 0), XRP(10), USD(10),  USD(10),  XRP(10),             tesSUCCESS, XRP(10) + (1*f),  USD(10),      0,     1},
{"deb", XRP(10) + reserve (env, 0) + 1*f, USD( 0), XRP(10), USD(20),  USD(10),  XRP(10),             tesSUCCESS, XRP(10) + (1*f),  USD(20),      0,     1},
{"eve", XRP(10) + reserve (env, 0) + 1*f, USD( 0), XRP(10), USD(20),  USD( 5),  XRP( 5),             tesSUCCESS, XRP( 5) + (1*f),  USD(10),      0,     1},
{"flo", XRP(10) + reserve (env, 0) + 1*f, USD( 0), XRP(10), USD(20),  USD(20),  XRP(20),             tesSUCCESS, XRP(10) + (1*f),  USD(20),      0,     1},
{"gay", XRP(20) + reserve (env, 1) + 1*f, USD( 0), XRP(10), USD(20),  USD(20),  XRP(20),             tesSUCCESS, XRP(10) + (1*f),  USD(20),      0,     1},
{"hye", XRP(20) + reserve (env, 2) + 1*f, USD( 0), XRP(10), USD(20),  USD(20),  XRP(20),             tesSUCCESS, XRP(10) + (1*f),  USD(20),      1,     2,     XRP(10),  USD(10)},
// acct pays USD
{"meg",           reserve (env, 1) + 2*f, USD(10), USD(10), XRP( 5),  XRP(10),  USD(10), tecINSUF_RESERVE_OFFER, XRP(  0) + (2*f),  USD(10),      0,     1},
{"nia",           reserve (env, 2) + 2*f, USD(10), USD(10), XRP( 5),  XRP(10),  USD(10),             tesSUCCESS, XRP(  0) + (2*f),  USD(10),      1,     2,     USD(10),  XRP(10)},
{"ova",           reserve (env, 1) + 2*f, USD(10), USD(10), XRP(10),  XRP(10),  USD(10),             tesSUCCESS, XRP(-10) + (2*f),  USD( 0),      0,     1},
{"pam",           reserve (env, 1) + 2*f, USD(10), USD(10), XRP(20),  XRP(10),  USD(10),             tesSUCCESS, XRP(-20) + (2*f),  USD( 0),      0,     1},
{"qui",           reserve (env, 1) + 2*f, USD(10), USD(20), XRP(40),  XRP(10),  USD(10),             tesSUCCESS, XRP(-20) + (2*f),  USD( 0),      0,     1},
{"rae",           reserve (env, 2) + 2*f, USD(10), USD( 5), XRP( 5),  XRP(10),  USD(10),             tesSUCCESS, XRP( -5) + (2*f),  USD( 5),      1,     2,     USD( 5),  XRP( 5)},
{"sue",           reserve (env, 2) + 2*f, USD(10), USD( 5), XRP(10),  XRP(10),  USD(10),             tesSUCCESS, XRP(-10) + (2*f),  USD( 5),      1,     2,     USD( 5),  XRP( 5)},
};
        auto const zeroUsd = USD(0);
        for (auto const& t : tests)
        {
            // Make sure gateway has no current offers.
            env.require (offers (gw, 0));

            auto const acct = Account(t.account);

            env.fund (t.fundXrp, acct);
            env.close();

            // Optionally give acct some USD.  This is not part of the test,
            // so we assume that acct has sufficient USD to cover the reserve
            // on the trust line.
            if (t.fundUSD != zeroUsd)
            {
                env (trust (acct, t.fundUSD));
                env.close();
                env (pay (gw, acct, t.fundUSD));
                env.close();
            }

            env (offer (gw, t.gwGets, t.gwPays));
            env.close();
            std::uint32_t const gwOfferSeq = env.seq (gw) - 1;

            // Acct creates a tfSell offer.  This is the heart of the test.
            env (offer (acct, t.acctGets, t.acctPays, tfSell), ter (t.tec));
            env.close();
            std::uint32_t const acctOfferSeq = env.seq (acct) - 1;

            // Check results
            expect (env.balance (acct, USD.issue()) == t.finalUsd);
            expect (env.balance (acct, xrpIssue()) == t.fundXrp - t.spentXrp);
            env.require (offers (acct, t.offers));
            env.require (owners (acct, t.owners));

            if (t.offers)
            {
                auto const acctOffers = offersOnAccount (env, acct);
                if (acctOffers.size() > 0)
                {
                    expect (acctOffers.size() == 1);
                    auto const& acctOffer = *(acctOffers.front());

                    expect (acctOffer[sfLedgerEntryType] == ltOFFER);
                    expect (acctOffer[sfTakerGets] == t.takerGets);
                    expect (acctOffer[sfTakerPays] == t.takerPays);
                }
            }

            // Give the next loop a clean slate by canceling any left-overs
            // in the offers.
            env (offer_cancel (acct, acctOfferSeq));
            env (offer_cancel (gw, gwOfferSeq));
            env.close();
        }
    }

    void
    testTransferRateOffer()
    {
        testcase ("Transfer Rate Offer");

        using namespace jtx;

        auto const gw1 = Account("gateway1");
        auto const USD = gw1["USD"];

        Env env(*this);

        // The fee that's charged for transactions.
        auto const fee = env.current ()->fees ().base;

        env.fund (XRP(100000), gw1);
        env.close();

        env(rate(gw1, 1.25));
        {
            auto const alice = Account("alice");
            auto const bob   = Account("bob");
            env.fund (XRP(100) + reserve(env, 2) + (fee*2), alice, bob);
            env.close();

            env (trust(alice, USD(200)));
            env (trust(bob, USD(200)));
            env.close();

            env (pay (gw1, bob, USD(125)));
            env.close();

            // bob offers to sell USD(100) for XRP.  alice takes bob's offer.
            // Notice that although bob only offered USD(100), USD(125) was
            // removed from his account due to the gateway fee.
            //
            // A comparable payment would look like this:
            //   env (pay (bob, alice, USD(100)), sendmax(USD(125)))
            env (offer (bob, XRP(1), USD(100)));
            env.close();

            env (offer (alice, USD(100), XRP(1)));
            env.close();

            env.require (balance (alice, USD(100)));
            env.require (balance (alice, XRP( 99) + reserve(env, 2)));
            env.require (offers (alice, 0));

            env.require (balance (bob, USD(  0)));
            env.require (balance (bob, XRP(101) + reserve(env, 2)));
            env.require (offers (bob, 0));
        }
        {
            // Reverse the order, so the offer in the books is to sell XRP
            // in return for USD.  Gateway rate should still apply identically.
            auto const colin = Account("colin");
            auto const daria = Account("daria");
            env.fund (XRP(100) + reserve(env, 2) + (fee*2), colin, daria);
            env.close();

            env (trust(colin, USD(200)));
            env (trust(daria, USD(200)));
            env.close();

            env (pay (gw1, daria, USD(125)));
            env.close();

            env (offer (colin, USD(100), XRP(1)));
            env.close();

            env (offer (daria, XRP(1), USD(100)));
            env.close();

            env.require (balance (colin, USD(100)));
            env.require (balance (colin, XRP( 99) + reserve(env, 2)));
            env.require (offers (colin, 0));

            env.require (balance (daria, USD(  0)));
            env.require (balance (daria, XRP(101) + reserve(env, 2)));
            env.require (offers (daria, 0));
        }
        {
            auto const edith = Account("edith");
            auto const frank = Account("frank");

            env.fund (XRP(20000) + fee*2, edith, frank);
            env.close();

            env (trust (edith, USD(1000)));
            env (trust (frank, USD(1000)));
            env.close();

            env (pay (gw1, edith, USD(100)));
            env (pay (gw1, frank, USD(100)));
            env.close();

            // This test verifies that the amount removed from an offer
            // accounts for the transfer fee that is removed from the
            // account but not from the remaining offer.
            env (offer (edith, USD(10), XRP(4000)));
            env.close();
            std::uint32_t const edithOfferSeq = env.seq (edith) - 1;

            env (offer (frank, XRP(2000), USD(5)));
            env.close();

            env.require (balance (edith, USD(105)));
            env.require (balance (edith, XRP(18000)));
            auto const edithsOffers = offersOnAccount (env, edith);
            expect (edithsOffers.size() == 1);
            if (edithsOffers.size() != 0)
            {
                auto const& edithsOffer = *(edithsOffers.front());
                expect (edithsOffer[sfLedgerEntryType] == ltOFFER);
                expect (edithsOffer[sfTakerGets] == XRP (2000));
                expect (edithsOffer[sfTakerPays] == USD (5));
            }
            env (offer_cancel (edith, edithOfferSeq)); // For later tests

            env.require (balance (frank, USD(93.75)));
            env.require (balance (frank, XRP(22000)));
            env.require (offers (frank, 0));
        }
        // Start messing with two non-native currencies.
        auto const gw2   = Account("gateway2");
        auto const EUR = gw2["EUR"];

        env.fund (XRP(100000), gw2);
        env.close();

        env(rate(gw2, 1.5));
        {
            // Remove XRP from the equation.  Give the two currencies two
            // different transfer rates so we can see both transfer rates
            // apply in the same transaction.
            auto const grace = Account("grace");
            auto const henry = Account("henry");
            env.fund (reserve(env, 3) + (fee*3), grace, henry);
            env.close();

            env (trust(grace, USD(200)));
            env (trust(grace, EUR(200)));
            env (trust(henry, USD(200)));
            env (trust(henry, EUR(200)));
            env.close();

            env (pay (gw1, grace, USD(125)));
            env (pay (gw2, henry, EUR(150)));
            env.close();

            env (offer (grace, EUR(100), USD(100)));
            env.close();

            env (offer (henry, USD(100), EUR(100)));
            env.close();

            env.require (balance (grace, USD(  0)));
            env.require (balance (grace, EUR(100)));
            env.require (balance (grace, reserve(env, 3)));
            env.require (offers (grace, 0));

            env.require (balance (henry, USD(100)));
            env.require (balance (henry, EUR(  0)));
            env.require (balance (henry, reserve(env, 3)));
            env.require (offers (henry, 0));
        }
        {
            // Make sure things work right when we're auto-bridging as well.
            auto const irene = Account("irene");
            auto const jimmy = Account("jimmy");
            auto const karen = Account("karen");
            env.fund (XRP(2) + reserve(env, 3) + (fee*3), irene, jimmy, karen);
            env.close();

            //   o irene has USD but wants XPR.
            //   o jimmy has XRP but wants EUR.
            //   o karen has EUR but wants USD.
            env (trust(irene, USD(200)));
            env (trust(irene, EUR(200)));
            env (trust(jimmy, USD(200)));
            env (trust(jimmy, EUR(200)));
            env (trust(karen, USD(200)));
            env (trust(karen, EUR(200)));
            env.close();

            env (pay (gw1, irene, USD(125)));
            env (pay (gw2, karen, EUR(150)));
            env.close();

            env (offer (irene, XRP(2), USD(100)));
            env (offer (jimmy, EUR(100), XRP(2)));
            env.close();

            env (offer (karen, USD(100), EUR(100)));
            env.close();

            env.require (balance (irene, USD(  0)));
            env.require (balance (irene, EUR(  0)));
            env.require (balance (irene, XRP(4) + reserve(env, 3)));

            // In pre-flow code irene's offer is left empty in the ledger.
            auto const irenesOffers = offersOnAccount (env, irene);
            if (irenesOffers.size() != 0)
            {
                expect (irenesOffers.size() == 1);
                auto const& irenesOffer = *(irenesOffers.front());

                expect (irenesOffer[sfLedgerEntryType] == ltOFFER);
                expect (irenesOffer[sfTakerGets] == USD (0));
                expect (irenesOffer[sfTakerPays] == XRP (0));
            }

            env.require (balance (jimmy, USD(  0)));
            env.require (balance (jimmy, EUR(100)));
            env.require (balance (jimmy, XRP(0) + reserve(env, 3)));
            env.require (offers (jimmy, 0));

            env.require (balance (karen, USD(100)));
            env.require (balance (karen, EUR(  0)));
            env.require (balance (karen, XRP(2) + reserve(env, 3)));
            env.require (offers (karen, 0));
        }
    }

    void run ()
    {
        testCanceledOffer ();
        testRmFundedOffer ();
        testTinyPayment ();
        testXRPTinyPayment ();
        testEnforceNoRipple ();
        testInsufficientReserve ();
        testFillModes ();
        testMalformed ();
        testExpiration ();
        testPartialCross ();
        testUnfundedCross ();
        testXRPDirectCross ();
        testDirectCross();
        testBridgedCross();
        testSellOffer();
        testTransferRateOffer();
    }
};

BEAST_DEFINE_TESTSUITE (Offer, tx, ripple);

}  // test
}  // ripple
