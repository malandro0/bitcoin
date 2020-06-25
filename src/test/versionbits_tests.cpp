// Copyright (c) 2014-2020 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chain.h>
#include <chainparams.h>
#include <consensus/params.h>
#include <test/util/setup_common.h>
#include <validation.h>
#include <versionbits.h>
#include <versionbitsinfo.h>

#include <boost/test/unit_test.hpp>

/* Define a virtual block time, one block per 10 minutes after Nov 14 2014, 0:55:36am */
static int32_t TestTime(int nHeight) { return 1415926536 + 600 * nHeight; }

static const std::string StateName(ThresholdState state)
{
    switch (state) {
    case ThresholdState::DEFINED:   return "DEFINED";
    case ThresholdState::STARTED:   return "STARTED";
    case ThresholdState::LOCKED_IN: return "LOCKED_IN";
    case ThresholdState::ACTIVE:    return "ACTIVE";
    case ThresholdState::FAILED:    return "FAILED";
    } // no default case, so the compiler can warn about missing cases
    return "";
}

static const Consensus::Params paramsDummy = Consensus::Params();

class TestConditionChecker : public AbstractThresholdConditionChecker
{
private:
    mutable ThresholdConditionCache cache;

public:
    int StartHeight(const Consensus::Params& params) const override { return 100; }
    int TimeoutHeight(const Consensus::Params& params) const override { return 200; }
    int Period(const Consensus::Params& params) const override { return 10; }
    int Threshold(const Consensus::Params& params) const override { return 9; }
    bool Condition(const CBlockIndex* pindex, const Consensus::Params& params) const override { return (pindex->nVersion & 0x100); }

    ThresholdState GetStateFor(const CBlockIndex* pindexPrev) const { return AbstractThresholdConditionChecker::GetStateFor(pindexPrev, paramsDummy, cache); }
    int GetStateSinceHeightFor(const CBlockIndex* pindexPrev) const { return AbstractThresholdConditionChecker::GetStateSinceHeightFor(pindexPrev, paramsDummy, cache); }
};

class TestDelayedActivationConditionChecker : public TestConditionChecker
{
public:
    int MinActivationHeight(const Consensus::Params& params) const override { return 250; }
};

class TestAlwaysActiveConditionChecker : public TestConditionChecker
{
public:
    int StartHeight(const Consensus::Params& params) const override { return Consensus::BIP9Deployment::ALWAYS_ACTIVE; }
};

class TestNeverActiveConditionChecker : public TestConditionChecker
{
public:
    int StartHeight(const Consensus::Params& params) const override { return Consensus::BIP9Deployment::NEVER_ACTIVE; }
};

#define CHECKERS 6

class VersionBitsTester
{
    // A fake blockchain
    std::vector<CBlockIndex*> vpblock;

    // 6 independent checkers for the same bit.
    // The first one performs all checks, the second only 50%, the third only 25%, etc...
    // This is to test whether lack of cached information leads to the same results.
    TestConditionChecker checker[CHECKERS];
    // Another 6 that assume delayed activation
    TestDelayedActivationConditionChecker checker_delayed[CHECKERS];
    // Another 6 that assume always active activation
    TestAlwaysActiveConditionChecker checker_always[CHECKERS];
    // Another 6 that assume never active activation
    TestNeverActiveConditionChecker checker_never[CHECKERS];

    // Test counter (to identify failures)
    int num;

public:
    VersionBitsTester() : num(1000) {}

    VersionBitsTester& Reset() {
        // Have each group of tests be counted by the 1000s part, starting at 1000
        num = num - (num % 1000) + 1000;

        for (unsigned int i = 0; i < vpblock.size(); i++) {
            delete vpblock[i];
        }
        for (unsigned int  i = 0; i < CHECKERS; i++) {
            checker[i] = TestConditionChecker();
            checker_delayed[i] = TestDelayedActivationConditionChecker();
            checker_always[i] = TestAlwaysActiveConditionChecker();
            checker_never[i] = TestNeverActiveConditionChecker();
        }
        vpblock.clear();
        return *this;
    }

