// Copyright (c) 2017-2018 The Syscoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef ASSET_H
#define ASSET_H

#include "rpc/server.h"
#include "dbwrapper.h"
#include "script/script.h"
#include "script/standard.h"
#include "serialize.h"
#include "primitives/transaction.h"
#include "services/assetallocation.h"

class CTransaction;
class CReserveKey;
class CCoinsViewCache;
class CBlock;
struct CRecipient;
class COutPoint;
class UniValue;
class CTxOut;
class CWallet;
const int SYSCOIN_TX_VERSION_ASSET = 0x7401;
const int SYSCOIN_TX_VERSION_MINT = 0x7402;
static const unsigned int MAX_GUID_LENGTH = 20;
static const unsigned int MAX_NAME_LENGTH = 256;
static const unsigned int MAX_VALUE_LENGTH = 512;
static const unsigned int MAX_SYMBOL_LENGTH = 8;
static const unsigned int MIN_SYMBOL_LENGTH = 1;
static const uint64_t ONE_YEAR_IN_SECONDS = 31536000;
std::string stringFromVch(const std::vector<unsigned char> &vch);
std::vector<unsigned char> vchFromValue(const UniValue& value);
std::vector<unsigned char> vchFromString(const std::string &str);
std::vector<uint8_t> vchFromStringUint8(const std::string &str);
std::string stringFromVchUint8(const std::vector<uint8_t> &vch);
std::string stringFromValue(const UniValue& value);
void CreateRecipient(const CScript& scriptPubKey, CRecipient& recipient);
void CreateAssetRecipient(const CScript& scriptPubKey, CRecipient& recipient);
void CreateFeeRecipient(CScript& scriptPubKey, const std::vector<unsigned char>& data, CRecipient& recipient);
unsigned int addressunspent(const std::string& strAddressFrom, COutPoint& outpoint);
int GetSyscoinDataOutput(const CTransaction& tx);
bool IsSyscoinDataOutput(const CTxOut& out);
bool DecodeAndParseSyscoinTx(const CTransaction& tx, int& op, std::vector<std::vector<unsigned char> >& vvch, char &type);
bool GetSyscoinData(const CTransaction &tx, std::vector<unsigned char> &vchData, std::vector<unsigned char> &vchHash, int& nOut, int &op);
bool GetSyscoinData(const CScript &scriptPubKey, std::vector<unsigned char> &vchData, std::vector<unsigned char> &vchHash, int &op);
void SysTxToJSON(const int op, const std::vector<unsigned char> &vchData, const std::vector<unsigned char> &vchHash, UniValue &entry, const char& type);
std::string GetSyscoinTransactionDescription(const CTransaction& tx, const int op, std::string& responseEnglish, const char &type, std::string& responseGUID);
bool IsOutpointMature(const COutPoint& outpoint);
UniValue syscointxfund_helper(const std::string &vchWitness, std::vector<CRecipient> &vecSend);
bool FlushSyscoinDBs();
bool FindAssetOwnerInTx(const CCoinsViewCache &inputs, const CTransaction& tx, const std::string& ownerAddressToMatch);
CWallet* GetDefaultWallet();
CAmount GetFee(const size_t nBytes);
bool CheckAssetInputs(const CTransaction &tx, const CCoinsViewCache &inputs, int op, const std::vector<std::vector<unsigned char> > &vvchArgs, bool fJustCheck, int nHeight, sorted_vector<CAssetAllocationTuple> &revertedAssetAllocations, std::string &errorMessage, bool bSanityCheck=false);
bool DecodeAssetTx(const CTransaction& tx, int& op, std::vector<std::vector<unsigned char> >& vvch);
bool DecodeAndParseAssetTx(const CTransaction& tx, int& op, std::vector<std::vector<unsigned char> >& vvch, char& type);
bool DecodeAssetScript(const CScript& script, int& op, std::vector<std::vector<unsigned char> > &vvch);
bool IsAssetOp(int op);
std::vector<unsigned char> GenerateSyscoinGuid();
bool IsSyscoinScript(const CScript& scriptPubKey, int &op, std::vector<std::vector<unsigned char> > &vvchArgs);
bool RemoveSyscoinScript(const CScript& scriptPubKeyIn, CScript& scriptPubKeyOut);
void AssetTxToJSON(const int op, const std::vector<unsigned char> &vchData, const std::vector<unsigned char> &vchHash, UniValue &entry);
std::string assetFromOp(int op);
bool RemoveAssetScriptPrefix(const CScript& scriptIn, CScript& scriptOut);
// 10m max asset input range circulation
static const CAmount MAX_INPUTRANGE_ASSET = 10000000;
/** Upper bound for mantissa.
* 10^18-1 is the largest arbitrary decimal that will fit in a signed 64-bit integer.
* Larger integers cannot consist of arbitrary combinations of 0-9:
*
*   999999999999999999  10^18-1
*  1000000000000000000  10^18		(would overflow)
*  9223372036854775807  (1<<63)-1   (max int64_t)
*  9999999999999999999  10^19-1     (would overflow)
*/
static const CAmount MAX_ASSET = 1000000000000000000LL - 1LL;
inline bool AssetRange(const CAmount& nValue, bool bUseInputRange) { return (nValue > 0 && nValue <= (bUseInputRange? MAX_INPUTRANGE_ASSET: MAX_ASSET)); }
enum {
    ASSET_UPDATE_ADMIN=1, // god mode flag, governs flags field below
    ASSET_UPDATE_DATA=2, // can you update pubic data field?
    ASSET_UPDATE_CONTRACT=4, // can you update smart contract field?
    ASSET_UPDATE_SUPPLY=8, // can you update supply?
    ASSET_UPDATE_BLACKLIST=16, // can you update blacklist?
    ASSET_UPDATE_FLAGS=32, // can you update flags? if you would set permanently disable this one and admin flag as well
    ASSET_UPDATE_ALL=63
};
class CAsset {
public:
	std::vector<unsigned char> vchAsset;
	std::vector<unsigned char> vchSymbol;
	std::vector<uint8_t> vchAddress;
	std::vector<unsigned char> vchContract;
    std::vector<std::vector<uint8_t> > vchBlacklist;
	std::vector<unsigned char> vchExtra;
	// if allocations are tracked by individual inputs
	std::vector<CRange> listAllocationInputs;
    uint256 txHash;
	unsigned int nHeight;
	std::vector<unsigned char> vchPubData;
	CAmount nBalance;
	CAmount nTotalSupply;
	CAmount nMaxSupply;
	bool bUseInputRanges;
	unsigned char nPrecision;
	float fInterestRate;
	unsigned char nUpdateFlags;
    CAsset() {
        SetNull();
    }
    CAsset(const CTransaction &tx) {
        SetNull();
        UnserializeFromTx(tx);
    }
	inline void ClearAsset()
	{
		vchPubData.clear();
		listAllocationInputs.clear();
		vchAddress.clear();
		vchSymbol.clear();
		vchContract.clear();
        vchBlacklist.clear();

	}
	ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
	inline void SerializationOp(Stream& s, Operation ser_action) {		
		READWRITE(vchPubData);
		READWRITE(txHash);
		READWRITE(VARINT(nHeight));
		READWRITE(vchAsset);
		READWRITE(vchSymbol);
		READWRITE(vchAddress);
		READWRITE(listAllocationInputs);
		READWRITE(nBalance);
		READWRITE(nTotalSupply);
		READWRITE(nMaxSupply);
		READWRITE(bUseInputRanges);
		READWRITE(fInterestRate);
		READWRITE(VARINT(nUpdateFlags));
		READWRITE(VARINT(nPrecision));
		READWRITE(vchContract);
        READWRITE(vchBlacklist);
		READWRITE(vchExtra);
	}
    inline friend bool operator==(const CAsset &a, const CAsset &b) {
        return (
		a.vchAsset == b.vchAsset
        );
    }

