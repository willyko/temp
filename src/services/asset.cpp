// Copyright (c) 2017-2018 The Syscoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "services/asset.h"
#include "services/assetallocation.h"
#include "init.h"
#include "validation.h"
#include "util.h"
#include "random.h"
#include "base58.h"
#include "core_io.h"
#include "rpc/server.h"
#include "wallet/wallet.h"
#include "chainparams.h"
#include "wallet/coincontrol.h"
#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/case_conv.hpp> // for to_upper()
#include <boost/lexical_cast.hpp>
#include <boost/thread.hpp>
#include <boost/range/algorithm_ext/erase.hpp>
#include <chrono>
#include <key_io.h>
#include <policy/policy.h>
#include <consensus/validation.h>
#include <wallet/fees.h>
#include <outputtype.h>
#include <bech32.h>
unsigned int MAX_UPDATES_PER_BLOCK = 10;
std::unique_ptr<CAssetDB> passetdb;
std::unique_ptr<CAssetAllocationDB> passetallocationdb;
std::unique_ptr<CAssetAllocationTransactionsDB> passetallocationtransactionsdb;
using namespace std::chrono;
using namespace std;
bool FindSyscoinScriptOp(const CScript& script, int& op) {
	CScript::const_iterator pc = script.begin();
	opcodetype opcode;
	if (!script.GetOp(pc, opcode))
		return false;
	if (opcode < OP_1 || opcode > OP_16)
		return false;
	op = CScript::DecodeOP_N(opcode);
	return op == OP_SYSCOIN_ASSET || op == OP_SYSCOIN_ASSET_ALLOCATION || op == OP_ASSET_ALLOCATION_BURN;
}
bool IsSyscoinScript(const CScript& scriptPubKey, int &op, vector<vector<unsigned char> > &vvchArgs)
{
	if (DecodeAssetAllocationScript(scriptPubKey, op, vvchArgs))
		return true;
	else if (DecodeAssetScript(scriptPubKey, op, vvchArgs))
		return true;
	return false;
}
bool RemoveSyscoinScript(const CScript& scriptPubKeyIn, CScript& scriptPubKeyOut)
{
	if (!RemoveAssetAllocationScriptPrefix(scriptPubKeyIn, scriptPubKeyOut))
		if (!RemoveAssetScriptPrefix(scriptPubKeyIn, scriptPubKeyOut))
			return false;
		return true;
}

int GetSyscoinDataOutput(const CTransaction& tx) {
	for (unsigned int i = 0; i<tx.vout.size(); i++) {
		if (IsSyscoinDataOutput(tx.vout[i]))
			return i;
	}
	return -1;
}
bool IsSyscoinDataOutput(const CTxOut& out) {
	txnouttype whichType;
	if (!IsStandard(out.scriptPubKey, whichType))
		return false;
	if (whichType == TX_NULL_DATA)
		return true;
	return false;
}

string stringFromValue(const UniValue& value) {
	string strName = value.get_str();
	return strName;
}
vector<unsigned char> vchFromValue(const UniValue& value) {
	string strName = value.get_str();
	unsigned char *strbeg = (unsigned char*)strName.c_str();
	return vector<unsigned char>(strbeg, strbeg + strName.size());
}

std::vector<unsigned char> vchFromString(const std::string &str) {
	unsigned char *strbeg = (unsigned char*)str.c_str();
	return vector<unsigned char>(strbeg, strbeg + str.size());
}
std::vector<uint8_t> vchFromStringUint8(const std::string &str) {
    uint8_t *strbeg = (uint8_t*)str.c_str();
    return vector<uint8_t>(strbeg, strbeg + str.size());
}
string stringFromVchUint8(const vector<uint8_t> &vch) {
    string res;
    vector<uint8_t>::const_iterator vi = vch.begin();
    while (vi != vch.end()) {
        res += (uint8_t)(*vi);
        vi++;
    }
    return res;
}
string stringFromVch(const vector<unsigned char> &vch) {
	string res;
	vector<unsigned char>::const_iterator vi = vch.begin();
	while (vi != vch.end()) {
		res += (char)(*vi);
		vi++;
	}
	return res;
}
bool GetSyscoinData(const CTransaction &tx, vector<unsigned char> &vchData, vector<unsigned char> &vchHash, int& nOut, int &op)
{
	nOut = GetSyscoinDataOutput(tx);
	if (nOut == -1)
		return false;

	const CScript &scriptPubKey = tx.vout[nOut].scriptPubKey;
	return GetSyscoinData(scriptPubKey, vchData, vchHash, op);
}
bool GetSyscoinData(const CScript &scriptPubKey, vector<unsigned char> &vchData, vector<unsigned char> &vchHash, int &op)
{
	op = 0;
	CScript::const_iterator pc = scriptPubKey.begin();
	opcodetype opcode;
	if (!scriptPubKey.GetOp(pc, opcode))
		return false;
	if (opcode != OP_RETURN)
		return false;
	if (!scriptPubKey.GetOp(pc, opcode))
		return false;
	if (opcode < OP_1 || opcode > OP_16)
		return false;
	op = CScript::DecodeOP_N(opcode);
	if (!scriptPubKey.GetOp(pc, opcode, vchData))
		return false;
	if (!scriptPubKey.GetOp(pc, opcode, vchHash))
		return false;
	return true;
}
bool IsAssetOp(int op) {
    return op == OP_ASSET_ACTIVATE
        || op == OP_ASSET_UPDATE
        || op == OP_ASSET_TRANSFER
		|| op == OP_ASSET_SEND;
}


string assetFromOp(int op) {
    switch (op) {
    case OP_ASSET_ACTIVATE:
        return "assetactivate";
    case OP_ASSET_UPDATE:
        return "assetupdate";
    case OP_ASSET_TRANSFER:
        return "assettransfer";
	case OP_ASSET_SEND:
		return "assetsend";
    default:
        return "<unknown asset op>";
    }
}
bool CAsset::UnserializeFromData(const vector<unsigned char> &vchData, const vector<unsigned char> &vchHash) {
    try {
		CDataStream dsAsset(vchData, SER_NETWORK, PROTOCOL_VERSION);
		dsAsset >> *this;
		vector<unsigned char> vchSerializedData;
		Serialize(vchSerializedData);
		const uint256 &calculatedHash = Hash(vchSerializedData.begin(), vchSerializedData.end());
		const vector<unsigned char> &vchRand = vector<unsigned char>(calculatedHash.begin(), calculatedHash.end());
		if (vchRand != vchHash) {
			SetNull();
			return false;
		}
    } catch (std::exception &e) {
		SetNull();
        return false;
    }
	return true;
}
bool CAsset::UnserializeFromTx(const CTransaction &tx) {
	vector<unsigned char> vchData;
	vector<unsigned char> vchHash;
	int nOut, op;
	if (!GetSyscoinData(tx, vchData, vchHash, nOut, op) || op != OP_SYSCOIN_ASSET)
	{
		SetNull();
		return false;
	}
	if(!UnserializeFromData(vchData, vchHash))
	{	
		return false;
	}
    return true;
}
bool FlushSyscoinDBs() {
	{
		LOCK(cs_assetallocationindex);
		if (passetallocationtransactionsdb != nullptr)
		{
			passetallocationtransactionsdb->WriteAssetAllocationWalletIndex(AssetAllocationIndex);
			if (!passetallocationtransactionsdb->Flush()) {
				LogPrintf("Failed to write to asset allocation transactions database!");
				return false;
			}
		}
	}
	return true;
}
bool DecodeAndParseSyscoinTx(const CTransaction& tx, int& op,
	vector<vector<unsigned char> >& vvch, char& type)
{
	return
		DecodeAndParseAssetAllocationTx(tx, op, vvch, type)
		|| DecodeAndParseAssetTx(tx, op, vvch, type);
}
bool FindAssetOwnerInTx(const CCoinsViewCache &inputs, const CTransaction& tx, const string& ownerAddressToMatch) {
	CTxDestination dest;
	for (unsigned int i = 0; i < tx.vin.size(); i++) {
		const Coin& prevCoins = inputs.AccessCoin(tx.vin[i].prevout);
		if (prevCoins.IsSpent()) {
			continue;
		}
		if (!ExtractDestination(prevCoins.out.scriptPubKey, dest))
			continue;
		if (EncodeDestination(dest) == ownerAddressToMatch) {
			return true;
		}
	}
	return false;
}
void CreateAssetRecipient(const CScript& scriptPubKey, CRecipient& recipient)
{
	CRecipient recp = { scriptPubKey, recipient.nAmount, false };
	recipient = recp;
	CCoinControl coin_control;
	const CAmount &minFee = GetFee(3000);
	recipient.nAmount = minFee;
}
void CreateRecipient(const CScript& scriptPubKey, CRecipient& recipient)
{
	CRecipient recp = { scriptPubKey, recipient.nAmount, false };
	recipient = recp;
	CTxOut txout(recipient.nAmount, scriptPubKey);
	size_t nSize = GetSerializeSize(txout, SER_DISK, 0) + 148u;
	recipient.nAmount = GetFee(nSize);
}
void CreateFeeRecipient(CScript& scriptPubKey, const vector<unsigned char>& data, CRecipient& recipient)
{
	// add hash to data output (must match hash in inputs check with the tx scriptpubkey hash)
	uint256 hash = Hash(data.begin(), data.end());
	vector<unsigned char> vchHashRand = vector<unsigned char>(hash.begin(), hash.end());
	scriptPubKey << vchHashRand;
	CRecipient recp = { scriptPubKey, 0, false };
	recipient = recp;
}
UniValue SyscoinListReceived(bool includeempty = true, bool includechange = false)
{
	map<string, int> mapAddress;
	UniValue ret(UniValue::VARR);
	CWallet* const pwallet = GetDefaultWallet();
    if(!pwallet)
        return ret;
	const std::map<CKeyID, int64_t>& mapKeyPool = pwallet->GetAllReserveKeys();
	for (const std::pair<const CTxDestination, CAddressBookData>& item : pwallet->mapAddressBook) {

		const CTxDestination& dest = item.first;
		const string& strAccount = item.second.name;

		isminefilter filter = ISMINE_SPENDABLE;
		isminefilter mine = IsMine(*pwallet, dest);
		if (!(mine & filter))
			continue;

		const string& strAddress = EncodeDestination(dest);


		UniValue paramsBalance(UniValue::VARR);
		UniValue param(UniValue::VOBJ);
		UniValue balanceParams(UniValue::VARR);
		balanceParams.push_back("addr(" + strAddress + ")");
		paramsBalance.push_back("start");
		paramsBalance.push_back(balanceParams);
		JSONRPCRequest request;
		request.params = paramsBalance;
		UniValue resBalance = scantxoutset(request);
		UniValue obj(UniValue::VOBJ);
		obj.pushKV("address", strAddress);
		const CAmount& nBalance = AmountFromValue(find_value(resBalance.get_obj(), "total_amount"));
		if (includeempty || (!includeempty && nBalance > 0)) {
			obj.pushKV("balance", ValueFromAmount(nBalance));
			obj.pushKV("label", strAccount);
			const CKeyID *keyID = boost::get<CKeyID>(&dest);
			if (keyID && !pwallet->mapAddressBook.count(dest) && !mapKeyPool.count(*keyID)) {
				if (!includechange)
					continue;
				obj.pushKV("change", true);
			}
			else
				obj.pushKV("change", false);
			ret.push_back(obj);
		}
		mapAddress[strAddress] = 1;
	}

	vector<COutput> vecOutputs;
	{
		LOCK(pwallet->cs_wallet);
		pwallet->AvailableCoins(vecOutputs, true, nullptr, 1, MAX_MONEY, MAX_MONEY, 0, 0, 9999999, ALL_COINS);
	}
	BOOST_FOREACH(const COutput& out, vecOutputs) {
		CTxDestination address;
		if (!ExtractDestination(out.tx->tx->vout[out.i].scriptPubKey, address))
			continue;

		const string& strAddress = EncodeDestination(address);
		if (mapAddress.find(strAddress) != mapAddress.end())
			continue;

		UniValue paramsBalance(UniValue::VARR);
		UniValue param(UniValue::VOBJ);
		UniValue balanceParams(UniValue::VARR);
		balanceParams.push_back("addr(" + strAddress + ")");
		paramsBalance.push_back("start");
		paramsBalance.push_back(balanceParams);
		JSONRPCRequest request;
		request.params = paramsBalance;
		UniValue resBalance = scantxoutset(request);
		UniValue obj(UniValue::VOBJ);
		obj.pushKV("address", strAddress);
		const CAmount& nBalance = AmountFromValue(find_value(resBalance.get_obj(), "total_amount"));
		if (includeempty || (!includeempty && nBalance > 0)) {
			obj.pushKV("balance", ValueFromAmount(nBalance));
			obj.pushKV("label", "");
			const CKeyID *keyID = boost::get<CKeyID>(&address);
			if (keyID && !pwallet->mapAddressBook.count(address) && !mapKeyPool.count(*keyID)) {
				if (!includechange)
					continue;
				obj.pushKV("change", true);
			}
			else
				obj.pushKV("change", false);
			ret.push_back(obj);
		}
		mapAddress[strAddress] = 1;

	}
	return ret;
}
UniValue syscointxfund_helper(const string &vchWitness, vector<CRecipient> &vecSend) {
	CMutableTransaction txNew;
	txNew.nVersion = SYSCOIN_TX_VERSION_ASSET;

	COutPoint witnessOutpoint;
	if (!vchWitness.empty() && vchWitness != "''")
	{
		string strWitnessAddress;
		strWitnessAddress = vchWitness;
		addressunspent(strWitnessAddress, witnessOutpoint);
		if (witnessOutpoint.IsNull())
		{
			throw runtime_error("SYSCOIN_RPC_ERROR ERRCODE: 9000 - " + _("This transaction requires a witness but not enough outputs found for witness address: ") + vchWitness);
		}
		Coin pcoinW;
		if (GetUTXOCoin(witnessOutpoint, pcoinW))
			txNew.vin.push_back(CTxIn(witnessOutpoint, pcoinW.out.scriptPubKey));
	}

	// vouts to the payees
	for (const auto& recipient : vecSend)
	{
		CTxOut txout(recipient.nAmount, recipient.scriptPubKey);
		if (!IsDust(txout, dustRelayFee))
		{
			txNew.vout.push_back(txout);
		}
	}   

	UniValue paramsFund(UniValue::VARR);
	paramsFund.push_back(EncodeHexTx(txNew));
	return paramsFund;
}

