#pragma once
// Copyright (c) 2014-2017 The Dash Core developers
// Copyright (c) 2018-2022 The Pastel Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <string>
#include <map>
#include <vector>
#include <memory>

#include <key.h>
#include <consensus/validation.h>
#include <arith_uint256.h>
#include <timedata.h>
#include <net.h>

class CMasternode;
class CMasternodeBroadcast;

//
// The Masternode Ping Class : Contains a different serialize method for sending pings from masternodes throughout the network
//

class CMasternodePing
{
public:
    CTxIn vin;
    uint256 blockHash;
    int64_t sigTime{0}; //mnp message times
    v_uint8 vchSig;

    CMasternodePing() = default;
    CMasternodePing(const COutPoint& outpoint);

    ADD_SERIALIZE_METHODS;

    template <typename Stream>
    inline void SerializationOp(Stream& s, const SERIALIZE_ACTION ser_action)
    {
        READWRITE(vin);
        READWRITE(blockHash);
        READWRITE(sigTime);
        READWRITE(vchSig);
    }

    uint256 GetHash() const
    {
        CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
        ss << vin;
        ss << sigTime;
        return ss.GetHash();
    }
    std::string GetDesc() const noexcept { return vin.prevout.ToStringShort(); }

    bool IsExpired() const;

    bool Sign(const CKey& keyMasternode, const CPubKey& pubKeyMasternode);
    bool CheckSignature(CPubKey& pubKeyMasternode, int &nDos);
    bool SimpleCheck(int& nDos);
    bool CheckAndUpdate(CMasternode* pmn, bool fFromNewBroadcast, int& nDos);
    void Relay();
};

inline bool operator==(const CMasternodePing& a, const CMasternodePing& b)
{
    return a.vin == b.vin && a.blockHash == b.blockHash;
}
inline bool operator!=(const CMasternodePing& a, const CMasternodePing& b)
{
    return !(a == b);
}

enum class MASTERNODE_STATE : int
{
    PRE_ENABLED = 0,
    ENABLED,
    EXPIRED,
    OUTPOINT_SPENT,
    UPDATE_REQUIRED,
    WATCHDOG_EXPIRED,
    NEW_START_REQUIRED,
    POSE_BAN,

    COUNT
};

typedef struct _MNStateInfo
{
    const MASTERNODE_STATE state;
    const char* szState;
} MNStateInfo;

std::string MasternodeStateToString(const MASTERNODE_STATE state) noexcept;

class masternode_info_t
{
public:
    masternode_info_t() = default;
    masternode_info_t(const MASTERNODE_STATE activeState, int protoVer, int64_t sTime) :
        m_ActiveState{activeState},
        nProtocolVersion{protoVer},
        sigTime{sTime}
    {}

    masternode_info_t(const MASTERNODE_STATE activeState, int protoVer, int64_t sTime,
                      COutPoint const& outpoint, CService const& addr,
                      CPubKey const& pkCollAddr, CPubKey const& pkMN,
                      const std::string& extAddress, const std::string& extP2P,
                      const std::string& extKey, const std::string& extCfg,
                      int64_t tWatchdogV = 0) :
        m_ActiveState{activeState}, nProtocolVersion{protoVer}, sigTime{sTime},
        vin{outpoint}, addr{addr},
        pubKeyCollateralAddress{pkCollAddr}, pubKeyMasternode{pkMN},
        strExtraLayerAddress{extAddress}, strExtraLayerP2P{extP2P}, strExtraLayerKey{extKey}, strExtraLayerCfg{extCfg},
        nTimeLastWatchdogVote{tWatchdogV} {}

    MASTERNODE_STATE GetActiveState() const noexcept { return m_ActiveState; }
    std::string GetStateString() const noexcept { return MasternodeStateToString(m_ActiveState); }
    std::string GetDesc() const noexcept { return vin.prevout.ToStringShort(); }

    // set new MasterNode's state
    void SetState(const MASTERNODE_STATE newState, const char *szMethodName = nullptr);