    inline CAsset operator=(const CAsset &b) {
		vchPubData = b.vchPubData;
		txHash = b.txHash;
        nHeight = b.nHeight;
		vchAddress = b.vchAddress;
		vchAsset = b.vchAsset;
		listAllocationInputs = b.listAllocationInputs;
		nBalance = b.nBalance;
		nTotalSupply = b.nTotalSupply;
		nMaxSupply = b.nMaxSupply;
		bUseInputRanges = b.bUseInputRanges;
		fInterestRate = b.fInterestRate;
		nUpdateFlags = b.nUpdateFlags;
		nPrecision = b.nPrecision;
		vchSymbol = b.vchSymbol;
		vchContract = b.vchContract;
        vchBlacklist = b.vchBlacklist;
        return *this;
    }

    inline friend bool operator!=(const CAsset &a, const CAsset &b) {
        return !(a == b);
    }
	inline void SetNull() { vchExtra.clear(); vchBlacklist.clear(); vchContract.clear(); vchSymbol.clear(); nPrecision = 8; nUpdateFlags = 0; fInterestRate = 0;  bUseInputRanges = false; nMaxSupply = 0; nTotalSupply = 0; nBalance = 0; listAllocationInputs.clear(); vchAsset.clear(); nHeight = 0; txHash.SetNull(); vchAddress.clear(); vchPubData.clear(); }
    inline bool IsNull() const { return (vchAsset.empty()); }
    bool UnserializeFromTx(const CTransaction &tx);
	bool UnserializeFromData(const std::vector<unsigned char> &vchData, const std::vector<unsigned char> &vchHash);
	void Serialize(std::vector<unsigned char>& vchData);
};


class CAssetDB : public CDBWrapper {
public:
    CAssetDB(size_t nCacheSize, bool fMemory, bool fWipe) : CDBWrapper(GetDataDir() / "assets", nCacheSize, fMemory, fWipe) {}

    bool WriteAsset(const CAsset& asset, const int &op) {
		bool writeState = false;
		writeState = Write(make_pair(std::string("asseti"), asset.vchAsset), asset);
		if(writeState)
			WriteAssetIndex(asset, op);
        return writeState;
    }
	bool EraseAsset(const std::vector<unsigned char>& vchAsset, bool cleanup = false) {
		return Erase(make_pair(std::string("asseti"), vchAsset));
	}
    bool ReadAsset(const std::vector<unsigned char>& vchAsset, CAsset& asset) {
        return Read(make_pair(std::string("asseti"), vchAsset), asset);
    }
	void WriteAssetIndex(const CAsset& asset, const int &op);
	bool ScanAssets(const int count, const int from, const UniValue& oOptions, UniValue& oRes);
};
bool GetAsset(const std::vector<unsigned char> &vchAsset,CAsset& txPos);
bool BuildAssetJson(const CAsset& asset, const bool bGetInputs, UniValue& oName);
bool BuildAssetIndexerJson(const CAsset& asset,UniValue& oName);
UniValue ValueFromAssetAmount(const CAmount& amount, int precision, bool isInputRange);
CAmount AssetAmountFromValue(UniValue& value, int precision, bool isInputRange);
CAmount AssetAmountFromValueNonNeg(const UniValue& value, int precision, bool isInputRange);
bool AssetRange(const CAmount& amountIn, int precision, bool isInputRange);
extern std::unique_ptr<CAssetDB> passetdb;
extern std::unique_ptr<CAssetAllocationDB> passetallocationdb;
extern std::unique_ptr<CAssetAllocationTransactionsDB> passetallocationtransactionsdb;
#endif // ASSET_H