    ~VersionBitsTester() {
         Reset();
    }

    VersionBitsTester& Mine(unsigned int height, int32_t nTime, int32_t nVersion) {
        while (vpblock.size() < height) {
            CBlockIndex* pindex = new CBlockIndex();
            pindex->nHeight = vpblock.size();
            pindex->pprev = Tip();
            pindex->nTime = nTime;
            pindex->nVersion = nVersion;
            pindex->BuildSkip();
            vpblock.push_back(pindex);
        }
        return *this;
    }

    VersionBitsTester& TestStateSinceHeight(int height)
    {
        return TestStateSinceHeight(height, height);
    }

    VersionBitsTester& TestStateSinceHeight(int height, int height_delayed)
    {
        const CBlockIndex* tip = Tip();
        for (int i = 0; i < CHECKERS; i++) {
            if (InsecureRandBits(i) == 0) {
                BOOST_CHECK_MESSAGE(checker[i].GetStateSinceHeightFor(tip) == height, strprintf("Test %i for StateSinceHeight", num));
                BOOST_CHECK_MESSAGE(checker_delayed[i].GetStateSinceHeightFor(tip) == height_delayed, strprintf("Test %i for StateSinceHeight (delayed)", num));
                BOOST_CHECK_MESSAGE(checker_always[i].GetStateSinceHeightFor(tip) == 0, strprintf("Test %i for StateSinceHeight (always active)", num));
                BOOST_CHECK_MESSAGE(checker_never[i].GetStateSinceHeightFor(tip) == 0, strprintf("Test %i for StateSinceHeight (never active)", num));
            }
        }
        num++;
        return *this;
    }

    VersionBitsTester& TestState(ThresholdState exp)
    {
        return TestState(exp, exp);
    }

    VersionBitsTester& TestState(ThresholdState exp, ThresholdState exp_delayed)
    {
        if (exp != exp_delayed) {
            // only expected differences are that delayed stays in locked_in longer
            BOOST_CHECK_EQUAL(exp, ThresholdState::ACTIVE);
            BOOST_CHECK_EQUAL(exp_delayed, ThresholdState::LOCKED_IN);
        }

        const CBlockIndex* pindex = Tip();
        for (int i = 0; i < CHECKERS; i++) {
            if (InsecureRandBits(i) == 0) {
                ThresholdState got = checker[i].GetStateFor(pindex);
                ThresholdState got_delayed = checker_delayed[i].GetStateFor(pindex);
                ThresholdState got_always = checker_always[i].GetStateFor(pindex);
                ThresholdState got_never = checker_never[i].GetStateFor(pindex);
                // nHeight of the next block. If vpblock is empty, the next (ie first)
                // block should be the genesis block with nHeight == 0.
                int height = pindex == nullptr ? 0 : pindex->nHeight + 1;
                BOOST_CHECK_MESSAGE(got == exp, strprintf("Test %i for %s height %d (got %s)", num, StateName(exp), height, StateName(got)));
                BOOST_CHECK_MESSAGE(got_delayed == exp_delayed, strprintf("Test %i for %s height %d (got %s; delayed case)", num, StateName(exp_delayed), height, StateName(got_delayed)));
                BOOST_CHECK_MESSAGE(got_always == ThresholdState::ACTIVE, strprintf("Test %i for ACTIVE height %d (got %s; always active case)", num, height, StateName(got_always)));
                BOOST_CHECK_MESSAGE(got_never == ThresholdState::FAILED, strprintf("Test %i for FAILED height %d (got %s; never active case)", num, height, StateName(got_never)));
            }
        }
        num++;
        return *this;
    }

    VersionBitsTester& TestDefined() { return TestState(ThresholdState::DEFINED); }
    VersionBitsTester& TestStarted() { return TestState(ThresholdState::STARTED); }
    VersionBitsTester& TestLockedIn() { return TestState(ThresholdState::LOCKED_IN); }
    VersionBitsTester& TestActive() { return TestState(ThresholdState::ACTIVE); }
    VersionBitsTester& TestFailed() { return TestState(ThresholdState::FAILED); }