CWallet* GetDefaultWallet() {
	const std::vector<std::shared_ptr<CWallet>>& vecWallets = GetWallets();
	if (vecWallets.empty())
		return nullptr;
	std::shared_ptr<CWallet> const wallet = vecWallets[0];
	CWallet* const pwallet = wallet.get();
	return pwallet;
}
CAmount GetFee(const size_t nBytes) {
    CWallet* const pwallet = GetDefaultWallet();
	FeeCalculation feeCalc;
	CFeeRate feeRate = ::feeEstimator.estimateSmartFee(1, &feeCalc, true);
	CAmount minFee=0;
	if (feeRate != CFeeRate(0)) {
		minFee = feeRate.GetFeePerK()*nBytes / 1000;
	}
	else if(pwallet != nullptr){
		minFee = GetRequiredFee(*pwallet, nBytes);
	}
	return minFee;
}

class CCountSigsVisitor : public boost::static_visitor<void> {
private:
	const CKeyStore &keystore;
	int &nNumSigs;

public:
	CCountSigsVisitor(const CKeyStore &keystoreIn, int &numSigs) : keystore(keystoreIn), nNumSigs(numSigs) {}

	void Process(const CScript &script) {
		txnouttype type;
		std::vector<CTxDestination> vDest;
		int nRequired;
		if (ExtractDestinations(script, type, vDest, nRequired)) {
			BOOST_FOREACH(const CTxDestination &dest, vDest)
				boost::apply_visitor(*this, dest);
		}
	}
	void operator()(const CKeyID &keyId) {
		nNumSigs++;
	}

	void operator()(const CScriptID &scriptId) {
		CScript script;
		if (keystore.GetCScript(scriptId, script))
			Process(script);
	}
	void operator()(const WitnessV0ScriptHash& scriptID)
	{
		CScriptID id;
		CRIPEMD160().Write(scriptID.begin(), 32).Finalize(id.begin());
		CScript script;
		if (keystore.GetCScript(id, script)) {
			Process(script);
		}
	}

	void operator()(const WitnessV0KeyHash& keyid) {
		nNumSigs++;
	}

