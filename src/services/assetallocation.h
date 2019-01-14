// Copyright (c) 2017-2018 The Syscoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef ASSETALLOCATION_H
#define ASSETALLOCATION_H

#include "rpc/server.h"
#include "dbwrapper.h"
#include "primitives/transaction.h"
#include <unordered_map>
#include "services/graph.h"
class CTransaction;
class CReserveKey;
class CCoinsViewCache;
class CBlock;
class CAsset;

bool DecodeAssetAllocationTx(const CTransaction& tx, int& op, std::vector<std::vector<unsigned char> >& vvch);
bool DecodeAndParseAssetAllocationTx(const CTransaction& tx, int& op, std::vector<std::vector<unsigned char> >& vvch, char& type);
bool DecodeAssetAllocationScript(const CScript& script, int& op, std::vector<std::vector<unsigned char> > &vvch);
bool IsAssetAllocationOp(int op);
void AssetAllocationTxToJSON(const int op, const std::vector<unsigned char> &vchData, UniValue &entry);
void AssetMintTxToJson(const CTransaction& tx, UniValue &entry);
std::string assetAllocationFromOp(int op);
bool RemoveAssetAllocationScriptPrefix(const CScript& scriptIn, CScript& scriptOut);
class CWitnessAddress{
public:
    unsigned char nVersion;
    std::vector<unsigned char> vchWitnessProgram;
    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(nVersion);
        READWRITE(vchWitnessProgram);
    }
    CWitnessAddress(const unsigned char &version, const std::vector<unsigned char> &vchWitnessProgram_) {
        nVersion = version;
        vchWitnessProgram = vchWitnessProgram_;
    }
    CWitnessAddress() {
        SetNull();
    }
    inline CWitnessAddress operator=(const CWitnessAddress& other) {
        this->nVersion = other.nVersion;
        this->vchWitnessProgram = other.vchWitnessProgram;
        return *this;
    }
    inline bool operator==(const CWitnessAddress& other) const {
        return this->nVersion == other.nVersion && this->vchWitnessProgram == other.vchWitnessProgram;
    }
    inline bool operator!=(const CWitnessAddress& other) const {
        return (this->nVersion != other.nVersion || this->vchWitnessProgram != other.vchWitnessProgram);
    }
    inline bool operator< (const CWitnessAddress& right) const
    {
        return ToString() < right.ToString();
    }
    inline void SetNull() {
        nVersion = 0;
        vchWitnessProgram.clear();
    }
    bool IsValid() const;
    std::string ToString() const;
    inline bool IsNull() const {
        return (nVersion == 0 && vchWitnessProgram.empty());
    }
};
class CAssetAllocationTuple {
public:
	uint32_t nAsset;
	CWitnessAddress witnessAddress;
	ADD_SERIALIZE_METHODS;

	template <typename Stream, typename Operation>
	inline void SerializationOp(Stream& s, Operation ser_action) {
		READWRITE(nAsset);
		READWRITE(witnessAddress);
	}
	CAssetAllocationTuple(const uint32_t &asset, const CWitnessAddress &witnessAddress_) {
		nAsset = asset;
		witnessAddress = witnessAddress_;
	}
	CAssetAllocationTuple(const uint32_t &asset) {
		nAsset = asset;
		witnessAddress.SetNull();
	}
	CAssetAllocationTuple() {
		SetNull();
	}
	inline CAssetAllocationTuple operator=(const CAssetAllocationTuple& other) {
		this->nAsset = other.nAsset;
		this->witnessAddress = other.witnessAddress;
		return *this;
	}
	inline bool operator==(const CAssetAllocationTuple& other) const {
		return this->nAsset == other.nAsset && this->witnessAddress == other.witnessAddress;
	}
	inline bool operator!=(const CAssetAllocationTuple& other) const {
		return (this->nAsset != other.nAsset || this->witnessAddress != other.witnessAddress);
	}
	inline bool operator< (const CAssetAllocationTuple& right) const
	{
		return ToString() < right.ToString();
	}
	inline void SetNull() {
		nAsset = 0;
		witnessAddress.SetNull();
	}
	std::string ToString() const;
	inline bool IsNull() const {
		return (nAsset == 0 && witnessAddress.IsNull());
	}
};
typedef std::unordered_map<std::string, CAmount> AssetBalanceMap;
typedef std::unordered_map<uint256, int64_t,SaltedTxidHasher> ArrivalTimesMap;
typedef std::unordered_map<std::string, ArrivalTimesMap> ArrivalTimesMapImpl;
typedef std::vector<std::pair<CWitnessAddress, CAmount > > RangeAmountTuples;
typedef std::map<std::string, std::string> AssetAllocationIndexItem;
typedef std::map<int, AssetAllocationIndexItem> AssetAllocationIndexItemMap;
extern AssetAllocationIndexItemMap AssetAllocationIndex;
static const int ZDAG_MINIMUM_LATENCY_SECONDS = 10;
static const int MAX_MEMO_LENGTH = 128;
static const int ONE_YEAR_IN_BLOCKS = 525600;
static const int ONE_HOUR_IN_BLOCKS = 60;
static const int ONE_MONTH_IN_BLOCKS = 43800;
static sorted_vector<std::string> assetAllocationConflicts;
static CCriticalSection cs_assetallocation;
static CCriticalSection cs_assetallocationarrival;
static CCriticalSection cs_assetallocationindex;
enum {
	ZDAG_NOT_FOUND = -1,
	ZDAG_STATUS_OK = 0,
	ZDAG_MINOR_CONFLICT,
	ZDAG_MAJOR_CONFLICT
};