    bool IsEnabled() const noexcept { return m_ActiveState == MASTERNODE_STATE::ENABLED; }
    bool IsPreEnabled() const noexcept { return m_ActiveState == MASTERNODE_STATE::PRE_ENABLED; }
    bool IsPoSeBanned() const noexcept { return m_ActiveState == MASTERNODE_STATE::POSE_BAN; }
    bool IsExpired() const noexcept { return m_ActiveState == MASTERNODE_STATE::EXPIRED; }
    bool IsOutpointSpent() const noexcept { return m_ActiveState == MASTERNODE_STATE::OUTPOINT_SPENT; }
    bool IsUpdateRequired() const noexcept { return m_ActiveState == MASTERNODE_STATE::UPDATE_REQUIRED; }
    bool IsWatchdogExpired() const noexcept { return m_ActiveState == MASTERNODE_STATE::WATCHDOG_EXPIRED; }
    bool IsNewStartRequired() const noexcept { return m_ActiveState == MASTERNODE_STATE::NEW_START_REQUIRED; }

    int nProtocolVersion = 0;
    int64_t sigTime = 0; //mnb message time

    CTxIn vin{};
    CService addr{};
    CPubKey pubKeyCollateralAddress{};
    CPubKey pubKeyMasternode{};

    std::string strExtraLayerAddress;
    std::string strExtraLayerKey;
    std::string strExtraLayerCfg;
    std::string strExtraLayerP2P;

    int64_t nTimeLastWatchdogVote = 0;

    int64_t nTimeLastChecked = 0;
    int64_t nTimeLastPaid = 0;
    int64_t nTimeLastPing = 0; //* not in CMN
    bool fInfoValid = false; //* not in CMN

protected:
    MASTERNODE_STATE m_ActiveState = MASTERNODE_STATE::PRE_ENABLED;
};

//
// The Masternode Class
// it's the one who own that ip address and code for calculating the payment election.
//
class CMasternode : public masternode_info_t
{
private:
    // critical section to protect the inner data structures
    mutable CCriticalSection cs;

public:
    enum CollateralStatus
    {
        COLLATERAL_OK,
        COLLATERAL_UTXO_NOT_FOUND,
        COLLATERAL_INVALID_AMOUNT
    };

    CMasternodePing lastPing{};
    v_uint8 vchSig;

    uint256 nCollateralMinConfBlockHash;
    int nBlockLastPaid{};
    int nPoSeBanScore{};
    int nPoSeBanHeight{};
    bool fUnitTest = false;
    const CChainParams& m_chainparams;

    CAmount aMNFeePerMB = 0; // 0 means default (masterNodeCtrl.MasternodeFeePerMBDefault)
    CAmount aNFTTicketFeePerKB = 0; // 0 means default (masterNodeCtrl.NFTTicketFeePerKBDefault)

    CMasternode();
    CMasternode(const CMasternode& other);
    CMasternode(const CMasternodeBroadcast& mnb);
    CMasternode(CService addrNew, COutPoint outpointNew, CPubKey pubKeyCollateralAddressNew, CPubKey pubKeyMasternodeNew, 
                const std::string& strExtraLayerAddress, const std::string& strExtraLayerP2P, const std::string& strExtraLayerKey, const std::string& strExtraLayerCfg,
                int nProtocolVersionIn);

    ADD_SERIALIZE_METHODS;

    template <typename Stream>
    inline void SerializationOp(Stream& s, const SERIALIZE_ACTION ser_action)
    {
        LOCK(cs);
        READWRITE(vin);
        READWRITE(addr);
        READWRITE(pubKeyCollateralAddress);
        READWRITE(pubKeyMasternode);
        READWRITE(lastPing);
        READWRITE(vchSig);
        READWRITE(sigTime);
        READWRITE(nTimeLastChecked);
        READWRITE(nTimeLastPaid);
        READWRITE(nTimeLastWatchdogVote);
        int nActiveState = to_integral_type<MASTERNODE_STATE>(GetActiveState());
        READWRITE(nActiveState);
        if (ser_action == SERIALIZE_ACTION::Read)
        {
            if (!is_enum_valid<MASTERNODE_STATE>(nActiveState, MASTERNODE_STATE::PRE_ENABLED, MASTERNODE_STATE::POSE_BAN))
                throw std::runtime_error(strprintf("Not supported MasterNode's state [%d]", nActiveState));
            SetState(static_cast<MASTERNODE_STATE>(nActiveState));
        }
        READWRITE(nCollateralMinConfBlockHash);
        READWRITE(nBlockLastPaid);
        READWRITE(nProtocolVersion);
        READWRITE(nPoSeBanScore);
        READWRITE(nPoSeBanHeight);
        READWRITE(fUnitTest);
        READWRITE(strExtraLayerKey);
        READWRITE(strExtraLayerAddress);
        READWRITE(strExtraLayerCfg);
        READWRITE(aMNFeePerMB);
        READWRITE(aNFTTicketFeePerKB);
        
        //For backward compatibility
        try
        {
            READWRITE(strExtraLayerP2P);
        }
        catch ([[maybe_unused]] const std::ios_base::failure& e)
        {
            LogPrintf("CMasternode: missing extP2P!\n");
        }
    }