	template<typename X>
	void operator()(const X &none) {}
};
UniValue syscointxfund(const JSONRPCRequest& request) {
	std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
	CWallet* const pwallet = wallet.get();
	const UniValue &params = request.params;
	if (request.fHelp || 1 > params.size() || 4 < params.size())
		throw runtime_error(
			"syscointxfund\n"
			"\nFunds a new syscoin transaction with inputs used from wallet or an array of addresses specified.\n"
			"\nArguments:\n"
			"  \"hexstring\" (string, required) The raw syscoin transaction output given from rpc (ie: assetnew, assetupdate)\n"
			"  \"address\"  (string, required) Address belonging to this asset transaction. \n"
			"  \"txid\"  (string, optional) Pass in a txid/vout to fund the transaction with. Leave empty to use address. \n"
			"  \"vout\"  (number, optional) If txid is passed in you should also pass in the output index into the transaction for the UTXO to fund this transaction with. \n"
			"\nExamples:\n"
			+ HelpExampleCli("syscointxfund", " <hexstring> \"175tWpb8K1S7NmH4Zx6rewF9WQrcZv245W\"")
			+ HelpExampleCli("syscointxfund", " <hexstring> \"175tWpb8K1S7NmH4Zx6rewF9WQrcZv245W\" \"1075db55d416d3ca199f55b6084e2115b9345e16c5cf302fc80e9d5fbf5d48d\" 0")
			+ HelpRequiringPassphrase(pwallet));

	const string &hexstring = params[0].get_str();
    const string &strAddress = params[1].get_str();
	CMutableTransaction tx;
    // decode as non-witness
	if (!DecodeHexTx(tx, hexstring, true, false))
		throw runtime_error("SYSCOIN_ASSET_RPC_ERROR: ERRCODE: 5500 - " + _("Could not send raw transaction: Cannot decode transaction from hex string: ") + hexstring);
	
	UniValue addressArray(UniValue::VARR);	
	bool bFunded = false;
    if (params.size() > 2) {
        COutPoint fundOut;
        fundOut.hash.SetHex(params[2].get_str());
        fundOut.n = params[3].get_int();
        Coin coin;
        if (!GetUTXOCoin(fundOut, coin))
            throw runtime_error("SYSCOIN_ASSET_RPC_ERROR: ERRCODE: 5501 - " + _("No unspent outputs found in txid/vout information you provided: ") + fundOut.ToStringShort());
        tx.vin.push_back(CTxIn(fundOut, coin.out.scriptPubKey));
        // ensure the address extracted matches the adddress passed in
        CTxDestination destination;
        if(!ExtractDestination(coin.out.scriptPubKey, destination) || EncodeDestination(destination) != strAddress)
            throw runtime_error("SYSCOIN_ASSET_RPC_ERROR: ERRCODE: 5500 - " + _("Transaction output address does not match first address parameter passed in"));
        // if we pass in a txid/vout we probably assume the one input is enough to fund it, this is an optimal path anyway for efficiency. If you don't have enough don't pass it in, the logic below will fall through and find outputs based on the address.
        bFunded = true;
    }
    else{
        // save recipient assuming the owner of the address is getting the outputs for future funding
        // if assumption is incorrect then the tx won't confirm as ownership needs to be proven in consensus code anyway
        CRecipient addressRecipient;
        CScript scriptPubKeyFromOrig = GetScriptForDestination(DecodeDestination(strAddress));
        CreateAssetRecipient(scriptPubKeyFromOrig, addressRecipient);  
        addressArray.push_back("addr(" + strAddress + ")");
        COutPoint addressOutPoint;
        unsigned int unspentcount = addressunspent(strAddress, addressOutPoint);
        if (unspentcount <= 1 && !fTPSTestEnabled)
        {
            for (unsigned int i = 0; i < MAX_UPDATES_PER_BLOCK; i++)
                tx.vout.push_back(CTxOut(addressRecipient.nAmount, addressRecipient.scriptPubKey));
        }
        Coin pcoin;
        if (GetUTXOCoin(addressOutPoint, pcoin))
            tx.vin.push_back(CTxIn(addressOutPoint, pcoin.out.scriptPubKey));   
    }
    
      
        
    CTransaction txIn_t(tx);
    
    // add total output amount of transaction to desired amount
    CAmount nDesiredAmount = txIn_t.GetValueOut();
    CAmount nCurrentAmount = 0;

    LOCK(cs_main);
    CCoinsViewCache view(pcoinsTip.get());
    // get value of inputs
    nCurrentAmount = view.GetValueIn(txIn_t);

    // # vin (with IX)*FEE + # vout*FEE + (10 + # vin)*FEE + 34*FEE (for change output)
    CAmount nFees = GetFee(10 + 34);

    for (auto& vin : tx.vin) {
        Coin coin;
        if (!GetUTXOCoin(vin.prevout, coin))
            continue;
        int numSigs = 0;
        CCountSigsVisitor(*pwallet, numSigs).Process(coin.out.scriptPubKey);
        nFees += GetFee(numSigs * 200);
    }
    for (auto& vout : tx.vout) {
        const unsigned int nBytes = ::GetSerializeSize(vout, SER_NETWORK, PROTOCOL_VERSION);
        nFees += GetFee(nBytes);
    }
    
    
    
	if(!bFunded){
    	UniValue paramsBalance(UniValue::VARR);
    	paramsBalance.push_back("start");
    	paramsBalance.push_back(addressArray);
    	JSONRPCRequest request1;
    	request1.params = paramsBalance;

    	UniValue resUTXOs = scantxoutset(request1);
    	UniValue utxoArray(UniValue::VARR);
    	if (resUTXOs.isObject()) {
    		const UniValue& resUtxoUnspents = find_value(resUTXOs.get_obj(), "unspents");
    		if (!resUtxoUnspents.isArray())
    			throw runtime_error("SYSCOIN_ASSET_RPC_ERROR: ERRCODE: 5501 - " + _("No unspent outputs found in addresses provided"));
    		utxoArray = resUtxoUnspents.get_array();
    	}
    	else
    		throw runtime_error("SYSCOIN_ASSET_RPC_ERROR: ERRCODE: 5501 - " + _("No funds found in addresses provided"));


    	const CAmount &minFee = GetFee(3000);
    	if (nCurrentAmount < (nDesiredAmount + nFees)) {
    		// only look for small inputs if addresses were passed in, if looking through wallet we do not want to fund via small inputs as we may end up spending small inputs inadvertently
    		if (tx.nVersion == SYSCOIN_TX_VERSION_ASSET && params.size() > 1) {
    			LOCK(mempool.cs);
    			// fund with small inputs first
    			for (unsigned int i = 0; i < utxoArray.size(); i++)
    			{
    				const UniValue& utxoObj = utxoArray[i].get_obj();
    				const string &strTxid = find_value(utxoObj, "txid").get_str();
    				const uint256& txid = uint256S(strTxid);
    				const int& nOut = find_value(utxoObj, "vout").get_int();
    				const std::vector<unsigned char> &data(ParseHex(find_value(utxoObj, "scriptPubKey").get_str()));
    				const CScript& scriptPubKey = CScript(data.begin(), data.end());
    				const CAmount &nValue = AmountFromValue(find_value(utxoObj, "amount"));
    				const CTxIn txIn(txid, nOut, scriptPubKey);
    				const COutPoint outPoint(txid, nOut);
    				if (std::find(tx.vin.begin(), tx.vin.end(), txIn) != tx.vin.end())
    					continue;
    				// look for small inputs only, if not selecting all
    				if (nValue <= minFee) {

    					if (mempool.mapNextTx.find(outPoint) != mempool.mapNextTx.end())
    						continue;
    					{
    						LOCK(pwallet->cs_wallet);
    						if (pwallet->IsLockedCoin(txid, nOut))
    							continue;
    					}
    					if (!IsOutpointMature(outPoint))
    						continue;
    					int numSigs = 0;
    					CCountSigsVisitor(*pwallet, numSigs).Process(scriptPubKey);
    					// add fees to account for every input added to this transaction
    					nFees += GetFee(numSigs * 200);
    					tx.vin.push_back(txIn);
    					nCurrentAmount += nValue;
    					if (nCurrentAmount >= (nDesiredAmount + nFees)) {
    						break;
    					}
    				}
    			}
    		}
    		// if after selecting small inputs we are still not funded, we need to select alias balances to fund this transaction
    		if (nCurrentAmount < (nDesiredAmount + nFees)) {
    			LOCK(mempool.cs);
    			for (unsigned int i = 0; i < utxoArray.size(); i++)
    			{
    				const UniValue& utxoObj = utxoArray[i].get_obj();
    				const string &strTxid = find_value(utxoObj, "txid").get_str();
    				const uint256& txid = uint256S(strTxid);
    				const int& nOut = find_value(utxoObj, "vout").get_int();
    				const std::vector<unsigned char> &data(ParseHex(find_value(utxoObj, "scriptPubKey").get_str()));
    				const CScript& scriptPubKey = CScript(data.begin(), data.end());
    				const CAmount &nValue = AmountFromValue(find_value(utxoObj, "amount"));
    				const CTxIn txIn(txid, nOut, scriptPubKey);
    				const COutPoint outPoint(txid, nOut);
    				if (std::find(tx.vin.begin(), tx.vin.end(), txIn) != tx.vin.end())
    					continue;
    				// look for bigger inputs
    				if (nValue <= minFee)
    					continue;
    				if (mempool.mapNextTx.find(outPoint) != mempool.mapNextTx.end())
    					continue;
    				{
    					LOCK(pwallet->cs_wallet);
    					if (pwallet->IsLockedCoin(txid, nOut))
    						continue;
    				}
    				if (!IsOutpointMature(outPoint))
    					continue;
    				int numSigs = 0;
    				CCountSigsVisitor(*pwallet, numSigs).Process(scriptPubKey);
    				// add fees to account for every input added to this transaction
    				nFees += GetFee(numSigs * 200);
    				tx.vin.push_back(txIn);
    				nCurrentAmount += nValue;
    				if (nCurrentAmount >= (nDesiredAmount + nFees)) {
    					break;
    				}
    			}
    		}
    	}
    }
	const CAmount &nChange = nCurrentAmount - nDesiredAmount - nFees;
	if (nChange < 0)
		throw runtime_error("SYSCOIN_ASSET_RPC_ERROR: ERRCODE: 5502 - " + _("Insufficient funds"));
        
    // change back to funding address
	const CTxDestination & dest = DecodeDestination(strAddress);
	if (!IsValidDestination(dest))
		throw runtime_error("Change address is not valid");
	CTxOut changeOut(nChange, GetScriptForDestination(dest));
	if (!IsDust(changeOut, dustRelayFee))
		tx.vout.push_back(changeOut);
	


	if (tx.nVersion == SYSCOIN_TX_VERSION_ASSET /*&& !fTPSTestEnabled*/) {
        CValidationState state;
		// call this twice, with fJustCheck and !fJustCheck both with bSanity enabled so it doesn't actually write out to the databases just does the checks
		if (!CheckSyscoinInputs(tx, state, view, true, 0, CBlock(), true))
			throw runtime_error(FormatStateMessage(state));
		CBlock block;
		CMutableTransaction coinbasetx;
		block.vtx.push_back(MakeTransactionRef(coinbasetx));
		block.vtx.push_back(MakeTransactionRef(tx));
		if (!CheckSyscoinInputs(tx, state, view, false, 0, block, true))
			throw runtime_error(FormatStateMessage(state));
	}
	// pass back new raw transaction
	UniValue res(UniValue::VARR);
	res.push_back(EncodeHexTx(tx));
	return res;
}
UniValue syscoinburn(const JSONRPCRequest& request) {
	std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
	CWallet* const pwallet = wallet.get();
	const UniValue &params = request.params;
	if (request.fHelp || 2 != params.size())
		throw runtime_error(
			"syscoinburn [amount] [burn_to_sysx]\n"
			"<amount> Amount of SYS to burn. Note that fees are applied on top. It is not inclusive of fees.\n"
			"<burn_to_sysx> Boolean. Set to true if you are provably burning SYS to go to SYSX. False if you are provably burning SYS forever.\n"
			+ HelpRequiringPassphrase(pwallet));
            
	CAmount nAmount = AmountFromValue(params[0]);
	bool bBurnToSYSX = params[1].get_bool();

	vector<CRecipient> vecSend;
	CScript scriptData;
	scriptData << OP_RETURN;
	if (bBurnToSYSX)
		scriptData << OP_TRUE;
	else
		scriptData << OP_FALSE;

	CMutableTransaction txNew;
	CTxOut txout(nAmount, scriptData);
	txNew.vout.push_back(txout);
       

	UniValue paramsFund(UniValue::VARR);
	paramsFund.push_back(EncodeHexTx(txNew));
	return paramsFund;
}
UniValue syscoinmint(const JSONRPCRequest& request) {
	std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
	CWallet* const pwallet = wallet.get();
	const UniValue &params = request.params;
	if (request.fHelp || 2 != params.size())
		throw runtime_error(
			"syscoinmint [addressto] [amount]\n"
			"<addressto> Mint to this address.\n"
			"<amount> Amount of SYS to mint. Note that fees are applied on top. It is not inclusive of fees.\n"
			+ HelpRequiringPassphrase(pwallet));

	string vchAddress = params[0].get_str();
	CAmount nAmount = AmountFromValue(params[1]);

	vector<CRecipient> vecSend;
	const CTxDestination &dest = DecodeDestination(vchAddress);
	CScript scriptPubKeyFromOrig = GetScriptForDestination(dest);

	CMutableTransaction txNew;
	txNew.nVersion = SYSCOIN_TX_VERSION_MINT;
	txNew.vout.push_back(CTxOut(nAmount, scriptPubKeyFromOrig));

	UniValue res(UniValue::VARR);
	res.push_back(EncodeHexTx(txNew));
	return res;
}
UniValue syscoindecoderawtransaction(const JSONRPCRequest& request) {
	const UniValue &params = request.params;
	if (request.fHelp || 1 != params.size())
		throw runtime_error("syscoindecoderawtransaction <hexstring>\n"
			"Decode raw syscoin transaction (serialized, hex-encoded) and display information pertaining to the service that is included in the transactiion data output(OP_RETURN)\n"
			"<hexstring> The transaction hex string.\n");
	string hexstring = params[0].get_str();
	CMutableTransaction tx;
	if(!DecodeHexTx(tx, hexstring, false, true))
        DecodeHexTx(tx, hexstring, true, true);
	CTransaction rawTx(tx);
	if (rawTx.IsNull())
	{
		throw runtime_error("SYSCOIN_RPC_ERROR: ERRCODE: 5512 - " + _("Could not decode transaction"));
	}
	vector<unsigned char> vchData;
	int nOut;
	int op;
	vector<vector<unsigned char> > vvch;
	vector<unsigned char> vchHash;
	int type;
	char ctype;
	GetSyscoinData(rawTx, vchData, vchHash, nOut, type);
	UniValue output(UniValue::VOBJ);
	if (DecodeAndParseSyscoinTx(rawTx, op, vvch, ctype))
		SysTxToJSON(op, vchData, vchHash, output, ctype);

	return output;
}
void SysTxToJSON(const int op, const vector<unsigned char> &vchData, const vector<unsigned char> &vchHash, UniValue &entry, const char& type)
{
	if (type == OP_SYSCOIN_ASSET)
		AssetTxToJSON(op, vchData, vchHash, entry);
	else if (type == OP_SYSCOIN_ASSET_ALLOCATION)
		AssetAllocationTxToJSON(op, vchData, vchHash, entry);
}
int GenerateSyscoinGuid()
{
    int rand = 0;
    while(rand <= SYSCOIN_TX_VERSION_MINT)
	    rand = GetRand(std::numeric_limits<int>::max());
    return rand;
}
unsigned int addressunspent(const string& strAddressFrom, COutPoint& outpoint)
{
	UniValue paramsUTXO(UniValue::VARR);
	UniValue param(UniValue::VOBJ);
	UniValue utxoParams(UniValue::VARR);
	utxoParams.push_back("addr(" + strAddressFrom + ")");
	paramsUTXO.push_back("start");
	paramsUTXO.push_back(utxoParams);
	JSONRPCRequest request;
	request.params = paramsUTXO;
	UniValue resUTXOs = scantxoutset(request);
	UniValue utxoArray(UniValue::VARR);
	if (resUTXOs.isArray())
		utxoArray = resUTXOs.get_array();
	else
		return 0;
	unsigned int count = 0;
	{
		LOCK(mempool.cs);
		const CAmount &minFee = GetFee(3000);
		for (unsigned int i = 0; i < utxoArray.size(); i++)
		{
			const UniValue& utxoObj = utxoArray[i].get_obj();
			const uint256& txid = uint256S(find_value(utxoObj, "txid").get_str());
			const int& nOut = find_value(utxoObj, "vout").get_int();
			const CAmount &nValue = AmountFromValue(find_value(utxoObj, "amount"));
			if (nValue > minFee)
				continue;
			const COutPoint &outPointToCheck = COutPoint(txid, nOut);

			if (mempool.mapNextTx.find(outPointToCheck) != mempool.mapNextTx.end())
				continue;
			if (outpoint.IsNull())
				outpoint = outPointToCheck;
			count++;
		}
	}
	return count;
}
UniValue syscoinaddscript(const JSONRPCRequest& request) {
	const UniValue &params = request.params;
	if (request.fHelp || 1 != params.size())
		throw runtime_error("syscoinaddscript redeemscript\n"
			"Add redeemscript to local wallet for signing smart contract based syscoin transactions.\n");
    CWallet* const pwallet = GetDefaultWallet();
	std::vector<unsigned char> data(ParseHex(params[0].get_str()));
    if(pwallet)
	    pwallet->AddCScript(CScript(data.begin(), data.end()));
	UniValue res(UniValue::VOBJ);
	res.pushKV("result", "success");
	return res;
}
UniValue syscoinlistreceivedbyaddress(const JSONRPCRequest& request)
{
	const UniValue &params = request.params;
	if (request.fHelp || params.size() != 0)
		throw runtime_error(
			"syscoinlistreceivedbyaddress\n"
			"\nList balances by receiving address.\n"
			"\nResult:\n"
			"[\n"
			"  {\n"
			"    \"address\" : \"receivingaddress\",    (string) The receiving address\n"
			"    \"amount\" : x.xxx,					(numeric) The total amount in " + CURRENCY_UNIT + " received by the address\n"
			"    \"label\" : \"label\"                  (string) A comment for the address/transaction, if any\n"
			"  }\n"
			"  ,...\n"
			"]\n"

			"\nExamples:\n"
			+ HelpExampleCli("syscoinlistreceivedbyaddress", "")
		);

	return SyscoinListReceived(true, false);
}
string GetSyscoinTransactionDescription(const CTransaction& tx, const int op, string& responseEnglish, const char &type, string& responseGUID)
{
	if (tx.IsNull()) {
		return "Null Tx";
	}
	string strResponse = "";

	if (type == OP_SYSCOIN_ASSET) {
		// message from op code
		if (op == OP_ASSET_ACTIVATE) {
			strResponse = _("Asset Activated");
			responseEnglish = "Asset Activated";
		}
		else
			if (op == OP_ASSET_UPDATE) {
				strResponse = _("Asset Updated");
				responseEnglish = "Asset Updated";
			}
			else if (op == OP_ASSET_TRANSFER) {
				strResponse = _("Asset Transferred");
				responseEnglish = "Asset Transferred";
			}
			else if (op == OP_ASSET_SEND) {
				strResponse = _("Asset Sent");
				responseEnglish = "Asset Sent";
			}
			if (op == OP_ASSET_SEND) {
				CAssetAllocation assetallocation(tx);
				if (!assetallocation.IsNull()) {
					responseGUID = assetallocation.assetAllocationTuple.ToString();
				}
			}
			else {
				CAsset asset(tx);
				if (!asset.IsNull()) {
					responseGUID = boost::lexical_cast<string>(asset.nAsset);
				}
			}
	}
	else if (type == OP_SYSCOIN_ASSET_ALLOCATION) {
		// message from op code
		if (op == OP_ASSET_ALLOCATION_SEND) {
			strResponse = _("Asset Allocation Sent");
			responseEnglish = "Asset Allocation Sent";
		}
		else if (op == OP_ASSET_ALLOCATION_BURN) {
			strResponse = _("Asset Allocation Burned");
			responseEnglish = "Asset Allocation Burned";
		}

		CAssetAllocation assetallocation(tx);
		if (!assetallocation.IsNull()) {
			responseGUID = assetallocation.assetAllocationTuple.ToString();
		}
	}
	else {
		strResponse = _("Unknown Op Type");
		responseEnglish = "Unknown Op Type";
		return strResponse + " " + string(1, type);
	}
	return strResponse + " " + responseGUID;
}
bool IsOutpointMature(const COutPoint& outpoint)
{
	Coin coin;
	GetUTXOCoin(outpoint, coin);
	if (coin.IsSpent())
		return false;
	int numConfirmationsNeeded = 0;
	if (coin.IsCoinBase())
		numConfirmationsNeeded = COINBASE_MATURITY - 1;

	if (coin.nHeight > -1 && chainActive.Tip())
		return (chainActive.Height() - coin.nHeight) >= numConfirmationsNeeded;

	// don't have chainActive or coin height is neg 1 or less
	return false;

}
void CAsset::Serialize( vector<unsigned char> &vchData) {
    CDataStream dsAsset(SER_NETWORK, PROTOCOL_VERSION);
    dsAsset << *this;
	vchData = vector<unsigned char>(dsAsset.begin(), dsAsset.end());

}
void CAssetDB::WriteAssetIndex(const CAsset& asset, const int& op) {
	if (gArgs.IsArgSet("-zmqpubassetrecord")) {
		UniValue oName(UniValue::VOBJ);
		if (BuildAssetIndexerJson(asset, oName)) {
			GetMainSignals().NotifySyscoinUpdate(oName.write().c_str(), "assetrecord");
		}
	}
}
bool GetAsset(const int &nAsset,
        CAsset& txPos) {
    if (passetdb == nullptr || !passetdb->ReadAsset(nAsset, txPos))
        return false;
    return true;
}
bool DecodeAndParseAssetTx(const CTransaction& tx, int& op,
		vector<vector<unsigned char> >& vvch, char &type)
{
	if (op == OP_ASSET_SEND)
		return false;
	CAsset asset;
	bool decode = DecodeAssetTx(tx, op, vvch);
	bool parse = asset.UnserializeFromTx(tx);
	if (decode&&parse) {
		type = OP_SYSCOIN_ASSET;
		return true;
	}
	return false;
}
bool DecodeAssetTx(const CTransaction& tx, int& op,
        vector<vector<unsigned char> >& vvch) {
    bool found = false;


    // Strict check - bug disallowed
    for (unsigned int i = 0; i < tx.vout.size(); i++) {
        const CTxOut& out = tx.vout[i];
        vector<vector<unsigned char> > vvchRead;
        if (DecodeAssetScript(out.scriptPubKey, op, vvchRead)) {
            found = true; vvch = vvchRead;
            break;
        }
    }
    if (!found) vvch.clear();
    return found;
}