class CAssetAllocation {
public:
	CAssetAllocationTuple assetAllocationTuple;
	RangeAmountTuples listSendingAllocationAmounts;
	CAmount nBalance;
	template <typename Stream, typename Operation>
	inline void SerializationOp(Stream& s, Operation ser_action) {
		READWRITE(assetAllocationTuple);
		READWRITE(listSendingAllocationAmounts);
		READWRITE(nBalance);
	}
	CAssetAllocation() {
		SetNull();
	}
	CAssetAllocation(const CTransaction &tx) {
		SetNull();
		UnserializeFromTx(tx);
	}
	inline void ClearAssetAllocation()
	{
		assetAllocationTuple.SetNull();
		listSendingAllocationAmounts.clear();
	}
	ADD_SERIALIZE_METHODS;

	inline friend bool operator==(const CAssetAllocation &a, const CAssetAllocation &b) {
		return (a.assetAllocationTuple == b.assetAllocationTuple
			);
	}

	inline CAssetAllocation operator=(const CAssetAllocation &b) {
		assetAllocationTuple = b.assetAllocationTuple;
		listSendingAllocationAmounts = b.listSendingAllocationAmounts;
		nBalance = b.nBalance;
		return *this;
	}

	inline friend bool operator!=(const CAssetAllocation &a, const CAssetAllocation &b) {
		return !(a == b);
	}
	inline void SetNull() { nBalance = 0; listSendingAllocationAmounts.clear(); assetAllocationTuple.SetNull(); }
	inline bool IsNull() const { return (assetAllocationTuple.IsNull()); }
	bool UnserializeFromTx(const CTransaction &tx);
	bool UnserializeFromData(const std::vector<unsigned char> &vchData);
	void Serialize(std::vector<unsigned char>& vchData);
};
typedef std::unordered_map<std::string, CAssetAllocation> AssetAllocationMap;
class CAssetAllocationDB : public CDBWrapper {
public:
	CAssetAllocationDB(size_t nCacheSize, bool fMemory, bool fWipe) : CDBWrapper(GetDataDir() / "assetallocations", nCacheSize, fMemory, fWipe) {}
    
    bool ReadAssetAllocation(const CAssetAllocationTuple& assetAllocationTuple, CAssetAllocation& assetallocation) {
        return Read(assetAllocationTuple, assetallocation);
    }
    bool Flush(const AssetAllocationMap &mapAssetAllocations);
    bool Flush(const AssetAllocationMap &mapAssetAllocations,const AssetAllocationMap &mapEraseAssetAllocations);
	void WriteAssetAllocationIndex(const CAssetAllocation& assetAllocationTuple, const uint256& txHash, int nHeight, const CAsset& asset, const CAmount& nSenderBalance, const CAmount& nAmount, const CWitnessAddress& senderWitness);
	bool ScanAssetAllocations(const int count, const int from, const UniValue& oOptions, UniValue& oRes);
};
class CAssetAllocationTransactionsDB : public CDBWrapper {
public:
	CAssetAllocationTransactionsDB(size_t nCacheSize, bool fMemory, bool fWipe) : CDBWrapper(GetDataDir() / "assetallocationtransactions", nCacheSize, fMemory, fWipe) {
		ReadAssetAllocationWalletIndex(AssetAllocationIndex);
	}

	bool WriteAssetAllocationWalletIndex(const AssetAllocationIndexItemMap &valueMap) {
		return Write(std::string("assetallocationtxi"), valueMap, true);
	}
	bool ReadAssetAllocationWalletIndex(AssetAllocationIndexItemMap &valueMap) {
		return Read(std::string("assetallocationtxi"), valueMap);
	}
	bool ScanAssetAllocationIndex(const int count, const int from, const UniValue& oOptions, UniValue& oRes);
};
bool DisconnectAssetAllocation(const CTransaction &tx);
bool DisconnectMintAsset(const CTransaction &tx);
bool CheckAssetAllocationInputs(const CTransaction &tx, const CCoinsViewCache &inputs, int op, const std::vector<std::vector<unsigned char> > &vvchArgs, bool fJustCheck, int nHeight, AssetAllocationMap &mapAssetAllocations, AssetBalanceMap &blockMapAssetBalances, std::string &errorMessage, bool bSanityCheck = false, bool bMiner = false);
bool GetAssetAllocation(const CAssetAllocationTuple& assetAllocationTuple,CAssetAllocation& txPos);
bool BuildAssetAllocationJson(CAssetAllocation& assetallocation, const CAsset& asset, UniValue& oName);
bool BuildAssetAllocationIndexerJson(const CAssetAllocation& assetallocation, const CAsset& asset, const CAmount& nSenderBalance, const CAmount& nAmount, const std::string& strSender, const std::string& strReceiver, bool &isMine, UniValue& oAssetAllocation);
#endif // ASSETALLOCATION_H