    // non-delayed should be active; delayed should still be locked in
    VersionBitsTester& TestActiveDelayed() { return TestState(ThresholdState::ACTIVE, ThresholdState::LOCKED_IN); }

    CBlockIndex* Tip() { return vpblock.empty() ? nullptr : vpblock.back(); }
};

BOOST_FIXTURE_TEST_SUITE(versionbits_tests, TestingSetup)

BOOST_AUTO_TEST_CASE(versionbits_test)
{
    for (int i = 0; i < 64; i++) {
        // DEFINED -> STARTED -> FAILED
        VersionBitsTester().TestDefined().TestStateSinceHeight(0)
                           .Mine(1, TestTime(1), 0).TestDefined().TestStateSinceHeight(0)
                           .Mine(99, TestTime(10000) - 1, 0x100).TestDefined().TestStateSinceHeight(0) // One block more and it would be defined
                           .Mine(100, TestTime(10000), 0x100).TestStarted().TestStateSinceHeight(100) // So that's what happens the next period
                           .Mine(101, TestTime(10010), 0).TestStarted().TestStateSinceHeight(100) // 1 old block
                           .Mine(109, TestTime(10020), 0x100).TestStarted().TestStateSinceHeight(100) // 8 new blocks
                           .Mine(110, TestTime(10020), 0).TestStarted().TestStateSinceHeight(100) // 1 old block (so 8 out of the past 10 are new)
                           .Mine(151, TestTime(10020), 0).TestStarted().TestStateSinceHeight(100)
                           .Mine(200, TestTime(20000), 0).TestFailed().TestStateSinceHeight(200)
                           .Mine(300, TestTime(20010), 0x100).TestFailed().TestStateSinceHeight(200)

        // DEFINED -> STARTED -> LOCKEDIN before timeout -> ACTIVE
                           .Reset().TestDefined()
                           .Mine(1, TestTime(1), 0).TestDefined().TestStateSinceHeight(0)
                           .Mine(99, TestTime(10000) - 1, 0x101).TestDefined().TestStateSinceHeight(0) // One block more and it would be started
                           .Mine(100, TestTime(10000), 0x101).TestStarted().TestStateSinceHeight(100) // So that's what happens the next period
                           .Mine(109, TestTime(10020), 0x100).TestStarted().TestStateSinceHeight(100) // 9 new blocks
                           .Mine(110, TestTime(29999), 0x200).TestLockedIn().TestStateSinceHeight(110) // 1 old block (so 9 out of the past 10)
                           .Mine(119, TestTime(30001), 0).TestLockedIn().TestStateSinceHeight(110)
                           .Mine(120, TestTime(30002), 0).TestActiveDelayed().TestStateSinceHeight(120, 110) // delayed will not become active until height=250
                           .Mine(200, TestTime(30003), 0).TestActiveDelayed().TestStateSinceHeight(120, 110)
                           .Mine(250, TestTime(30004), 0).TestActive().TestStateSinceHeight(120, 250)
                           .Mine(300, TestTime(40000), 0).TestActive().TestStateSinceHeight(120, 250)

        // DEFINED multiple periods -> STARTED multiple periods -> FAILED
                           .Reset().TestDefined().TestStateSinceHeight(0)
                           .Mine(9, TestTime(999), 0).TestDefined().TestStateSinceHeight(0)
                           .Mine(10, TestTime(1000), 0).TestDefined().TestStateSinceHeight(0)
                           .Mine(20, TestTime(2000), 0).TestDefined().TestStateSinceHeight(0)
                           .Mine(100, TestTime(10000), 0).TestStarted().TestStateSinceHeight(100)
                           .Mine(103, TestTime(10000), 0).TestStarted().TestStateSinceHeight(100)
                           .Mine(105, TestTime(10000), 0).TestStarted().TestStateSinceHeight(100)
                           .Mine(199, TestTime(20000), 0).TestStarted().TestStateSinceHeight(100)
                           .Mine(200, TestTime(20000), 0).TestFailed().TestStateSinceHeight(200)
                           .Mine(300, TestTime(20000), 0x100).TestFailed().TestStateSinceHeight(200)
                           .Mine(800, TestTime(20000), 0x100).TestFailed().TestStateSinceHeight(200) // stay in FAILED no matter how much we signal
        ;
    }
}