bool DecodeAssetScript(const CScript& script, int& op,
        vector<vector<unsigned char> > &vvch, CScript::const_iterator& pc) {
    opcodetype opcode;
	vvch.clear();
    if (!script.GetOp(pc, opcode)) return false;
    if (opcode < OP_1 || opcode > OP_16) return false;
    op = CScript::DecodeOP_N(opcode);
	if (op != OP_SYSCOIN_ASSET)
		return false;
	if (!script.GetOp(pc, opcode))
		return false;
	if (opcode < OP_1 || opcode > OP_16)
		return false;
	op = CScript::DecodeOP_N(opcode);
	if (!IsAssetOp(op))
		return false;

	bool found = false;
	for (;;) {
		vector<unsigned char> vch;
		if (!script.GetOp(pc, opcode, vch))
			return false;
		if (opcode == OP_DROP || opcode == OP_2DROP)
		{
			found = true;
			break;
		}
		if (!(opcode >= 0 && opcode <= OP_PUSHDATA4))
			return false;
		vvch.emplace_back(std::move(vch));
	}

	// move the pc to after any DROP or NOP
	while (opcode == OP_DROP || opcode == OP_2DROP) {
		if (!script.GetOp(pc, opcode))
			break;
	}

	pc--;
	return found;
}
bool DecodeAssetScript(const CScript& script, int& op,
        vector<vector<unsigned char> > &vvch) {
    CScript::const_iterator pc = script.begin();
    return DecodeAssetScript(script, op, vvch, pc);
}
bool RemoveAssetScriptPrefix(const CScript& scriptIn, CScript& scriptOut) {
    int op;
    vector<vector<unsigned char> > vvch;
    CScript::const_iterator pc = scriptIn.begin();

    if (!DecodeAssetScript(scriptIn, op, vvch, pc))
		return false;
	scriptOut = CScript(pc, scriptIn.end());
	return true;
}
bool CheckAssetInputs(const CTransaction &tx, const CCoinsViewCache &inputs, int op, const vector<vector<unsigned char> > &vvchArgs,
        bool fJustCheck, int nHeight, AssetMap& mapAssets, AssetAllocationMap &mapAssetAllocations, AssetBalanceMap &blockMapAssetBalances, string &errorMessage, bool bSanityCheck) {
	if (passetdb == nullptr)
		return false;
	if (tx.IsCoinBase() && !fJustCheck && !bSanityCheck)
	{
		LogPrintf("*Trying to add asset in coinbase transaction, skipping...");
		return false;
	}
	const uint256& txHash = tx.GetHash();
	if (!bSanityCheck)
		LogPrint(BCLog::SYS, "*** ASSET %d %d %s %s\n", nHeight,
			chainActive.Tip()->nHeight, txHash.ToString().c_str(),
			fJustCheck ? "JUSTCHECK" : "BLOCK");

	// unserialize asset from txn, check for valid
	CAsset theAsset;
	CAssetAllocation theAssetAllocation;
	vector<unsigned char> vchData;
	vector<unsigned char> vchHash;
	int nDataOut, tmpOp;
	if(!GetSyscoinData(tx, vchData, vchHash, nDataOut, tmpOp) || (op != OP_ASSET_SEND && (tmpOp != OP_SYSCOIN_ASSET || !theAsset.UnserializeFromData(vchData, vchHash))) || (op == OP_ASSET_SEND && (tmpOp != OP_SYSCOIN_ASSET_ALLOCATION || !theAssetAllocation.UnserializeFromData(vchData, vchHash))))
	{
		errorMessage = "SYSCOIN_ASSET_CONSENSUS_ERROR ERRCODE: 2000 - " + _("Cannot unserialize data inside of this transaction relating to an asset");
		return error(errorMessage.c_str());
	}

	if(fJustCheck)
	{
		if(vvchArgs.size() != 1)
		{
			errorMessage = "SYSCOIN_ASSET_CONSENSUS_ERROR: ERRCODE: 2001 - " + _("Asset arguments incorrect size");
			return error(errorMessage.c_str());
		}

					
		if(vchHash != vvchArgs[0])
		{
			errorMessage = "SYSCOIN_ASSET_CONSENSUS_ERROR: ERRCODE: 2002 - " + _("Hash provided doesn't match the calculated hash of the data");
			return error(errorMessage.c_str());
		}
			
	}

	string retError = "";
	if(fJustCheck)
	{
		if (op != OP_ASSET_SEND) {
			if (theAsset.vchPubData.size() > MAX_VALUE_LENGTH)
			{
				errorMessage = "SYSCOIN_ASSET_CONSENSUS_ERROR: ERRCODE: 2004 - " + _("Asset public data too big");
				return error(errorMessage.c_str());
			}
		}
		switch (op) {
		case OP_ASSET_ACTIVATE:
			if (theAsset.nAsset <= SYSCOIN_TX_VERSION_MINT)
			{
				errorMessage = "SYSCOIN_ASSET_CONSENSUS_ERROR: ERRCODE: 2005 - " + _("asset guid invalid");
				return error(errorMessage.c_str());
			}
            if (!theAsset.vchContract.empty() && theAsset.vchContract.size() != MAX_GUID_LENGTH)
            {
                errorMessage = "SYSCOIN_ASSET_CONSENSUS_ERROR: ERRCODE: 2005 - " + _("Contract address not proper size");
                return error(errorMessage.c_str());
            }  
			if (theAsset.nPrecision > 8)
			{
				errorMessage = "SYSCOIN_ASSET_CONSENSUS_ERROR: ERRCODE: 2013 - " + _("Precision must be between 0 and 8");
				return error(errorMessage.c_str());
			}
			if (theAsset.nMaxSupply != -1 && !AssetRange(theAsset.nMaxSupply, theAsset.nPrecision))
			{
				errorMessage = "SYSCOIN_ASSET_CONSENSUS_ERROR: ERRCODE: 2014 - " + _("Max supply out of money range");
				return error(errorMessage.c_str());
			}
			if (theAsset.nBalance > theAsset.nMaxSupply)
			{
				errorMessage = "SYSCOIN_ASSET_CONSENSUS_ERROR: ERRCODE: 2015 - " + _("Total supply cannot exceed maximum supply");
				return error(errorMessage.c_str());
			}
            if (theAsset.vchAddress.empty())
            {
                errorMessage = "SYSCOIN_ASSET_CONSENSUS_ERROR: ERRCODE: 2015 - " + _("No address specified as owner in the transaction payload");
                return error(errorMessage.c_str());
            }
			break;

		case OP_ASSET_UPDATE:
			if (theAsset.nBalance < 0)
			{
				errorMessage = "SYSCOIN_ASSET_CONSENSUS_ERROR: ERRCODE: 2017 - " + _("Balance must be greator than or equal to 0");
				return error(errorMessage.c_str());
			}
            if (!theAssetAllocation.IsNull())
            {
                errorMessage = "SYSCOIN_ASSET_CONSENSUS_ERROR: ERRCODE: 2019 - " + _("Cannot update allocations");
                return error(errorMessage.c_str());
            }
            if (!theAsset.vchContract.empty() && theAsset.vchContract.size() != MAX_GUID_LENGTH)
            {
                errorMessage = "SYSCOIN_ASSET_CONSENSUS_ERROR: ERRCODE: 2005 - " + _("Contract address not proper size");
                return error(errorMessage.c_str());
            }    
			break;
            
		case OP_ASSET_SEND:
			if (theAssetAllocation.listSendingAllocationAmounts.empty())
			{
				errorMessage = "SYSCOIN_ASSET_CONSENSUS_ERROR: ERRCODE: 2020 - " + _("Asset send must send an input or transfer balance");
				return error(errorMessage.c_str());
			}
			if (theAssetAllocation.listSendingAllocationAmounts.size() > 250)
			{
				errorMessage = "SYSCOIN_ASSET_CONSENSUS_ERROR: ERRCODE: 2021 - " + _("Too many receivers in one allocation send, maximum of 250 is allowed at once");
				return error(errorMessage.c_str());
			}
			break;
        case OP_ASSET_TRANSFER:
            break;
		default:
			errorMessage = "SYSCOIN_ASSET_CONSENSUS_ERROR: ERRCODE: 2023 - " + _("Asset transaction has unknown op");
			return error(errorMessage.c_str());
		}
	}
	if (!fJustCheck) {
		string strResponseEnglish = "";
		string strResponseGUID = "";
		CTransaction txTmp;
		GetSyscoinTransactionDescription(txTmp, op, strResponseEnglish, OP_SYSCOIN_ASSET, strResponseGUID);
		CAsset dbAsset;
		if (!GetAsset(op == OP_ASSET_SEND ? theAssetAllocation.assetAllocationTuple.nAsset : theAsset.nAsset, dbAsset))
		{
			if (op != OP_ASSET_ACTIVATE) {
				errorMessage = "SYSCOIN_ASSET_CONSENSUS_ERROR: ERRCODE: 2024 - " + _("Failed to read from asset DB");
				return error(errorMessage.c_str());
			}
		}
		const string &vchOwner = op == OP_ASSET_SEND ? bech32::Encode(Params().Bech32HRP(),theAssetAllocation.assetAllocationTuple.vchAddress) : bech32::Encode(Params().Bech32HRP(),theAsset.vchAddress);
		const string &user1 = dbAsset.IsNull()? vchOwner : bech32::Encode(Params().Bech32HRP(),dbAsset.vchAddress);
		string user2 = "";
		string user3 = "";
		if (op == OP_ASSET_TRANSFER) {
			user2 = vchOwner;
            if (!FindAssetOwnerInTx(inputs, tx, user1))
            {
                errorMessage = "SYSCOIN_ASSET_CONSENSUS_ERROR: ERRCODE: 1015 - " + _("Cannot transfer this asset. Asset owner must sign off on this change");
                return error(errorMessage.c_str());
            }           
		}

		if (op == OP_ASSET_UPDATE) {
			if (!FindAssetOwnerInTx(inputs, tx, user1))
			{
				errorMessage = "SYSCOIN_ASSET_CONSENSUS_ERROR: ERRCODE: 1015 - " + _("Cannot update this asset. Asset owner must sign off on this change");
				return error(errorMessage.c_str());
			}
			CAmount increaseBalanceByAmount = theAsset.nBalance;
			if (increaseBalanceByAmount > 0 && !(dbAsset.nUpdateFlags & ASSET_UPDATE_SUPPLY))
			{
				errorMessage = "SYSCOIN_ASSET_CONSENSUS_ERROR: ERRCODE: 2026 - " + _("Insufficient privileges to update supply");
				return error(errorMessage.c_str());
			}
			theAsset.nBalance = dbAsset.nBalance;
			
			theAsset.nBalance += increaseBalanceByAmount;
			// increase total supply
			theAsset.nTotalSupply += increaseBalanceByAmount;
			if (!AssetRange(theAsset.nTotalSupply, dbAsset.nPrecision))
			{
				errorMessage = "SYSCOIN_ASSET_CONSENSUS_ERROR: ERRCODE: 2029 - " + _("Total supply out of money range");
				return error(errorMessage.c_str());
			}
			if (theAsset.nTotalSupply > dbAsset.nMaxSupply)
			{
				errorMessage = "SYSCOIN_ASSET_CONSENSUS_ERROR: ERRCODE: 2030 - " + _("Total supply cannot exceed maximum supply");
				return error(errorMessage.c_str());
			}

		}
		else if (op != OP_ASSET_ACTIVATE) {
			// these fields cannot change after activation
			theAsset.nBalance = dbAsset.nBalance;
			theAsset.nTotalSupply = dbAsset.nBalance;
			theAsset.nMaxSupply = dbAsset.nMaxSupply;
		}

		if (op == OP_ASSET_SEND) {
			if (dbAsset.vchAddress != theAssetAllocation.assetAllocationTuple.vchAddress || !FindAssetOwnerInTx(inputs, tx, user1))
			{
				errorMessage = "SYSCOIN_ASSET_CONSENSUS_ERROR: ERRCODE: 1015 - " + _("Cannot send this asset. Asset owner must sign off on this change");
				return error(errorMessage.c_str());
			}
			theAsset = dbAsset;
			
			// check balance is sufficient on sender
			CAmount nTotal = 0;
			for (auto& amountTuple : theAssetAllocation.listSendingAllocationAmounts) {
				nTotal += amountTuple.second;
				if (amountTuple.second <= 0)
				{
					errorMessage = "SYSCOIN_ASSET_CONSENSUS_ERROR: ERRCODE: 2032 - " + _("Receiving amount must be positive");
					return error(errorMessage.c_str());
				}
			}
			if (theAsset.nBalance < nTotal) {
				errorMessage = "SYSCOIN_ASSET_CONSENSUS_ERROR: ERRCODE: 2033 - " + _("Sender balance is insufficient");
				return error(errorMessage.c_str());
			}
			for (auto& amountTuple : theAssetAllocation.listSendingAllocationAmounts) {
				if (!bSanityCheck) {
                    
					CAssetAllocation receiverAllocation;
					const CAssetAllocationTuple receiverAllocationTuple(theAssetAllocation.assetAllocationTuple.nAsset, amountTuple.first);
                    const string& receiverTupleStr = receiverAllocationTuple.ToString();
					// don't need to check for existance of allocation because it may not exist, may be creating it here for the first time for receiver
					GetAssetAllocation(receiverAllocationTuple, receiverAllocation);
					if (receiverAllocation.IsNull()) {
						receiverAllocation.assetAllocationTuple = receiverAllocationTuple;
					} 
                    
                    AssetBalanceMap::iterator mapBalanceReceiver = blockMapAssetBalances.find(receiverTupleStr);
                    if(mapBalanceReceiver == blockMapAssetBalances.end()){
                        receiverAllocation.nBalance += amountTuple.second;
                        blockMapAssetBalances.emplace(std::move(receiverTupleStr), receiverAllocation.nBalance); 
                    }
                    else{
                        mapBalanceReceiver->second += amountTuple.second;
                        receiverAllocation.nBalance = mapBalanceReceiver->second;
                    } 
                                            
					receiverAllocation.txHash = txHash;
					const string& receiverAddress = bech32::Encode(Params().Bech32HRP(),receiverAllocation.assetAllocationTuple.vchAddress);
					// adjust sender balance
					theAsset.nBalance -= amountTuple.second;
                    passetallocationdb->WriteAssetAllocationIndex(receiverAllocation, nHeight, dbAsset, dbAsset.nBalance - nTotal, amountTuple.second, user1, receiverAddress);
                    auto it = mapAssetAllocations.find(receiverTupleStr);
                    if( it != mapAssetAllocations.end() ) {
                        it->second = std::move(receiverAllocation);
                    }
                    else {
                       mapAssetAllocations.emplace(std::move(receiverTupleStr), std::move(receiverAllocation));
                    }                                        
				}
			}
		}
		else if (op != OP_ASSET_ACTIVATE)
		{
			// these fields cannot change after activation

			theAsset.nPrecision = dbAsset.nPrecision;
			if (theAsset.vchAddress.empty())
				theAsset.vchAddress = dbAsset.vchAddress;
			if (theAsset.vchPubData.empty())
				theAsset.vchPubData = dbAsset.vchPubData;
			else if (!(dbAsset.nUpdateFlags & ASSET_UPDATE_DATA))
			{
				errorMessage = "SYSCOIN_ASSET_CONSENSUS_ERROR: ERRCODE: 2026 - " + _("Insufficient privileges to update public data");
				return error(errorMessage.c_str());
			}
			if (theAsset.vchContract.empty())
				theAsset.vchContract = dbAsset.vchContract;				
			else if (!(dbAsset.nUpdateFlags & ASSET_UPDATE_CONTRACT))
			{
				errorMessage = "SYSCOIN_ASSET_CONSENSUS_ERROR: ERRCODE: 2026 - " + _("Insufficient privileges to update smart contract");
				return error(errorMessage.c_str());
			}
			if (theAsset.nUpdateFlags != dbAsset.nUpdateFlags && (!(dbAsset.nUpdateFlags & (ASSET_UPDATE_FLAGS | ASSET_UPDATE_ADMIN)))) {
				errorMessage = "SYSCOIN_ASSET_CONSENSUS_ERROR: ERRCODE: 2040 - " + _("Insufficient privileges to update flags");
				return error(errorMessage.c_str());
			}
			if (op == OP_ASSET_TRANSFER)
			{
				// cannot adjust these upon transfer
				theAsset.vchContract = dbAsset.vchContract;
			}

		}
		if (op == OP_ASSET_ACTIVATE)
		{
			if (!FindAssetOwnerInTx(inputs, tx, user1))
			{
				errorMessage = "SYSCOIN_ASSET_CONSENSUS_ERROR: ERRCODE: 1015 - " + _("Cannot create this asset. Asset owner must sign off on this change");
				return error(errorMessage.c_str());
			}
			if (GetAsset(theAsset.nAsset, theAsset))
			{
				errorMessage = "SYSCOIN_ASSET_CONSENSUS_ERROR: ERRCODE: 2041 - " + _("Asset already exists");
				return error(errorMessage.c_str());
			}
			// starting supply is the supplied balance upon init
			theAsset.nTotalSupply = theAsset.nBalance;
			// with input ranges precision is forced to 0
		}
		// set the asset's txn-dependent values
		theAsset.txHash = txHash;
		// write asset, if asset send, only write on pow since asset -> asset allocation is not 0-conf compatible
		if (!bSanityCheck) {
            passetdb->WriteAssetIndex(theAsset, op); 
            auto it = mapAssets.find(theAsset.nAsset);
            if( it != mapAssets.end() ) {
                it->second = std::move(theAsset);
            }
            else {
                mapAssets.emplace(std::move(theAsset.nAsset), std::move(theAsset));
            }
			// debug
			
			LogPrint(BCLog::SYS,"CONNECTED ASSET: op=%s symbol=%d hash=%s height=%d fJustCheck=%d\n",
					assetFromOp(op).c_str(),
					theAsset.nAsset,
					txHash.ToString().c_str(),
					nHeight,
					fJustCheck ? 1 : 0);
		}
	}
    return true;
}

