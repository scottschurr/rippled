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

#include <ripple/basics/contract.h>
#include <ripple/protocol/Feature.h>
#include <ripple/protocol/digest.h>

#include <cstring>

namespace ripple {

//------------------------------------------------------------------------------

constexpr char const* const detail::FeatureCollections::featureNames[];

detail::FeatureCollections::FeatureCollections()
{
    features.reserve(numFeatures());
    featureToIndex.reserve(numFeatures());
    nameToFeature.reserve(numFeatures());

    for (std::size_t i = 0; i < numFeatures(); ++i)
    {
        auto const name = featureNames[i];
        sha512_half_hasher h;
        h(name, std::strlen(name));
        auto const f = static_cast<uint256>(h);

        features.push_back(f);
        featureToIndex[f] = i;
        nameToFeature[name] = f;
    }
}

boost::optional<uint256>
detail::FeatureCollections::getRegisteredFeature(std::string const& name) const
{
    auto const i = nameToFeature.find(name);
    if (i == nameToFeature.end())
        return boost::none;
    return i->second;
}

size_t
detail::FeatureCollections::featureToBitsetIndex(uint256 const& f) const
{
    auto const i = featureToIndex.find(f);
    if (i == featureToIndex.end())
        LogicError("Invalid Feature ID");
    return i->second;
}

uint256 const&
detail::FeatureCollections::bitsetIndexToFeature(size_t i) const
{
    if (i >= features.size())
        LogicError("Invalid FeatureBitset index");
    return features[i];
}

static detail::FeatureCollections const featureCollections;

/** Amendments that this server supports, but doesn't enable by default */
std::vector<std::string> const&
detail::supportedAmendments()
{
    // clang-format off
    // Commented out amendments will be supported in a future release (and
    // uncommented at that time).
    //
    // There are also unconditionally supported amendments in the list.
    // Those are amendments that were enabled some time ago and the
    // amendment conditional code has been removed.
    //
    // ** WARNING **
    // Unconditionally supported amendments need to remain in the list.
    // Removing them will cause servers to become amendment blocked.
    static std::vector<std::string> const supported{
        "MultiSign",            // Unconditionally supported.
        "TrustSetAuth",         // Unconditionally supported.
        "FeeEscalation",        // Unconditionally supported.
//      "OwnerPaysFee",
        "PayChan",              // Unconditionally supported.
        "Flow",                 // Unconditionally supported.
        "CryptoConditions",     // Unconditionally supported.
        "TickSize",             // Unconditionally supported.
        "fix1368",              // Unconditionally supported.
        "Escrow",               // Unconditionally supported.
        "CryptoConditionsSuite",
        "fix1373",              // Unconditionally supported.
        "EnforceInvariants",    // Unconditionally supported.
        "FlowCross",
        "SortedDirectories",    // Unconditionally supported.
        "fix1201",              // Unconditionally supported.
        "fix1512",              // Unconditionally supported.
        "fix1513",
        "fix1523",              // Unconditionally supported.
        "fix1528",              // Unconditionally supported.
        "DepositAuth",
        "Checks",
        "fix1571",
        "fix1543",
        "fix1623",
        "DepositPreauth",
        // Use liquidity from strands that consume max offers, but mark as dry
        "fix1515",
        "fix1578",
        "MultiSignReserve",
        "fixTakerDryOfferRemoval",
        "fixMasterKeyAsRegularKey",
        "fixCheckThreading",
        "fixPayChanRecipientOwnerDir",
        "DeletableAccounts",
        "fixQualityUpperBound",
        "RequireFullyCanonicalSig",
        "fix1781",
        "HardenedValidations",
        "fixAmendmentMajorityCalc",
        //"NegativeUNL",      // Commented out to prevent automatic enablement
        //"TicketBatch",      // Commented out to prevent automatic enablement
    };
    // clang-format on
    return supported;
}

//------------------------------------------------------------------------------

boost::optional<uint256>
getRegisteredFeature(std::string const& name)
{
    return featureCollections.getRegisteredFeature(name);
}

// Used for static initialization.  It's a LogicError if the named feature
// is missing.
static uint256
getMandatoryFeature(std::string const& name)
{
    boost::optional<uint256> const optFeatureId = getRegisteredFeature(name);
    if (!optFeatureId)
    {
        LogicError(
            std::string("Requested feature \"") + name +
            "\" is not registered in FeatureCollections::featureName.");
    }
    return *optFeatureId;
}

size_t
featureToBitsetIndex(uint256 const& f)
{
    return featureCollections.featureToBitsetIndex(f);
}

uint256
bitsetIndexToFeature(size_t i)
{
    return featureCollections.bitsetIndexToFeature(i);
}

// clang-format off
uint256 const
    featureOwnerPaysFee             = getMandatoryFeature("OwnerPaysFee"),
    featureFlow                     = getMandatoryFeature("Flow"),
    featureCompareTakerFlowCross    = getMandatoryFeature("CompareTakerFlowCross"),
    featureFlowCross                = getMandatoryFeature("FlowCross"),
    featureCryptoConditionsSuite    = getMandatoryFeature("CryptoConditionsSuite"),
    fix1513                         = getMandatoryFeature("fix1513"),
    featureDepositAuth              = getMandatoryFeature("DepositAuth"),
    featureChecks                   = getMandatoryFeature("Checks"),
    fix1571                         = getMandatoryFeature("fix1571"),
    fix1543                         = getMandatoryFeature("fix1543"),
    fix1623                         = getMandatoryFeature("fix1623"),
    featureDepositPreauth           = getMandatoryFeature("DepositPreauth"),
    fix1515                         = getMandatoryFeature("fix1515"),
    fix1578                         = getMandatoryFeature("fix1578"),
    featureMultiSignReserve         = getMandatoryFeature("MultiSignReserve"),
    fixTakerDryOfferRemoval         = getMandatoryFeature("fixTakerDryOfferRemoval"),
    fixMasterKeyAsRegularKey        = getMandatoryFeature("fixMasterKeyAsRegularKey"),
    fixCheckThreading               = getMandatoryFeature("fixCheckThreading"),
    fixPayChanRecipientOwnerDir     = getMandatoryFeature("fixPayChanRecipientOwnerDir"),
    featureDeletableAccounts        = getMandatoryFeature("DeletableAccounts"),
    fixQualityUpperBound            = getMandatoryFeature("fixQualityUpperBound"),
    featureRequireFullyCanonicalSig = getMandatoryFeature("RequireFullyCanonicalSig"),
    fix1781                         = getMandatoryFeature("fix1781"),
    featureHardenedValidations      = getMandatoryFeature("HardenedValidations"),
    fixAmendmentMajorityCalc        = getMandatoryFeature("fixAmendmentMajorityCalc"),
    featureNegativeUNL              = getMandatoryFeature("NegativeUNL"),
    featureTicketBatch              = getMandatoryFeature("TicketBatch");

// The following amendments have been active for at least two years. Their
// pre-amendment code has been removed and the identifiers are deprecated.
[[deprecated("The referenced amendment has been retired"), maybe_unused]]
static uint256 const
    retiredFeeEscalation     = getMandatoryFeature("FeeEscalation"),
    retiredMultiSign         = getMandatoryFeature("MultiSign"),
    retiredTrustSetAuth      = getMandatoryFeature("TrustSetAuth"),
    retiredFlow              = getMandatoryFeature("Flow"),
    retiredCryptoConditions  = getMandatoryFeature("CryptoConditions"),
    retiredTickSize          = getMandatoryFeature("TickSize"),
    retiredPayChan           = getMandatoryFeature("PayChan"),
    retiredFix1368           = getMandatoryFeature("fix1368"),
    retiredEscrow            = getMandatoryFeature("Escrow"),
    retiredFix1373           = getMandatoryFeature("fix1373"),
    retiredEnforceInvariants = getMandatoryFeature("EnforceInvariants"),
    retiredSortedDirectories = getMandatoryFeature("SortedDirectories"),
    retiredFix1201           = getMandatoryFeature("fix1201"),
    retiredFix1512           = getMandatoryFeature("fix1512"),
    retiredFix1523           = getMandatoryFeature("fix1523"),
    retiredFix1528           = getMandatoryFeature("fix1528");
// clang-format on

}  // namespace ripple