BOOST_AUTO_TEST_CASE(versionbits_sanity)
{
    // Sanity checks of version bit deployments
    const auto chainParams = CreateChainParams(*m_node.args, CBaseChainParams::MAIN);
    const Consensus::Params &mainnetParams = chainParams->GetConsensus();
    for (int i=0; i<(int) Consensus::MAX_VERSION_BITS_DEPLOYMENTS; i++) {
        uint32_t bitmask = VersionBitsMask(mainnetParams, static_cast<Consensus::DeploymentPos>(i));
        // Make sure that no deployment tries to set an invalid bit.
        BOOST_CHECK_EQUAL(bitmask & ~(uint32_t)VERSIONBITS_TOP_MASK, bitmask);

        std::string error;
        BOOST_CHECK_MESSAGE(CheckVBitsHeights(error, mainnetParams, mainnetParams.vDeployments[i].startheight, mainnetParams.vDeployments[i].timeoutheight), error);

        // Check min_activation_height is on a retarget boundary
        BOOST_CHECK_EQUAL(mainnetParams.vDeployments[i].min_activation_height % mainnetParams.nMinerConfirmationWindow, 0U);
        // Check min_activation_height is 0 for ALWAYS_ACTIVE and never active deployments
        if (mainnetParams.vDeployments[i].startheight == Consensus::BIP9Deployment::ALWAYS_ACTIVE || mainnetParams.vDeployments[i].startheight == Consensus::BIP9Deployment::NEVER_ACTIVE) {
            BOOST_CHECK_EQUAL(mainnetParams.vDeployments[i].min_activation_height, 0);
        }

        // Verify that the deployment windows of different deployment using the
        // same bit are disjoint.
        // This test may need modification at such time as a new deployment
        // is proposed that reuses the bit of an activated soft fork, before the
        // end time of that soft fork.  (Alternatively, the end time of that
        // activated soft fork could be later changed to be earlier to avoid
        // overlap.)
        for (int j=i+1; j<(int) Consensus::MAX_VERSION_BITS_DEPLOYMENTS; j++) {
            if (VersionBitsMask(mainnetParams, static_cast<Consensus::DeploymentPos>(j)) == bitmask) {
                BOOST_CHECK(mainnetParams.vDeployments[j].startheight > mainnetParams.vDeployments[i].timeoutheight ||
                        mainnetParams.vDeployments[i].startheight > mainnetParams.vDeployments[j].timeoutheight);
            }
        }
    }
}