UniValue assetnew(const JSONRPCRequest& request) {
    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    CWallet* const pwallet = wallet.get();
	const UniValue &params = request.params;
    if (request.fHelp || params.size() != 8)
        throw runtime_error(
			"assetnew [owner] [public value] [contract] [precision=8] [supply] [max_supply] [update_flags] [witness]\n"
						"<owner> An address that you own.\n"
                        "<public value> public data, 256 characters max.\n"
                        "<contract> Ethereum token contract for SyscoinX bridge. Must be in hex and not include the '0x' format tag. For example contract '0xb060ddb93707d2bc2f8bcc39451a5a28852f8d1d' should be set as 'b060ddb93707d2bc2f8bcc39451a5a28852f8d1d'\n"  
						"<precision> Precision of balances. Must be between 0 and 8. The lower it is the higher possible max_supply is available since the supply is represented as a 64 bit integer. With a precision of 8 the max supply is 10 billion.\n"
						"<supply> Initial supply of asset. Can mint more supply up to total_supply amount or if total_supply is -1 then minting is uncapped.\n"
						"<max_supply> Maximum supply of this asset. Set to -1 for uncapped. Depends on the precision value that is set, the lower the precision the higher max_supply can be.\n"
						"<update_flags> Ability to update certain fields. Must be decimal value which is a bitmask for certain rights to update. The bitmask represents 0x01(1) to give admin status (needed to update flags), 0x10(2) for updating public data field, 0x100(4) for updating the smart contract field, 0x1000(8) for updating supply, 0x10000(16) for being able to update flags (need admin access to update flags as well)\n"
						"<witness> Witness address that will sign for web-of-trust notarization of this transaction.\n"
						+ HelpRequiringPassphrase(pwallet));
	string vchAddress = params[0].get_str();
	vector<unsigned char> vchPubData = vchFromString(params[1].get_str());
    string strContract = params[2].get_str();
    if(!strContract.empty())
        boost::remove_erase_if(strContract, boost::is_any_of("0x")); // strip 0x in hex str if exist
    vector<unsigned char> vchContract = ParseHex(strContract);
	int precision = params[3].get_int();
	string vchWitness;
	UniValue param4 = params[4];
	UniValue param5 = params[5];
	CAmount nBalance = AssetAmountFromValue(param4, precision);
	CAmount nMaxSupply = AssetAmountFromValue(param5, precision);
	int nUpdateFlags = params[6].get_int();
	vchWitness = params[7].get_str();

	string strAddressFrom;
	string strAddress = vchAddress;
	const CTxDestination address = DecodeDestination(strAddress);
	if (IsValidDestination(address)) {
		strAddressFrom = strAddress;
	}

	CScript scriptPubKeyFromOrig;
	if (!strAddressFrom.empty()) {
		scriptPubKeyFromOrig = GetScriptForDestination(address);
	}
	
    CScript scriptPubKey;

	// calculate net
    // build asset object
    CAsset newAsset;
	newAsset.nAsset = GenerateSyscoinGuid();
	newAsset.vchPubData = vchPubData;
    newAsset.vchContract = vchContract;
	newAsset.vchAddress = bech32::Decode(strAddress).second;
	newAsset.nBalance = nBalance;
	newAsset.nMaxSupply = nMaxSupply;
	newAsset.nPrecision = precision;
	newAsset.nUpdateFlags = nUpdateFlags;
	vector<unsigned char> data;
	newAsset.Serialize(data);
    uint256 hash = Hash(data.begin(), data.end());
 	
    vector<unsigned char> vchHashAsset = vector<unsigned char>(hash.begin(), hash.end());

    scriptPubKey << CScript::EncodeOP_N(OP_SYSCOIN_ASSET) << CScript::EncodeOP_N(OP_ASSET_ACTIVATE) << vchHashAsset << OP_2DROP << OP_DROP;
    scriptPubKey += scriptPubKeyFromOrig;

	// use the script pub key to create the vecsend which sendmoney takes and puts it into vout
	vector<CRecipient> vecSend;
	CRecipient recipient;
	CreateRecipient(scriptPubKey, recipient);
	vecSend.push_back(recipient);


	CScript scriptData;
	scriptData << OP_RETURN << CScript::EncodeOP_N(OP_SYSCOIN_ASSET) << data;
	CRecipient fee;
	CreateFeeRecipient(scriptData, data, fee);
	vecSend.push_back(fee);
	UniValue res = syscointxfund_helper(vchWitness, vecSend);
	res.push_back(newAsset.nAsset);
	return res;
}