    // CALCULATE A RANK AGAINST OF GIVEN BLOCK
    arith_uint256 CalculateScore(const uint256& blockHash);

    bool UpdateFromNewBroadcast(CMasternodeBroadcast& mnb);

    static CollateralStatus CheckCollateral(const COutPoint& outpoint);
    static CollateralStatus CheckCollateral(const COutPoint& outpoint, int& nHeightRet);
    void Check(const bool fForce = false);

    bool IsBroadcastedWithin(int nSeconds) const noexcept { return GetAdjustedTime() - sigTime < nSeconds; }

    bool IsPingedWithin(const int nSeconds, int64_t nTimeToCheckAt = -1) const noexcept
    {
        if (lastPing == CMasternodePing())
            return false;
        if(nTimeToCheckAt == -1)
            nTimeToCheckAt = GetAdjustedTime();
        return nTimeToCheckAt - lastPing.sigTime < nSeconds;
    }

    // NOTE: this one relies on nPoSeBanScore, not on nActiveState as everything else here
    bool IsPoSeVerified();

    static bool IsValidStateForAutoStart(const MASTERNODE_STATE state) noexcept
    {
        return  state == MASTERNODE_STATE::ENABLED ||
                state == MASTERNODE_STATE::PRE_ENABLED ||
                state == MASTERNODE_STATE::EXPIRED ||
                state == MASTERNODE_STATE::WATCHDOG_EXPIRED;
    }

    bool IsValidForPayment() const noexcept
    {
        if (IsEnabled())
            return true;
        return false;
    }

    /// Is the input associated with collateral public key? (and there is 1000 PASTEL - checking if valid masternode)
    bool IsInputAssociatedWithPubkey();

    bool IsValidNetAddr();
    static bool IsValidNetAddr(CService addrIn);

    void IncreasePoSeBanScore();
    void DecreasePoSeBanScore();
    void PoSeBan();

    masternode_info_t GetInfo() const noexcept;

    std::string GetStatus() const;

    int64_t GetLastPaidTime() const noexcept { return nTimeLastPaid; }
    int GetLastPaidBlock() const noexcept { return nBlockLastPaid; }
    void UpdateLastPaid(const CBlockIndex *pindex, int nMaxBlocksToScanBack);
    
    bool VerifyCollateral(CollateralStatus& collateralStatus);
    
    void UpdateWatchdogVoteTime(const uint64_t nVoteTime = 0);

    CMasternode& operator=(CMasternode const& from)
    {
        static_cast<masternode_info_t&>(*this)=from;
        lastPing = from.lastPing;
        vchSig = from.vchSig;
        nCollateralMinConfBlockHash = from.nCollateralMinConfBlockHash;
        nBlockLastPaid = from.nBlockLastPaid;
        nPoSeBanScore = from.nPoSeBanScore;
        nPoSeBanHeight = from.nPoSeBanHeight;
        fUnitTest = from.fUnitTest;
        aMNFeePerMB = from.aMNFeePerMB;
        aNFTTicketFeePerKB = from.aNFTTicketFeePerKB;
        return *this;
    }
};

inline bool operator==(const CMasternode& a, const CMasternode& b)
{
    return a.vin == b.vin;
}
inline bool operator!=(const CMasternode& a, const CMasternode& b)
{
    return !(a.vin == b.vin);
}

//
// The Masternode Broadcast Class : Contains a different serialize method for sending masternodes through the network
//

class CMasternodeBroadcast : public CMasternode
{
public:

    bool fRecovery;

