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
void CreateFeeRecipient(CScript& scriptPubKey, CRecipient& recipient);
unsigned int addressunspent(const std::string& strAddressFrom, COutPoint& outpoint);
int GetSyscoinDataOutput(const CTransaction& tx);
bool DecodeAndParseSyscoinTx(const CTransaction& tx, int& op, std::vector<std::vector<unsigned char> >& vvch, char &type);
bool GetSyscoinData(const CTransaction &tx, std::vector<unsigned char> &vchData, int& nOut, int &op);
bool GetSyscoinData(const CScript &scriptPubKey, std::vector<unsigned char> &vchData,  int &op);
bool GetSyscoinMintData(const CTransaction &tx, std::vector<unsigned char> &vchData, int& nOut);
bool GetSyscoinMintData(const CScript &scriptPubKey, std::vector<unsigned char> &vchData);
void SysTxToJSON(const int op, const std::vector<unsigned char> &vchData,  UniValue &entry, const char& type);
std::string GetSyscoinTransactionDescription(const CTransaction& tx, const int op, std::string& responseEnglish, const char &type, std::string& responseGUID);
bool IsOutpointMature(const COutPoint& outpoint);
UniValue syscointxfund_helper(const std::string &vchWitness, std::vector<CRecipient> &vecSend);
bool FlushSyscoinDBs();
bool FindAssetOwnerInTx(const CCoinsViewCache &inputs, const CTransaction& tx, const std::string& ownerAddressToMatch);
CWallet* GetDefaultWallet();
CAmount GetFee(const size_t nBytes);
bool DecodeAndParseAssetTx(const CTransaction& tx, int& op, std::vector<std::vector<unsigned char> >& vvch, char& type);
bool DecodeAssetScript(const CScript& script, int& op, std::vector<std::vector<unsigned char> > &vvch);
bool IsAssetOp(int op);
int GenerateSyscoinGuid();
bool IsSyscoinScript(const CScript& scriptPubKey, int &op, std::vector<std::vector<unsigned char> > &vvchArgs);
bool RemoveSyscoinScript(const CScript& scriptPubKeyIn, CScript& scriptPubKeyOut);
void AssetTxToJSON(const int op, const std::vector<unsigned char> &vchData, UniValue &entry);
std::string assetFromOp(int op);
bool RemoveAssetScriptPrefix(const CScript& scriptIn, CScript& scriptOut);
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
inline bool AssetRange(const CAmount& nValue) { return (nValue > 0 && nValue <= MAX_ASSET); }
enum {
    ASSET_UPDATE_ADMIN=1, // god mode flag, governs flags field below
    ASSET_UPDATE_DATA=2, // can you update pubic data field?
    ASSET_UPDATE_CONTRACT=4, // can you update smart contract/burn method signature fields? If you modify this, any subsequent sysx mints will need to wait atleast 10 blocks
    ASSET_UPDATE_SUPPLY=8, // can you update supply?
    ASSET_UPDATE_FLAGS=16, // can you update flags? if you would set permanently disable this one and admin flag as well
    ASSET_UPDATE_ALL=31
};
class CAsset {
public:
	uint32_t nAsset;
	std::vector<uint8_t> vchAddress;
	std::vector<unsigned char> vchContract;
    std::vector<unsigned char> vchBurnMethodSignature;
    uint256 txHash;
    unsigned int nHeight;
	std::vector<unsigned char> vchPubData;
	CAmount nBalance;
	CAmount nTotalSupply;
	CAmount nMaxSupply;
	unsigned char nPrecision;
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
		vchAddress.clear();
		vchContract.clear();
        vchBurnMethodSignature.clear();

	}
	ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
	inline void SerializationOp(Stream& s, Operation ser_action) {		
		READWRITE(vchPubData);
		READWRITE(txHash);
		READWRITE(VARINT(nAsset));
		READWRITE(vchAddress);
		READWRITE(nBalance);
		READWRITE(nTotalSupply);
		READWRITE(nMaxSupply);
        READWRITE(nHeight);
		READWRITE(VARINT(nUpdateFlags));
		READWRITE(VARINT(nPrecision));
		READWRITE(vchContract); 
        READWRITE(vchBurnMethodSignature);     
	}
    inline friend bool operator==(const CAsset &a, const CAsset &b) {
        return (
		a.nAsset == b.nAsset
        );
    }

    inline CAsset operator=(const CAsset &b) {
		vchPubData = b.vchPubData;
		txHash = b.txHash;
		vchAddress = b.vchAddress;
		nAsset = b.nAsset;
		nBalance = b.nBalance;
		nTotalSupply = b.nTotalSupply;
		nMaxSupply = b.nMaxSupply;
		nUpdateFlags = b.nUpdateFlags;
		nPrecision = b.nPrecision;
		vchContract = b.vchContract;
        nHeight = b.nHeight;
        vchBurnMethodSignature = b.vchBurnMethodSignature;
        return *this;
    }

    inline friend bool operator!=(const CAsset &a, const CAsset &b) {
        return !(a == b);
    }
	inline void SetNull() { vchBurnMethodSignature.clear(); nHeight = 0;vchContract.clear(); nPrecision = 8; nUpdateFlags = 0; nMaxSupply = 0; nTotalSupply = 0; nBalance = 0; nAsset= 0; txHash.SetNull(); vchAddress.clear(); vchPubData.clear(); }
    inline bool IsNull() const { return (nAsset == 0); }
    bool UnserializeFromTx(const CTransaction &tx);
	bool UnserializeFromData(const std::vector<unsigned char> &vchData);
	void Serialize(std::vector<unsigned char>& vchData);
};
class CMintSyscoin {
public:
    std::vector<unsigned char> vchValue;
    std::vector<unsigned char> vchParentNodes;
    std::vector<unsigned char> vchBlockHash;
    std::vector<unsigned char> vchPath;
    CMintSyscoin() {
        SetNull();
    }
    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {      
        READWRITE(vchValue);
        READWRITE(vchParentNodes);
        READWRITE(vchBlockHash);
        READWRITE(vchPath);   
    }
    inline void SetNull() { vchValue.clear(); vchParentNodes.clear(); vchBlockHash.clear(); vchPath.clear(); }
    inline bool IsNull() const { return (vchValue.empty()); }
    bool UnserializeFromData(const std::vector<unsigned char> &vchData);
    void Serialize(std::vector<unsigned char>& vchData);
};
static const std::string assetKey = "AI";
static const std::string lastAssetKey = "LAI";
typedef std::unordered_map<int, CAsset> AssetMap;
class CAssetDB : public CDBWrapper {
public:
    CAssetDB(size_t nCacheSize, bool fMemory, bool fWipe) : CDBWrapper(GetDataDir() / "assets", nCacheSize, fMemory, fWipe) {}
    bool EraseAsset(const uint32_t& nAsset, bool cleanup = false) {
        return Erase(make_pair(assetKey, nAsset));
    }   
    bool ReadAsset(const uint32_t& nAsset, CAsset& asset) {
        return Read(make_pair(assetKey, nAsset), asset);
    }
    bool ReadLastAsset(const uint32_t& nAsset, CAsset& asset) {
        return Read(make_pair(lastAssetKey, nAsset), asset);
    }  
    bool EraseLastAsset(const uint32_t& nAsset, bool cleanup = false) {
        return Erase(make_pair(lastAssetKey, nAsset));
    }  
	void WriteAssetIndex(const CAsset& asset, const int &op);
	bool ScanAssets(const int count, const int from, const UniValue& oOptions, UniValue& oRes);
    bool Flush(const AssetMap &mapAssets);
    bool Flush(const AssetMap &mapLastAssets, const AssetMap &mapAssets);
};
bool GetAsset(const int &nAsset,CAsset& txPos);
bool BuildAssetJson(const CAsset& asset, UniValue& oName);
bool BuildAssetIndexerJson(const CAsset& asset,UniValue& oName);
UniValue ValueFromAssetAmount(const CAmount& amount, int precision);
CAmount AssetAmountFromValue(UniValue& value, int precision);
CAmount AssetAmountFromValueNonNeg(const UniValue& value, int precision);
bool AssetRange(const CAmount& amountIn, int precision);
bool CheckAssetInputs(const CTransaction &tx, const CCoinsViewCache &inputs, int op, const std::vector<std::vector<unsigned char> > &vvchArgs, bool fJustCheck, int nHeight, AssetMap& mapLastAssets, AssetMap &mapAssets, AssetAllocationMap &mapAssetAllocations, AssetBalanceMap &blockMapAssetBalances, std::string &errorMessage, bool bSanityCheck=false);
bool DecodeAssetTx(const CTransaction& tx, int& op, std::vector<std::vector<unsigned char> >& vvch);
extern std::unique_ptr<CAssetDB> passetdb;
extern std::unique_ptr<CAssetAllocationDB> passetallocationdb;
extern std::unique_ptr<CAssetAllocationTransactionsDB> passetallocationtransactionsdb;
#endif // ASSET_H