UniValue assetupdate(const JSONRPCRequest& request) {
    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    CWallet* const pwallet = wallet.get();
	const UniValue &params = request.params;
    if (request.fHelp || params.size() != 6)
        throw runtime_error(
			"assetupdate [asset] [public value] [contract] [supply] [update_flags] [witness]\n"
						"Perform an update on an asset you control.\n"
						"<asset> Asset guid.\n"
                        "<public value> Public data, 256 characters max.\n"
                        "<contract> Ethereum token contract for SyscoinX bridge. Must be in hex and not include the '0x' format tag. For example contract '0xb060ddb93707d2bc2f8bcc39451a5a28852f8d1d' should be set as 'b060ddb93707d2bc2f8bcc39451a5a28852f8d1d'\n"              
						"<supply> New supply of asset. Can mint more supply up to total_supply amount or if max_supply is -1 then minting is uncapped. If greator than zero, minting is assumed otherwise set to 0 to not mint any additional tokens.\n"
                        "<update_flags> Ability to update certain fields. Must be decimal value which is a bitmask for certain rights to update. The bitmask represents 0x01(1) to give admin status (needed to update flags), 0x10(2) for updating public data field, 0x100(4) for updating the smart contract field, 0x1000(8) for updating supply, 0x10000(16) for being able to update flags (need admin access to update flags as well)\n"
                        "<witness> Witness address that will sign for web-of-trust notarization of this transaction.\n"
						+ HelpRequiringPassphrase(pwallet));
	const int &nAsset = params[0].get_int();
	string strData = "";
	string strPubData = "";
	string strCategory = "";
	strPubData = params[1].get_str();
    string strContract = params[2].get_str();
    if(!strContract.empty())
        boost::remove_erase_if(strContract, boost::is_any_of("0x")); // strip 0x in hex str if exists
    vector<unsigned char> vchContract = ParseHex(strContract);

	int nUpdateFlags = params[4].get_int();
	string vchWitness;
	vchWitness = params[5].get_str();

    CScript scriptPubKeyFromOrig;
	CAsset theAsset;

    if (!GetAsset( nAsset, theAsset))
        throw runtime_error("SYSCOIN_ASSET_RPC_ERROR: ERRCODE: 2501 - " + _("Could not find a asset with this key"));

	string strAddressFrom;
	const string& strAddress = bech32::Encode(Params().Bech32HRP(),theAsset.vchAddress);
	const CTxDestination &address = DecodeDestination(strAddress);
	if (IsValidDestination(address)) {
		strAddressFrom = strAddress;
	}

	UniValue param3 = params[3];
	CAmount nBalance = 0;
	if(param3.get_str() != "0")
		nBalance = AssetAmountFromValue(param3, theAsset.nPrecision);
	
	
	if (!strAddressFrom.empty()) {
		scriptPubKeyFromOrig = GetScriptForDestination(address);
	}

	CAsset copyAsset = theAsset;
	theAsset.ClearAsset();

    // create ASSETUPDATE txn keys
    CScript scriptPubKey;

	if(strPubData != stringFromVch(copyAsset.vchPubData))
		theAsset.vchPubData = vchFromString(strPubData);
    if(vchContract != copyAsset.vchContract)
        theAsset.vchContract = vchContract;


	theAsset.nBalance = nBalance;
	theAsset.nUpdateFlags = nUpdateFlags;

	vector<unsigned char> data;
	theAsset.Serialize(data);
    uint256 hash = Hash(data.begin(), data.end());
 	
    vector<unsigned char> vchHashAsset = vector<unsigned char>(hash.begin(), hash.end());
    scriptPubKey << CScript::EncodeOP_N(OP_SYSCOIN_ASSET) << CScript::EncodeOP_N(OP_ASSET_UPDATE) << vchHashAsset << OP_2DROP << OP_DROP;
    scriptPubKey += scriptPubKeyFromOrig;

	vector<CRecipient> vecSend;
	CRecipient recipient;
	CreateRecipient(scriptPubKey, recipient);
	vecSend.push_back(recipient);


	CScript scriptData;
	scriptData << OP_RETURN << CScript::EncodeOP_N(OP_SYSCOIN_ASSET) << data;
	CRecipient fee;
	CreateFeeRecipient(scriptData, data, fee);
	vecSend.push_back(fee);
	return syscointxfund_helper(vchWitness, vecSend);
}