    CMasternodeBroadcast() :
        fRecovery(false)
    {}
    CMasternodeBroadcast(const CMasternode& mn) :
        CMasternode(mn),
        fRecovery(false)
    {}
    CMasternodeBroadcast(CService addrNew, COutPoint outpointNew, CPubKey pubKeyCollateralAddressNew, CPubKey pubKeyMasternodeNew, 
                            const std::string& strExtraLayerAddress, const std::string& strExtraLayerP2P, const std::string& strExtraLayerKey, const std::string& strExtraLayerCfg,
                            int nProtocolVersionIn) :
        CMasternode(addrNew, outpointNew, pubKeyCollateralAddressNew, pubKeyMasternodeNew, 
                    strExtraLayerAddress, strExtraLayerP2P, strExtraLayerKey, strExtraLayerCfg,
                    nProtocolVersionIn),
        fRecovery(false)
    {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream>
    inline void SerializationOp(Stream& s, const SERIALIZE_ACTION ser_action)
    {
        READWRITE(vin);
        READWRITE(addr);
        READWRITE(pubKeyCollateralAddress);
        READWRITE(pubKeyMasternode);
        READWRITE(vchSig);
        READWRITE(sigTime);
        READWRITE(nProtocolVersion);
        READWRITE(lastPing);
        READWRITE(strExtraLayerKey);
        READWRITE(strExtraLayerAddress);
        READWRITE(strExtraLayerCfg);

        //For backward compatibility
        try
        {
            READWRITE(strExtraLayerP2P);
        }
        catch ([[maybe_unused]] const std::ios_base::failure& e)
        {
            LogPrintf("CMasternodeBroadcast: missing extP2P!\n");
        }
    }

    uint256 GetHash() const
    {
        CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
        ss << vin;
        ss << pubKeyCollateralAddress;
        ss << sigTime;
        return ss.GetHash();
    }

    /// Create Masternode broadcast, needs to be relayed manually after that
    static bool Create(const COutPoint& outpoint, 
                        const CService& service, 
                        const CKey& keyCollateralAddressNew, const CPubKey& pubKeyCollateralAddressNew, 
                        const CKey& keyMasternodeNew, const CPubKey& pubKeyMasternodeNew, 
                        const std::string& strExtraLayerAddress, const std::string& strExtraLayerP2P, 
                        const std::string& strExtraLayerKey, const std::string& strExtraLayerCfg,
                        std::string &strErrorRet, CMasternodeBroadcast &mnbRet);
    static bool Create(
        const std::string &strService,
        const std::string &strKey,
        const std::string &strTxHash,
        const std::string &strOutputIndex, 
        const std::string &strExtraLayerAddress,
        const std::string &strExtraLayerP2P,
        const std::string &strExtraLayerKey,
        const std::string &strExtraLayerCfg,
        std::string& strErrorRet, CMasternodeBroadcast &mnbRet, bool fOffline = false);

    bool SimpleCheck(int& nDos);
    bool Update(CMasternode* pmn, int& nDos);
    bool CheckOutpoint(int& nDos);

    bool Sign(const CKey& keyCollateralAddress);
    bool CheckSignature(int& nDos);
    void Relay();
};

class CMasternodeVerification
{
public:
    CTxIn vin1{};
    CTxIn vin2{};
    CService addr{};
    int nonce{};
    uint32_t nBlockHeight{};
    v_uint8 vchSig1{};
    v_uint8 vchSig2{};

    CMasternodeVerification() = default;

    CMasternodeVerification(CService addr, int nonce, uint32_t nBlockHeight) :
        addr(addr),
        nonce(nonce),
        nBlockHeight(nBlockHeight)
    {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream>
    inline void SerializationOp(Stream& s, const SERIALIZE_ACTION ser_action)
    {
        READWRITE(vin1);
        READWRITE(vin2);
        READWRITE(addr);
        READWRITE(nonce);
        READWRITE(nBlockHeight);
        READWRITE(vchSig1);
        READWRITE(vchSig2);
    }

    uint256 GetHash() const
    {
        CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
        ss << vin1;
        ss << vin2;
        ss << addr;
        ss << nonce;
        ss << nBlockHeight;
        return ss.GetHash();
    }

    void Relay() const;
};
