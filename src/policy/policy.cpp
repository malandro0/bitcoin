// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// NOTE: This file is intended to be customised by the end user, and includes only local node policy logic

#include "policy/policy.h"

#include "main.h"
#include "tinyformat.h"
#include "util.h"
#include "utilstrencodings.h"

#include <boost/foreach.hpp>

    /**
     * Check transaction inputs to mitigate two
     * potential denial-of-service attacks:
     * 
     * 1. scriptSigs with extra data stuffed into them,
     *    not consumed by scriptPubKey (or P2SH script)
     * 2. P2SH scripts with a crazy number of expensive
     *    CHECKSIG/CHECKMULTISIG operations
     *
     * Why bother? To avoid denial-of-service attacks; an attacker
     * can submit a standard HASH... OP_EQUAL transaction,
     * which will get accepted into blocks. The redemption
     * script can be anything; an attacker could use a very
     * expensive-to-check-upon-redemption script like:
     *   DUP CHECKSIG DROP ... repeated 100 times... OP_1
     *
     * Note this must assign whichType even if returning false, in case
     * IsStandardTx ignores the "scriptpubkey" rejection.
     */

bool IsStandard(const CScript& scriptPubKey, txnouttype& whichType, const bool witnessEnabled)
{
    std::vector<std::vector<unsigned char> > vSolutions;
    if (!Solver(scriptPubKey, whichType, vSolutions))
        return false;

    if (whichType == TX_MULTISIG)
    {
        unsigned char m = vSolutions.front()[0];
        unsigned char n = vSolutions.back()[0];
        // Support up to x-of-3 multisig txns as standard
        if (n < 1 || n > 3)
            return false;
        if (m < 1 || m > n)
            return false;
    } else if (whichType == TX_NULL_DATA &&
               (!fAcceptDatacarrier || scriptPubKey.size() > nMaxDatacarrierBytes))
          return false;

    else if (!witnessEnabled && (whichType == TX_WITNESS_V0_KEYHASH || whichType == TX_WITNESS_V0_SCRIPTHASH))
        return false;

    return whichType != TX_NONSTANDARD;
}

static inline bool IsStandardTx_Rejection_(std::string& reasonOut, const std::string& reason, const std::set<std::string>& setIgnoreRejects=std::set<std::string>()) {
    if (setIgnoreRejects.find(reason) != setIgnoreRejects.end())
        return false;

    reasonOut = reason;
    return true;
}

#define IsStandardTx_Rejection(reasonIn)  do {  \
    if (IsStandardTx_Rejection_(reason, reasonIn, setIgnoreRejects)) {  \
        return false;  \
    }  \
} while(0)