/** Check that ComputeBlockVersion will set the appropriate bit correctly */
static void check_computeblockversion(const Consensus::Params& params, Consensus::DeploymentPos dep)
{
    // This implicitly uses versionbitscache, so clear it every time
    versionbitscache.Clear();

    int64_t bit = params.vDeployments[dep].bit;
    int64_t startheight = params.vDeployments[dep].startheight;
    int64_t timeoutheight = params.vDeployments[dep].timeoutheight;
    int min_activation_height = params.vDeployments[dep].min_activation_height;

    // should not be any signalling for first block
    BOOST_CHECK_EQUAL(ComputeBlockVersion(nullptr, params), VERSIONBITS_TOP_BITS);

    // always/never active deployments shouldn't need to be tested further
    if (startheight == Consensus::BIP9Deployment::ALWAYS_ACTIVE) return;
    if (startheight == Consensus::BIP9Deployment::NEVER_ACTIVE) return;

    BOOST_REQUIRE(startheight < timeoutheight);
    BOOST_REQUIRE(startheight >= 0);
    BOOST_REQUIRE(timeoutheight <= std::numeric_limits<int>::max() || timeoutheight == Consensus::BIP9Deployment::NO_TIMEOUT);
    BOOST_REQUIRE(0 <= bit && bit < 32);
    BOOST_REQUIRE(((1 << bit) & VERSIONBITS_TOP_MASK) == 0);
    BOOST_REQUIRE(min_activation_height >= 0);
    BOOST_REQUIRE_EQUAL(min_activation_height % params.nMinerConfirmationWindow, 0U);

    // In the first chain, test that the bit is set by CBV until it has failed.
    // In the second chain, test the bit is set by CBV while STARTED and
    // LOCKED-IN, and then no longer set while ACTIVE.
    VersionBitsTester firstChain, secondChain;

    int64_t nTime = TestTime(startheight);

    const CBlockIndex *lastBlock = nullptr;

    if (startheight > 0) {
        // Start generating blocks before startheight
        // Before the chain has reached startheight, the bit should not be set.
        lastBlock = firstChain.Mine(startheight - 2, nTime, VERSIONBITS_LAST_OLD_BLOCK_VERSION).Tip();
        BOOST_CHECK_EQUAL(ComputeBlockVersion(lastBlock, params) & (1<<bit), 0);
    }

    // Advance to the next block and transition to STARTED,
    lastBlock = firstChain.Mine(startheight, nTime, VERSIONBITS_LAST_OLD_BLOCK_VERSION).Tip();
    // so ComputeBlockVersion should now set the bit,
    BOOST_CHECK((ComputeBlockVersion(lastBlock, params) & (1<<bit)) != 0);
    // and should also be using the VERSIONBITS_TOP_BITS.
    BOOST_CHECK_EQUAL(ComputeBlockVersion(lastBlock, params) & VERSIONBITS_TOP_MASK, VERSIONBITS_TOP_BITS);

    // Check that ComputeBlockVersion will set the bit until timeoutheight
    nTime += 600;
    uint32_t blocksToMine = params.nMinerConfirmationWindow * 2; // test blocks for up to 2 time periods
    uint32_t nHeight = params.nMinerConfirmationWindow * 3;
    // These blocks are all before timeoutheight is reached.
    while (nHeight < timeoutheight && blocksToMine > 0) {
        lastBlock = firstChain.Mine(nHeight+1, nTime, VERSIONBITS_LAST_OLD_BLOCK_VERSION).Tip();
        BOOST_CHECK((ComputeBlockVersion(lastBlock, params) & (1<<bit)) != 0);
        BOOST_CHECK_EQUAL(ComputeBlockVersion(lastBlock, params) & VERSIONBITS_TOP_MASK, VERSIONBITS_TOP_BITS);
        blocksToMine--;
        nTime += 600;
        nHeight += 1;
    }

    if (timeoutheight != Consensus::BIP9Deployment::NO_TIMEOUT) {
        // can reach any nTimeout other than NO_TIMEOUT due to earlier BOOST_REQUIRE

        // Check that ComputeBlockVersion will set the bit until timeoutheight
        // These blocks are all before timeoutheight is reached.
        lastBlock = firstChain.Mine(timeoutheight - 1, nTime, VERSIONBITS_LAST_OLD_BLOCK_VERSION).Tip();
        BOOST_CHECK((ComputeBlockVersion(lastBlock, params) & (1<<bit)) != 0);
        BOOST_CHECK_EQUAL(ComputeBlockVersion(lastBlock, params) & VERSIONBITS_TOP_MASK, VERSIONBITS_TOP_BITS);

        // The next block should trigger no longer setting the bit.
        lastBlock = firstChain.Mine(timeoutheight, nTime, VERSIONBITS_LAST_OLD_BLOCK_VERSION).Tip();
        BOOST_CHECK_EQUAL(ComputeBlockVersion(lastBlock, params) & (1<<bit), 0);
    }

    // On a new chain:
    // verify that the bit will be set after lock-in, and then stop being set
    // after activation.

    // Mine up until startheight-1, and check that the bit will be on for the
    // next period.
    lastBlock = secondChain.Mine(startheight, nTime, VERSIONBITS_LAST_OLD_BLOCK_VERSION).Tip();
    BOOST_CHECK((ComputeBlockVersion(lastBlock, params) & (1<<bit)) != 0);

    // Mine another block, signaling the new bit.
    lastBlock = secondChain.Mine(startheight + params.nMinerConfirmationWindow, nTime, VERSIONBITS_TOP_BITS | (1<<bit)).Tip();
    // After one period of setting the bit on each block, it should have locked in.
    // We keep setting the bit for one more period though, until activation.
    BOOST_CHECK((ComputeBlockVersion(lastBlock, params) & (1<<bit)) != 0);

    // Now check that we keep mining the block until the end of this period, and
    // then stop at the beginning of the next period.
    lastBlock = secondChain.Mine(startheight + (params.nMinerConfirmationWindow * 2) - 1, nTime, VERSIONBITS_LAST_OLD_BLOCK_VERSION).Tip();
    BOOST_CHECK((ComputeBlockVersion(lastBlock, params) & (1 << bit)) != 0);
    lastBlock = secondChain.Mine(startheight + (params.nMinerConfirmationWindow * 2), nTime, VERSIONBITS_LAST_OLD_BLOCK_VERSION).Tip();

    if (lastBlock->nHeight + 1 < min_activation_height) {
        // check signalling continues while min_activation_height is not reached
        lastBlock = secondChain.Mine(min_activation_height - 1, nTime, VERSIONBITS_LAST_OLD_BLOCK_VERSION).Tip();
        BOOST_CHECK((ComputeBlockVersion(lastBlock, params) & (1 << bit)) != 0);
        // then reach min_activation_height, which was already REQUIRE'd to start a new period
        lastBlock = secondChain.Mine(min_activation_height, nTime, VERSIONBITS_LAST_OLD_BLOCK_VERSION).Tip();
    }

    // Check that we don't signal after activation
    BOOST_CHECK_EQUAL(ComputeBlockVersion(lastBlock, params) & (1<<bit), 0);
}