UniValue assettransfer(const JSONRPCRequest& request) {
    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    CWallet* const pwallet = wallet.get();
	const UniValue &params = request.params;
    if (request.fHelp || params.size() != 3)
        throw runtime_error(
			"assettransfer [asset] [ownerto] [witness]\n"
						"Transfer a asset allocation you own to another address.\n"
						"<asset> Asset guid.\n"
						"<ownerto> Address to transfer to.\n"
						"<witness> Witness address that will sign for web-of-trust notarization of this transaction.\n"	
						+ HelpRequiringPassphrase(pwallet));

    // gather & validate inputs
	const int &nAsset = params[0].get_int();
	string vchAddressTo = params[1].get_str();
	string vchWitness;
	vchWitness = params[2].get_str();

    CScript scriptPubKeyOrig, scriptPubKeyFromOrig;
	CAsset theAsset;
    if (!GetAsset( nAsset, theAsset))
        throw runtime_error("SYSCOIN_ASSET_RPC_ERROR: ERRCODE: 2505 - " + _("Could not find a asset with this key"));
	
	string strAddressFrom;
	const string& strAddress = bech32::Encode(Params().Bech32HRP(),theAsset.vchAddress);
	const CTxDestination addressFrom = DecodeDestination(strAddress);
	if (IsValidDestination(addressFrom)) {
		strAddressFrom = strAddress;
	}


	const CTxDestination addressTo = DecodeDestination(vchAddressTo);
	if (IsValidDestination(addressTo)) {
		scriptPubKeyOrig = GetScriptForDestination(addressTo);
	}


	if (!strAddressFrom.empty()) {
		scriptPubKeyFromOrig = GetScriptForDestination(addressFrom);
	}

	CAsset copyAsset = theAsset;
	theAsset.ClearAsset();
    CScript scriptPubKey;
	theAsset.vchAddress = bech32::Decode(vchAddressTo).second;

	vector<unsigned char> data;
	theAsset.Serialize(data);
    uint256 hash = Hash(data.begin(), data.end());
 	
    vector<unsigned char> vchHashAsset = vector<unsigned char>(hash.begin(), hash.end());
    scriptPubKey << CScript::EncodeOP_N(OP_SYSCOIN_ASSET) << CScript::EncodeOP_N(OP_ASSET_TRANSFER) << vchHashAsset << OP_2DROP << OP_DROP;
	scriptPubKey += scriptPubKeyOrig;
    // send the asset pay txn
	vector<CRecipient> vecSend;
	CRecipient recipient;
	CreateRecipient(scriptPubKey, recipient);
	vecSend.push_back(recipient);


	CScript scriptData;
	scriptData << OP_RETURN << CScript::EncodeOP_N(OP_SYSCOIN_ASSET) << data;
	CRecipient fee;
	CreateFeeRecipient(scriptData, data, fee);
	vecSend.push_back(fee);
	return syscointxfund_helper(vchWitness, vecSend);
}
UniValue assetsend(const JSONRPCRequest& request) {
    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    CWallet* const pwallet = wallet.get();
	const UniValue &params = request.params;
	if (request.fHelp || params.size() != 3)
		throw runtime_error(
			"assetsend [asset] ([{\"ownerto\":\"address\",\"amount\":amount},...] [witness]\n"
			"Send an asset you own to another address/address as an asset allocation. Maximimum recipients is 250.\n"
			"<asset> Asset guid.\n"
			"<owner> Address that owns this asset allocation.\n"
			"<ownerto> Address to transfer to.\n"
			"<amount> Quantity of asset to send.\n"
			"<witness> Witness address that will sign for web-of-trust notarization of this transaction.\n"
			+ HelpRequiringPassphrase(pwallet));
	// gather & validate inputs
	const int &nAsset = params[0].get_int();
	UniValue valueTo = params[1];
	string vchWitness = params[2].get_str();
	if (!valueTo.isArray())
		throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Array of receivers not found");

	CAsset theAsset;
	if (!GetAsset(nAsset, theAsset))
		throw runtime_error("SYSCOIN_ASSET_RPC_ERROR: ERRCODE: 2507 - " + _("Could not find a asset with this key"));

	string strAddressFrom;
	const string& strAddress = bech32::Encode(Params().Bech32HRP(),theAsset.vchAddress);
	const CTxDestination addressFrom = DecodeDestination(strAddress);
	if (IsValidDestination(addressFrom)) {
		strAddressFrom = strAddress;
	}


	CScript scriptPubKeyFromOrig;

	if (!strAddressFrom.empty()) {
		scriptPubKeyFromOrig = GetScriptForDestination(addressFrom);
	}


	CAssetAllocation theAssetAllocation;
	theAssetAllocation.assetAllocationTuple = CAssetAllocationTuple(nAsset, theAsset.vchAddress);

	UniValue receivers = valueTo.get_array();
	for (unsigned int idx = 0; idx < receivers.size(); idx++) {
		const UniValue& receiver = receivers[idx];
		if (!receiver.isObject())
			throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "expected object with {\"ownerto'\", or \"amount\"}");

		UniValue receiverObj = receiver.get_obj();
		string toStr = find_value(receiverObj, "ownerto").get_str();
        CTxDestination dest = DecodeDestination(toStr);
		if(!IsValidDestination(dest))
			throw runtime_error("SYSCOIN_ASSET_RPC_ERROR: ERRCODE: 2509 - " + _("Asset must be sent to a valid syscoin address"));
	    vector<uint8_t> vchAddressTo = bech32::Decode(toStr).second;
		
		UniValue amountObj = find_value(receiverObj, "amount");
		if (amountObj.isNum()) {
			const CAmount &amount = AssetAmountFromValue(amountObj, theAsset.nPrecision);
			if (amount <= 0)
				throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "amount must be positive");
			theAssetAllocation.listSendingAllocationAmounts.push_back(make_pair(vchAddressTo, amount));
		}
		else
			throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "expected amount as number in receiver array");

	}

	CScript scriptPubKey;

    vector<unsigned char> data;
    theAssetAllocation.Serialize(data);
	uint256 hash = Hash(data.begin(), data.end());
    vector<unsigned char> vchHashAsset = vector<unsigned char>(hash.begin(), hash.end());  
	scriptPubKey << CScript::EncodeOP_N(OP_SYSCOIN_ASSET) << CScript::EncodeOP_N(OP_ASSET_SEND) << vchHashAsset << OP_2DROP << OP_DROP;
	scriptPubKey += scriptPubKeyFromOrig;
	// send the asset pay txn
	vector<CRecipient> vecSend;
	CRecipient recipient;
	CreateRecipient(scriptPubKey, recipient);
	vecSend.push_back(recipient);

	CScript scriptData;
	scriptData << OP_RETURN << CScript::EncodeOP_N(OP_SYSCOIN_ASSET_ALLOCATION) << data;
	CRecipient fee;
	CreateFeeRecipient(scriptData, data, fee);
	vecSend.push_back(fee);

	return syscointxfund_helper(vchWitness, vecSend);
}

