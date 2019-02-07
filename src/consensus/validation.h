// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CONSENSUS_VALIDATION_H
#define BITCOIN_CONSENSUS_VALIDATION_H

#include <string>
#include <version.h>
#include <consensus/consensus.h>
#include <primitives/transaction.h>
#include <primitives/block.h>

/** "reject" message codes */
static const unsigned char REJECT_MALFORMED = 0x01;
static const unsigned char REJECT_INVALID = 0x10;
static const unsigned char REJECT_OBSOLETE = 0x11;
static const unsigned char REJECT_DUPLICATE = 0x12;
static const unsigned char REJECT_NONSTANDARD = 0x40;
// static const unsigned char REJECT_DUST = 0x41; // part of BIP 61
static const unsigned char REJECT_INSUFFICIENTFEE = 0x42;
static const unsigned char REJECT_CHECKPOINT = 0x43;

/** Capture information about block/transaction validation */
class CValidationState {
private:
    enum mode_state {
        MODE_VALID,   //!< everything ok
        MODE_INVALID, //!< network rule violation (DoS value may be set)
        MODE_ERROR,   //!< run-time error
    } mode;
    int nDoS;
    std::string strRejectReason;
    unsigned int chRejectCode;
    bool corruptionPossible;
    std::string strDebugMessage;
public:
    CValidationState() : mode(MODE_VALID), nDoS(0), chRejectCode(0), corruptionPossible(false) {}
    bool DoS(int level, bool ret = false,
             unsigned int chRejectCodeIn=0, const std::string &strRejectReasonIn="",
             bool corruptionIn=false,
             const std::string &strDebugMessageIn="") {
        chRejectCode = chRejectCodeIn;
        strRejectReason = strRejectReasonIn;
        corruptionPossible = corruptionIn;
        strDebugMessage = strDebugMessageIn;
        if (mode == MODE_ERROR)
            return ret;
        nDoS += level;
        mode = MODE_INVALID;
        return ret;
    }
    bool Invalid(bool ret = false,
                 unsigned int _chRejectCode=0, const std::string &_strRejectReason="",
                 const std::string &_strDebugMessage="") {
        return DoS(0, ret, _chRejectCode, _strRejectReason, false, _strDebugMessage);
    }
    bool Error(const std::string& strRejectReasonIn) {
        if (mode == MODE_VALID)
            strRejectReason = strRejectReasonIn;
        mode = MODE_ERROR;
        return false;
    }
    bool IsValid() const {
        return mode == MODE_VALID;
    }
    bool IsInvalid() const {
        return mode == MODE_INVALID;
    }
    bool IsError() const {
        return mode == MODE_ERROR;
    }
    bool IsInvalid(int &nDoSOut) const {
        if (IsInvalid()) {
            nDoSOut = nDoS;
            return true;
        }
        return false;
    }
    bool CorruptionPossible() const {
        return corruptionPossible;
    }
    void SetCorruptionPossible() {
        corruptionPossible = true;
    }
    unsigned int GetRejectCode() const { return chRejectCode; }
    std::string GetRejectReason() const { return strRejectReason; }
    std::string GetDebugMessage() const { return strDebugMessage; }
};

static inline int BlockWeightRulesVersion(int64_t block_time)
{
    // From 2019 Aug 1 through 2019 Dec 31, block weights are limited to 600kWU
    // Signatures are discounted on both Segwit and non-Segwit signatures
    if (block_time >= 1564617600 && block_time < 1577836800) {
        return 1;
    }
    return 0;
}

static inline size_t GetMaxAdjBlockWeight(int64_t block_time)
{
    if (BlockWeightRulesVersion(block_time) == 1) {
        return 600000;
    }
    return MAX_BLOCK_WEIGHT;
}

static inline int DiscountSerializationFlags(int64_t block_time)
{
    int discount_flags = SERIALIZE_TRANSACTION_NO_WITNESS;
    if (BlockWeightRulesVersion(block_time) > 0) {
        discount_flags |= SERIALIZE_TRANSACTION_NO_SIGNATURE;
    }
    return discount_flags;
}

// These implement the weight = (stripped_size * 4) + witness_size formula,
// using only serialization with and without witness data. As witness_size
// is equal to total_size - stripped_size, this formula is identical to:
// weight = (stripped_size * 3) + total_size.
static inline int64_t GetTransactionWeight(const CTransaction& tx, int64_t blocktime)
{
    return ::GetSerializeSize(tx, SER_NETWORK, PROTOCOL_VERSION | DiscountSerializationFlags(blocktime)) * (WITNESS_SCALE_FACTOR - 1) + ::GetSerializeSize(tx, SER_NETWORK, PROTOCOL_VERSION);
}
static inline int64_t GetBlockWeight(const CBlock& block, const bool discount_nonsegwit)
{
    int discount_flags;
    if (discount_nonsegwit) {
        discount_flags = DiscountSerializationFlags(block.nTime);
    } else {
        discount_flags = SERIALIZE_TRANSACTION_NO_WITNESS;
    }
    return ::GetSerializeSize(block, SER_NETWORK, PROTOCOL_VERSION | discount_flags) * (WITNESS_SCALE_FACTOR - 1) + ::GetSerializeSize(block, SER_NETWORK, PROTOCOL_VERSION);
}
static inline int64_t GetTransactionInputWeight(const CTxIn& txin)
{
    // scriptWitness size is added here because witnesses and txins are split up in segwit serialization.
    return ::GetSerializeSize(txin, SER_NETWORK, PROTOCOL_VERSION | SERIALIZE_TRANSACTION_NO_WITNESS) * (WITNESS_SCALE_FACTOR - 1) + ::GetSerializeSize(txin, SER_NETWORK, PROTOCOL_VERSION) + ::GetSerializeSize(txin.scriptWitness.stack, SER_NETWORK, PROTOCOL_VERSION);
}

#endif // BITCOIN_CONSENSUS_VALIDATION_H