BOOST_AUTO_TEST_CASE(versionbits_computeblockversion)
{
    // check that any deployment can conceivably reach both
    // ACTIVE and FAILED states in roughly the way we expect
    {
        const auto& chain_name{CBaseChainParams::REGTEST};
        ArgsManager args;
        const auto period = CreateChainParams(args, chain_name)->GetConsensus().nMinerConfirmationWindow;
        for (int i = 0; i < (int)Consensus::MAX_VERSION_BITS_DEPLOYMENTS; ++i) {
            args.ForceSetArg("-vbparams", strprintf("%s:@%s:@%s", VersionBitsDeploymentInfo[i].name, period, period * 3));
            const auto chainParams = CreateChainParams(args, chain_name);
            check_computeblockversion(chainParams->GetConsensus(), static_cast<Consensus::DeploymentPos>(i));
        }
    }

    {
        // Use regtest/testdummy to ensure we always exercise some
        // deployment that's not always/never active
        ArgsManager args;
        args.ForceSetArg("-vbparams", "testdummy:@144:@432");
        const auto chainParams = CreateChainParams(args, CBaseChainParams::REGTEST);
        check_computeblockversion(chainParams->GetConsensus(), Consensus::DEPLOYMENT_TESTDUMMY);
    }

    {
        // Use regtest/testdummy to ensure we always exercise the
        // min_activation_height test, even if we're not using that in a
        // live deployment
        ArgsManager args;
        args.ForceSetArg("-vbparams", "testdummy:@144:@432:864");
        const auto chainParams = CreateChainParams(args, CBaseChainParams::REGTEST);
        check_computeblockversion(chainParams->GetConsensus(), Consensus::DEPLOYMENT_TESTDUMMY);
    }
}

BOOST_AUTO_TEST_SUITE_END()