bool IsStandardTx(const CTransaction& tx, std::string& reason, const bool witnessEnabled, const std::set<std::string>& setIgnoreRejects)
{
    if (tx.nVersion > CTransaction::MAX_STANDARD_VERSION || tx.nVersion < 1) {
        IsStandardTx_Rejection("version");
    }

    if (setIgnoreRejects.find("tx-size") == setIgnoreRejects.end()) {
        // Extremely large transactions with lots of inputs can cost the network
        // almost as much to process as they cost the sender in fees, because
        // computing signature hashes is O(ninputs*txsize). Limiting transactions
        // to MAX_STANDARD_TX_SIZE mitigates CPU exhaustion attacks.
        unsigned int sz = GetTransactionWeight(tx);
        if (sz >= MAX_STANDARD_TX_WEIGHT) {
            reason = "tx-weight";
            return false;
        }
    }

    bool fCheckPushOnly = (setIgnoreRejects.find("scriptsig-not-pushonly") == setIgnoreRejects.end());
    if (setIgnoreRejects.find("scriptsig-size") == setIgnoreRejects.end() || fCheckPushOnly) {
        BOOST_FOREACH(const CTxIn& txin, tx.vin)
        {
            // Biggest 'standard' txin is a 15-of-15 P2SH multisig with compressed
            // keys (remember the 520 byte limit on redeemScript size). That works
            // out to a (15*(33+1))+3=513 byte redeemScript, 513+1+15*(73+1)+3=1627
            // bytes of scriptSig, which we round off to 1650 bytes for some minor
            // future-proofing. That's also enough to spend a 20-of-20
            // CHECKMULTISIG scriptPubKey, though such a scriptPubKey is not
            // considered standard.
            if (txin.scriptSig.size() > 1650) {
                IsStandardTx_Rejection("scriptsig-size");
            }
            if (fCheckPushOnly && !txin.scriptSig.IsPushOnly()) {
                reason = "scriptsig-not-pushonly";
                return false;
            }
        }
    }

    if (setIgnoreRejects.find("scriptpubkey") == setIgnoreRejects.end() || setIgnoreRejects.find("bare-multisig") == setIgnoreRejects.end() || setIgnoreRejects.find("dust") == setIgnoreRejects.end() || setIgnoreRejects.find("multi-op-return") == setIgnoreRejects.end()) {
        unsigned int nDataOut = 0;
        txnouttype whichType;
        BOOST_FOREACH(const CTxOut& txout, tx.vout) {
            if (!::IsStandard(txout.scriptPubKey, whichType, witnessEnabled)) {
                IsStandardTx_Rejection("scriptpubkey");
            }

            if (whichType == TX_NULL_DATA)
                nDataOut++;
            else {
                if ((whichType == TX_MULTISIG) && (!fIsBareMultisigStd)) {
                    IsStandardTx_Rejection("bare-multisig");
                }
                if (txout.IsDust(::minRelayTxFee)) {
                    IsStandardTx_Rejection("dust");
                }
            }
        }

        // only one OP_RETURN txout is permitted
        if (nDataOut > 1) {
            IsStandardTx_Rejection("multi-op-return");
        }
    }

    return true;
}

bool AreInputsStandard(const CTransaction& tx, const CCoinsViewCache& mapInputs, std::string& reason, const std::set<std::string>& setIgnoreRejects)
{
    if (tx.IsCoinBase())
        return true; // Coinbases don't use vin normally

    for (unsigned int i = 0; i < tx.vin.size(); i++)
    {
        const CTxOut& prev = mapInputs.GetOutputFor(tx.vin[i]);

        std::vector<std::vector<unsigned char> > vSolutions;
        txnouttype whichType;
        // get the scriptPubKey corresponding to this input:
        const CScript& prevScript = prev.scriptPubKey;
        if (!Solver(prevScript, whichType, vSolutions)) {
            IsStandardTx_Rejection("script-unknown");
        }

        if (whichType == TX_SCRIPTHASH)
        {
            if (!tx.vin[i].scriptSig.IsPushOnly()) {
                // The only way we got this far, is if the user ignored scriptsig-not-pushonly.
                // However, this case is invalid, and will be caught later on.
                // But for now, we don't want to run the [possibly expensive] script here.
                continue;
            }
            std::vector<std::vector<unsigned char> > stack;
            // convert the scriptSig into a stack, so we can inspect the redeemScript
            if (!EvalScript(stack, tx.vin[i].scriptSig, SCRIPT_VERIFY_NONE, BaseSignatureChecker(), SIGVERSION_BASE)) {
                // This case is also invalid or a bug
                reason = "scriptsig-failure";
                return false;
            }
            if (stack.empty()) {
                // Also invalid
                reason = "scriptcheck-missing";
                return false;
            }
            CScript subscript(stack.back().begin(), stack.back().end());
            if (subscript.GetSigOpCount(true) > MAX_P2SH_SIGOPS) {
                IsStandardTx_Rejection("scriptcheck-sigops");
            }
        }
    }

    return true;
}

unsigned int nBytesPerSigOp = DEFAULT_BYTES_PER_SIGOP;

int64_t GetVirtualTransactionSize(int64_t nWeight, int64_t nSigOpCost)
{
    return (std::max(nWeight, nSigOpCost * nBytesPerSigOp) + WITNESS_SCALE_FACTOR - 1) / WITNESS_SCALE_FACTOR;
}

int64_t GetVirtualTransactionSize(const CTransaction& tx, int64_t nSigOpCost)
{
    return GetVirtualTransactionSize(GetTransactionWeight(tx), nSigOpCost);
}