UniValue assetinfo(const JSONRPCRequest& request) {
	const UniValue &params = request.params;
    if (request.fHelp || 1 != params.size())
        throw runtime_error("assetinfo <asset>\n"
                "Show stored values of a single asset and its.\n");

    const int &nAsset = params[0].get_int();
	UniValue oAsset(UniValue::VOBJ);

	CAsset txPos;
	if (passetdb == nullptr || !passetdb->ReadAsset(nAsset, txPos))
		throw runtime_error("SYSCOIN_ASSET_RPC_ERROR: ERRCODE: 2511 - " + _("Failed to read from asset DB"));

	if(!BuildAssetJson(txPos, oAsset))
		oAsset.clear();
    return oAsset;
}
bool BuildAssetJson(const CAsset& asset, UniValue& oAsset)
{
    oAsset.pushKV("_id", asset.nAsset);
    oAsset.pushKV("txid", asset.txHash.GetHex());
	oAsset.pushKV("publicvalue", stringFromVch(asset.vchPubData));
	oAsset.pushKV("owner", bech32::Encode(Params().Bech32HRP(), asset.vchAddress));
    oAsset.pushKV("contract", "0x"+HexStr(asset.vchContract));
	oAsset.pushKV("balance", ValueFromAssetAmount(asset.nBalance, asset.nPrecision));
	oAsset.pushKV("total_supply", ValueFromAssetAmount(asset.nTotalSupply, asset.nPrecision));
	oAsset.pushKV("max_supply", ValueFromAssetAmount(asset.nMaxSupply, asset.nPrecision));
	oAsset.pushKV("update_flags", asset.nUpdateFlags);
	oAsset.pushKV("precision", (int)asset.nPrecision);
	return true;
}
bool BuildAssetIndexerJson(const CAsset& asset, UniValue& oAsset)
{
	oAsset.pushKV("_id", asset.nAsset);
	oAsset.pushKV("owner", bech32::Encode(Params().Bech32HRP(), asset.vchAddress));
	oAsset.pushKV("balance", ValueFromAssetAmount(asset.nBalance, asset.nPrecision));
	oAsset.pushKV("total_supply", ValueFromAssetAmount(asset.nTotalSupply, asset.nPrecision));
	oAsset.pushKV("max_supply", ValueFromAssetAmount(asset.nMaxSupply, asset.nPrecision));
	oAsset.pushKV("precision", (int)asset.nPrecision);
	return true;
}
void AssetTxToJSON(const int op, const std::vector<unsigned char> &vchData, const std::vector<unsigned char> &vchHash, UniValue &entry)
{
	string opName = assetFromOp(op);
	CAsset asset;
	if(!asset.UnserializeFromData(vchData, vchHash))
		return;

	CAsset dbAsset;
	GetAsset(asset.nAsset, dbAsset);
	

	entry.pushKV("txtype", opName);
	entry.pushKV("_id", asset.nAsset);

	if(!asset.vchPubData.empty() && dbAsset.vchPubData != asset.vchPubData)
		entry.pushKV("publicvalue", stringFromVch(asset.vchPubData));
        
    if(!asset.vchContract.empty() && dbAsset.vchContract != asset.vchContract)
        entry.pushKV("contract", "0x"+HexStr(asset.vchContract));       
        
	if (!asset.vchAddress.empty() && dbAsset.vchAddress != asset.vchAddress)
		entry.pushKV("owner", bech32::Encode(Params().Bech32HRP(), asset.vchAddress));

	if (asset.nUpdateFlags != dbAsset.nUpdateFlags)
		entry.pushKV("update_flags", asset.nUpdateFlags);
              
	if (asset.nBalance != dbAsset.nBalance)
		entry.pushKV("balance", ValueFromAssetAmount(asset.nBalance, dbAsset.nPrecision));

	CAssetAllocation assetallocation;
	if (assetallocation.UnserializeFromData(vchData, vchHash)) {
		UniValue oAssetAllocationReceiversArray(UniValue::VARR);
		if (!assetallocation.listSendingAllocationAmounts.empty()) {
			for (auto& amountTuple : assetallocation.listSendingAllocationAmounts) {
				UniValue oAssetAllocationReceiversObj(UniValue::VOBJ);
				oAssetAllocationReceiversObj.pushKV("ownerto", bech32::Encode(Params().Bech32HRP(), amountTuple.first));
                oAssetAllocationReceiversObj.pushKV("amount", ValueFromAssetAmount(amountTuple.second, dbAsset.nPrecision));
				oAssetAllocationReceiversArray.push_back(oAssetAllocationReceiversObj);
			}

		}
		entry.pushKV("allocations", oAssetAllocationReceiversArray);
	}

}

UniValue ValueFromAssetAmount(const CAmount& amount,int precision)
{
	if (precision < 0 || precision > 8)
		throw JSONRPCError(RPC_TYPE_ERROR, "Precision must be between 0 and 8");
	bool sign = amount < 0;
	int64_t n_abs = (sign ? -amount : amount);
	int64_t quotient = n_abs;
	int64_t divByAmount = 1;
	int64_t remainder = 0;
	string strPrecision = "0";
	if (precision > 0) {
		divByAmount = powf(10, precision);
		quotient = n_abs / divByAmount;
		remainder = n_abs % divByAmount;
		strPrecision = boost::lexical_cast<string>(precision);
	}

	return UniValue(UniValue::VSTR,
		strprintf("%s%d.%0" + strPrecision + "d", sign ? "-" : "", quotient, remainder));
}
CAmount AssetAmountFromValue(UniValue& value, int precision)
{
	if(precision < 0 || precision > 8)
		throw JSONRPCError(RPC_TYPE_ERROR, "Precision must be between 0 and 8");
	if (!value.isNum() && !value.isStr())
		throw JSONRPCError(RPC_TYPE_ERROR, "Amount is not a number or string");
	if (value.isStr() && value.get_str() == "-1") {
		value.setInt((int64_t)(MAX_ASSET / ((int)powf(10, precision))));
	}
	CAmount amount;
	if (!ParseFixedPoint(value.getValStr(), precision, &amount))
		throw JSONRPCError(RPC_TYPE_ERROR, "Invalid amount");
	if (!AssetRange(amount))
		throw JSONRPCError(RPC_TYPE_ERROR, "Amount out of range");
	return amount;
}
CAmount AssetAmountFromValueNonNeg(const UniValue& value, int precision)
{
	if (precision < 0 || precision > 8)
		throw JSONRPCError(RPC_TYPE_ERROR, "Precision must be between 0 and 8");
	if (!value.isNum() && !value.isStr())
		throw JSONRPCError(RPC_TYPE_ERROR, "Amount is not a number or string");
	CAmount amount;
	if (!ParseFixedPoint(value.getValStr(), precision, &amount))
		throw JSONRPCError(RPC_TYPE_ERROR, "Invalid amount");
	if (!AssetRange(amount))
		throw JSONRPCError(RPC_TYPE_ERROR, "Amount out of range");
	return amount;
}
bool AssetRange(const CAmount& amount, int precision)
{

	if (precision < 0 || precision > 8)
		throw JSONRPCError(RPC_TYPE_ERROR, "Precision must be between 0 and 8");
	bool sign = amount < 0;
	int64_t n_abs = (sign ? -amount : amount);
	int64_t quotient = n_abs;
	if (precision > 0) {
		int64_t divByAmount = powf(10, precision);
		quotient = n_abs / divByAmount;
	}
	if (!AssetRange(quotient))
		return false;
	return true;
}
bool CAssetDB::Flush(const AssetMap &mapAssets){
    if(mapAssets.empty())
        return true;
    CDBBatch batch(*this);
    for (const auto &key : mapAssets) {
        const CAsset &asset = key.second;
        batch.Write(make_pair(std::string("ai"), asset.nAsset), asset);
    }
    LogPrint(BCLog::SYS, "Flushing %d assets\n", mapAssets.size());
    return WriteBatch(batch);
}
bool CAssetDB::ScanAssets(const int count, const int from, const UniValue& oOptions, UniValue& oRes) {
	string strTxid = "";
	vector<vector<uint8_t> > vchAddresses;
    int32_t nAsset;
	if (!oOptions.isNull()) {
		const UniValue &txid = find_value(oOptions, "txid");
		if (txid.isStr()) {
			strTxid = txid.get_str();
		}
		const UniValue &assetObj = find_value(oOptions, "asset");
		if (assetObj.isNum()) {
			nAsset = boost::lexical_cast<int32_t>(assetObj.get_int());
		}

		const UniValue &owners = find_value(oOptions, "owner");
		if (owners.isArray()) {
			const UniValue &ownersArray = owners.get_array();
			for (unsigned int i = 0; i < ownersArray.size(); i++) {
				const UniValue &owner = ownersArray[i].get_obj();
				const UniValue &ownerStr = find_value(owner, "owner");
				if (ownerStr.isStr()) {
					vchAddresses.push_back(bech32::Decode(owner.get_str()).second);
				}
			}
		}
	}
	boost::scoped_ptr<CDBIterator> pcursor(NewIterator());
	pcursor->SeekToFirst();
	CAsset txPos;
	pair<string, int32_t > key;
	int index = 0;
	while (pcursor->Valid()) {
		boost::this_thread::interruption_point();
		try {
			if (pcursor->GetKey(key) && key.first == "ai") {
				pcursor->GetValue(txPos);
				if (!strTxid.empty() && strTxid != txPos.txHash.GetHex())
				{
					pcursor->Next();
					continue;
				}
				if (nAsset > 0 && nAsset != txPos.nAsset)
				{
					pcursor->Next();
					continue;
				}
				if (!vchAddresses.empty() && std::find(vchAddresses.begin(), vchAddresses.end(), txPos.vchAddress) == vchAddresses.end())
				{
					pcursor->Next();
					continue;
				}
				UniValue oAsset(UniValue::VOBJ);
				if (!BuildAssetJson(txPos, oAsset))
				{
					pcursor->Next();
					continue;
				}
				index += 1;
				if (index <= from) {
					pcursor->Next();
					continue;
				}
				oRes.push_back(oAsset);
				if (index >= count + from)
					break;
			}
			pcursor->Next();
		}
		catch (std::exception &e) {
			return error("%s() : deserialize error", __PRETTY_FUNCTION__);
		}
	}
	return true;
}
UniValue listassets(const JSONRPCRequest& request) {
	const UniValue &params = request.params;
	if (request.fHelp || 3 < params.size())
		throw runtime_error("listassets [count] [from] [{options}]\n"
			"scan through all assets.\n"
			"[count]          (numeric, optional, default=10) The number of results to return.\n"
			"[from]           (numeric, optional, default=0) The number of results to skip.\n"
			"[options]        (object, optional) A json object with options to filter results\n"
			"    {\n"
			"      \"txid\":txid					(string) Transaction ID to filter results for\n"
			"	   \"asset\":guid					(number) Asset GUID to filter.\n"
			"	   \"owners\"						(array) a json array with owners\n"
			"		[\n"
			"			{\n"
			"				\"owner\":string		(string) Address to filter.\n"
			"			} \n"
			"			,...\n"
			"		]\n"
			"    }\n"
			+ HelpExampleCli("listassets", "0")
			+ HelpExampleCli("listassets", "10 10")
			+ HelpExampleCli("listassets", "0 0 '{\"owners\":[{\"owner\":\"SfaMwYY19Dh96B9qQcJQuiNykVRTzXMsZR\"},{\"owner\":\"SfaMwYY19Dh96B9qQcJQuiNykVRTzXMsZR\"}]}'")
			+ HelpExampleCli("listassets", "0 0 '{\"asset\":3473733,\"owner\":\"SfaT8dGhk1zaQkk8bujMfgWw3szxReej4S\"0}'")
		);
	UniValue options;
	int count = 10;
	int from = 0;
	if (params.size() > 0) {
		count = params[0].get_int();
		if (count == 0) {
			count = 10;
		} else
		if (count < 0) {
			throw runtime_error("SYSCOIN_ASSET_RPC_ERROR: ERRCODE: 2512 - " + _("'count' must be 0 or greater"));
		}
	}
	if (params.size() > 1) {
		from = params[1].get_int();
		if (from < 0) {
			throw runtime_error("SYSCOIN_ASSET_RPC_ERROR: ERRCODE: 2512 - " + _("'from' must be 0 or greater"));
		}
	}
	if (params.size() > 2) {
		options = params[2];
	}

	UniValue oRes(UniValue::VARR);
	if (!passetdb->ScanAssets(count, from, options, oRes))
		throw runtime_error("SYSCOIN_ASSET_RPC_ERROR: ERRCODE: 2512 - " + _("Scan failed"));
	return oRes;
}
