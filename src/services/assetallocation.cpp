// Copyright (c) 2017-2018 The Syscoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "services/assetallocation.h"
#include "services/asset.h"
#include "init.h"
#include "validation.h"
#include "txmempool.h"
#include "util.h"
#include "random.h"
#include "base58.h"
#include "core_io.h"
#include "rpc/server.h"
#include "wallet/wallet.h"
#include "chainparams.h"
#include "wallet/coincontrol.h"
#include <boost/algorithm/hex.hpp>
#include <boost/algorithm/string/case_conv.hpp> // for to_lower()
#include <boost/foreach.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/thread.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/range/adaptor/reversed.hpp>
#include <boost/algorithm/string.hpp>
#include <future>
#include <boost/multiprecision/cpp_dec_float.hpp>
#include <key_io.h>
#include <services/ranges.h>
#include <bech32.h>
using namespace std;
using namespace boost::multiprecision;
AssetAllocationIndexItemMap AssetAllocationIndex;
bool IsAssetAllocationOp(int op) {
	return op == OP_ASSET_ALLOCATION_SEND || op == OP_ASSET_COLLECT_INTEREST || op == OP_ASSET_ALLOCATION_BURN;
}
string CAssetAllocationTuple::ToString() const {
	return HexStr(vchAsset) + "-" + bech32::Encode(Params().Bech32HRP(),vchAddress);
}
string assetAllocationFromOp(int op) {
    switch (op) {
	case OP_ASSET_SEND:
		return "assetsend";
	case OP_ASSET_ALLOCATION_SEND:
		return "assetallocationsend";
	case OP_ASSET_COLLECT_INTEREST:
		return "assetallocationcollectinterest";
	case OP_ASSET_ALLOCATION_BURN:
		return "assetallocationburn";
    default:
        return "<unknown assetallocation op>";
    }
}
bool CAssetAllocation::UnserializeFromData(const vector<unsigned char> &vchData, const vector<unsigned char> &vchHash) {
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
bool CAssetAllocation::UnserializeFromTx(const CTransaction &tx) {
	vector<unsigned char> vchData;
	vector<unsigned char> vchHash;
	int nOut, op;
	if (!GetSyscoinData(tx, vchData, vchHash, nOut, op) || op != OP_SYSCOIN_ASSET_ALLOCATION)
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
void CAssetAllocation::Serialize( vector<unsigned char> &vchData) {
    CDataStream dsAsset(SER_NETWORK, PROTOCOL_VERSION);
    dsAsset << *this;
	vchData = vector<unsigned char>(dsAsset.begin(), dsAsset.end());

}
void CAssetAllocationDB::WriteAssetAllocationIndex(const CAssetAllocation& assetallocation, const CAsset& asset, const CAmount& nSenderBalance, const CAmount& nAmount, const std::string& strSender, const std::string& strReceiver) {
	if (gArgs.IsArgSet("-zmqpubassetallocation") || fAssetAllocationIndex) {
		UniValue oName(UniValue::VOBJ);
		bool isMine = true;
		if (BuildAssetAllocationIndexerJson(assetallocation, asset, nSenderBalance, nAmount, strSender, strReceiver, isMine, oName)) {
			const string& strObj = oName.write();
			GetMainSignals().NotifySyscoinUpdate(strObj.c_str(), "assetallocation");
			if (isMine && fAssetAllocationIndex) {
				int nHeight = assetallocation.nHeight;
				const string& strKey = assetallocation.txHash.GetHex()+"-"+HexStr(asset.vchAsset)+"-"+ strSender +"-"+ strReceiver;
				{
					LOCK2(mempool.cs, cs_assetallocationindex);
					// we want to the height from mempool if it exists or use the one stored in assetallocation
					CTxMemPool::txiter it = mempool.mapTx.find(assetallocation.txHash);
					if (it != mempool.mapTx.end())
						nHeight = (*it).GetHeight();
					AssetAllocationIndex[nHeight][strKey] = strObj;
				}
			}
		}
	}

}
bool GetAssetAllocation(const CAssetAllocationTuple &assetAllocationTuple, CAssetAllocation& txPos) {
    if (passetallocationdb == nullptr || !passetallocationdb->ReadAssetAllocation(assetAllocationTuple, txPos))
        return false;
    return true;
}
bool DecodeAndParseAssetAllocationTx(const CTransaction& tx, int& op,
		vector<vector<unsigned char> >& vvch, char &type)
{
	CAssetAllocation assetallocation;
	// parse asset allocation, if not try to parse asset
	bool decode = DecodeAssetAllocationTx(tx, op, vvch)? true: DecodeAssetTx(tx, op, vvch);
	bool parse = assetallocation.UnserializeFromTx(tx);
	if (decode&&parse) {
		type = OP_SYSCOIN_ASSET_ALLOCATION;
		return true;
	}
	return false;
}
bool DecodeAssetAllocationTx(const CTransaction& tx, int& op,
        vector<vector<unsigned char> >& vvch) {
    bool found = false;


    // Strict check - bug disallowed
    for (unsigned int i = 0; i < tx.vout.size(); i++) {
        const CTxOut& out = tx.vout[i];
        vector<vector<unsigned char> > vvchRead;
        if (DecodeAssetAllocationScript(out.scriptPubKey, op, vvchRead)) {
            found = true; vvch = vvchRead;
            break;
        }
    }
    if (!found) vvch.clear();
    return found;
}


bool DecodeAssetAllocationScript(const CScript& script, int& op,
        vector<vector<unsigned char> > &vvch, CScript::const_iterator& pc) {
    opcodetype opcode;
	vvch.clear();
    if (!script.GetOp(pc, opcode)) return false;
    if (opcode < OP_1 || opcode > OP_16) return false;
    op = CScript::DecodeOP_N(opcode);
	if (op != OP_SYSCOIN_ASSET_ALLOCATION)
		return false;
	if (!script.GetOp(pc, opcode))
		return false;
	if (opcode < OP_1 || opcode > OP_16)
		return false;
	op = CScript::DecodeOP_N(opcode);
	if (!IsAssetAllocationOp(op))
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
bool DecodeAssetAllocationScript(const CScript& script, int& op,
        vector<vector<unsigned char> > &vvch) {
    CScript::const_iterator pc = script.begin();
    return DecodeAssetAllocationScript(script, op, vvch, pc);
}
bool RemoveAssetAllocationScriptPrefix(const CScript& scriptIn, CScript& scriptOut) {
    int op;
    vector<vector<unsigned char> > vvch;
    CScript::const_iterator pc = scriptIn.begin();

    if (!DecodeAssetAllocationScript(scriptIn, op, vvch, pc))
		return false;
	scriptOut = CScript(pc, scriptIn.end());
	return true;
}
// revert allocation to previous state and remove 
bool RevertAssetAllocation(const CAssetAllocationTuple &assetAllocationToRemove, const CAsset &asset, const uint256 &txHash, const int& nHeight, sorted_vector<CAssetAllocationTuple> &revertedAssetAllocations, const bool &bMiner=false) {
	// remove the sender arrival time from this tx
    if(!bMiner)
	    passetallocationdb->EraseISArrivalTime(assetAllocationToRemove, txHash);
	// only revert asset allocation once
	if (revertedAssetAllocations.find(assetAllocationToRemove) == revertedAssetAllocations.end())
	{

		string errorMessage = "";
		CAssetAllocation dbAssetAllocation;
		if (!passetallocationdb->ReadLastAssetAllocation(assetAllocationToRemove, dbAssetAllocation)) {
			dbAssetAllocation.vchAddress = assetAllocationToRemove.vchAddress;
			dbAssetAllocation.vchAsset = assetAllocationToRemove.vchAsset;
			dbAssetAllocation.nLastInterestClaimHeight = nHeight;
			dbAssetAllocation.nHeight = nHeight;
			dbAssetAllocation.fInterestRate = asset.fInterestRate;
		}
		// write the state back to previous state
		if (!passetallocationdb->WriteAssetAllocation(dbAssetAllocation, 0, 0, asset, INT64_MAX, "", "", false))
		{
			errorMessage = "SYSCOIN_ASSET_ALLOCATION_CONSENSUS_ERROR: ERRCODE: 1000 - " + _("Failed to write to asset allocation DB");
			return error(errorMessage.c_str());
		}

		revertedAssetAllocations.insert(assetAllocationToRemove);
	}

    if(!bMiner){
    	// remove the conflict once we revert since it is assumed to be resolved on POW
    	ArrivalTimesMap arrivalTimes;
    	passetallocationdb->ReadISArrivalTimes(assetAllocationToRemove, arrivalTimes);
    	bool removeConflict = true;
    	// remove only if all arrival times are either expired (30 mins) or no more zdag transactions left for this sender
    	for(auto& arrivalTime: arrivalTimes){
    		if((chainActive.Tip()->GetMedianTimePast() - arrivalTime.second) <= 1800000){
    			removeConflict = false;
    			break;
    		}
    	}
    	if(removeConflict){
    		passetallocationdb->EraseISArrivalTimes(assetAllocationToRemove);
    		sorted_vector<CAssetAllocationTuple>::const_iterator it = assetAllocationConflicts.find(assetAllocationToRemove);
    		if (it != assetAllocationConflicts.end()) {
    			assetAllocationConflicts.V.erase(const_iterator_cast(assetAllocationConflicts.V, it));
    		}
    	}
    }
	

	return true;
	
}
// calculate annual interest on an asset allocation
CAmount GetAssetAllocationInterest(CAssetAllocation & assetAllocation, const int& nHeight, string& errorMessage) {
	// need to do one more average balance calculation since the last update to this asset allocation
	if (!AccumulateInterestSinceLastClaim(assetAllocation, nHeight)) {
		errorMessage = _("Not enough blocks in-between interest claims");
		return 0;
	}
	const int &nInterestClaimBlockThreshold = fUnitTest ? 1 : ONE_MONTH_IN_BLOCKS;
	if ((nHeight - assetAllocation.nLastInterestClaimHeight) < nInterestClaimBlockThreshold || assetAllocation.nLastInterestClaimHeight == 0) {
		errorMessage = _("Not enough blocks have passed since the last claim, please wait some more time...");
		return 0;
	}
	const cpp_dec_float_50 &nInterestBlockTerm = fUnitTest? cpp_dec_float_50(ONE_HOUR_IN_BLOCKS): cpp_dec_float_50(ONE_YEAR_IN_BLOCKS);
	const cpp_dec_float_50 &nBlockDifference = cpp_dec_float_50(nHeight - assetAllocation.nLastInterestClaimHeight);

	// apply compound annual interest to get total interest since last time interest was collected
	const cpp_dec_float_50& nBalanceOverTimeDifference = cpp_dec_float_50(assetAllocation.nAccumulatedBalanceSinceLastInterestClaim / nBlockDifference);
	const cpp_dec_float_50& fInterestOverTimeDifference = cpp_dec_float_50(assetAllocation.fAccumulatedInterestSinceLastInterestClaim / nBlockDifference);
	const cpp_dec_float_50& nInterestPerBlock = fInterestOverTimeDifference / nInterestBlockTerm;
	const cpp_dec_float_50& powcalc = (boost::multiprecision::pow(cpp_dec_float_50(1.0) + nInterestPerBlock, nBlockDifference)*nBalanceOverTimeDifference) - nBalanceOverTimeDifference;
	return powcalc.convert_to<CAmount>();
}
bool ApplyAssetAllocationInterest(CAsset& asset, CAssetAllocation & assetAllocation, const int& nHeight, string& errorMessage) {
	CAmount nInterest = GetAssetAllocationInterest(assetAllocation, nHeight, errorMessage);
	if (nInterest <= 0) {
		errorMessage = _("Total interest exceeds maximum possible range of a 64 bit integer");
		return false;
	}
	// if interest cross max supply, reduce interest to fill up to max supply
	UniValue value = ValueFromAssetAmount(asset.nMaxSupply, asset.nPrecision, asset.bUseInputRanges);
	CAmount nMaxSupply = AssetAmountFromValue(value, asset.nPrecision, asset.bUseInputRanges);
	if ((nInterest + asset.nTotalSupply) > nMaxSupply) {
		nInterest = nMaxSupply - asset.nTotalSupply;
		if (nInterest <= 0) {
			errorMessage = _("Total Supply exceeds max supply");
			return false;
		}
	}
	assetAllocation.nBalance += nInterest;
	asset.nTotalSupply += nInterest;
	assetAllocation.nLastInterestClaimHeight = nHeight;
	// set accumulators to 0 again since we have claimed
	assetAllocation.nAccumulatedBalanceSinceLastInterestClaim = 0;
	assetAllocation.fAccumulatedInterestSinceLastInterestClaim = 0;
	return true;
}
// keep track of average balance within the interest claim period
bool AccumulateInterestSinceLastClaim(CAssetAllocation & assetAllocation, const int& nHeight) {
	const int &nBlocksSinceLastUpdate = (nHeight - assetAllocation.nHeight);
	if (nBlocksSinceLastUpdate <= 0)
		return false;
	// can't accumulate on burn
	if (assetAllocation.vchAddress == vchFromStringUint8("burn"))
		return false;
	// formula is 1/N * (blocks since last update * previous balance/interest rate) where N is the number of blocks in the total time period
	assetAllocation.nAccumulatedBalanceSinceLastInterestClaim += ((double)assetAllocation.nBalance)*nBlocksSinceLastUpdate;
	assetAllocation.fAccumulatedInterestSinceLastInterestClaim += assetAllocation.fInterestRate*nBlocksSinceLastUpdate;
	return true;
}
// revert asset allocations from "miner" db to the previous state db so the miner can run a mock setup to detect if transactions may cause issues before creating a block
bool RevertAssetAllocationMiner(const std::vector<CTransactionRef>& blockVtx){
    std::vector<vector<unsigned char> > vvchArgs;
    int op;
    // loop through twice, first time swap miner allocation to last allocation and second loop erase the miner allocation if it exists (assuming it was swapped)
    for (unsigned int n = 0; n< blockVtx.size(); n++) {
        const CTransactionRef txRef = blockVtx[n];
        if (!txRef)
            continue;
        const CTransaction &tx = *txRef;
        if (tx.nVersion == SYSCOIN_TX_VERSION_ASSET)
        {
            if (DecodeAssetAllocationTx(tx, op, vvchArgs) && op != OP_ASSET_COLLECT_INTEREST)
            {   
                CAssetAllocation theAssetAllocation(tx);
                if(theAssetAllocation.IsNull())
                    continue;
                const CAssetAllocationTuple assetAllocationTuple(theAssetAllocation.vchAsset, theAssetAllocation.vchAddress);
                CAssetAllocation allocationSender;
                if(passetallocationdb->ExistsLastAssetAllocationMiner(assetAllocationTuple)){
                    if(!passetallocationdb->ReadLastAssetAllocationMiner(assetAllocationTuple, allocationSender))
                        return false;
                    if(!passetallocationdb->WriteLastAssetAllocation(assetAllocationTuple, allocationSender))
                        return false;
                }
                else{
                    if(!passetallocationdb->EraseLastAssetAllocation(assetAllocationTuple))
                        return false;
                }
                
                if (!theAssetAllocation.listSendingAllocationAmounts.empty()) {
                    for (auto& amountTuple : theAssetAllocation.listSendingAllocationAmounts) {
                        const CAssetAllocationTuple receiverAllocationTuple(theAssetAllocation.vchAsset, amountTuple.first);
                        CAssetAllocation allocationReceiver;
                        if(passetallocationdb->ExistsLastAssetAllocationMiner(receiverAllocationTuple)){
                            if(!passetallocationdb->ReadLastAssetAllocationMiner(receiverAllocationTuple, allocationReceiver))
                                return false;
                            if(!passetallocationdb->WriteLastAssetAllocation(receiverAllocationTuple, allocationReceiver))
                                return false;
                        }
                        else{
                            if(!passetallocationdb->EraseLastAssetAllocation(receiverAllocationTuple))
                                return false;
                        }                                     
                    }
                }
                else if (!theAssetAllocation.listSendingAllocationInputs.empty()) {
                    for (auto& inputTuple : theAssetAllocation.listSendingAllocationInputs) {
                        const CAssetAllocationTuple receiverAllocationTuple(theAssetAllocation.vchAsset, inputTuple.first);
                        CAssetAllocation allocationReceiver;
                        if(passetallocationdb->ExistsLastAssetAllocationMiner(receiverAllocationTuple)){
                            if(!passetallocationdb->ReadLastAssetAllocationMiner(receiverAllocationTuple, allocationReceiver))
                                return false;
                            if(!passetallocationdb->WriteLastAssetAllocation(receiverAllocationTuple, allocationReceiver))
                                return false;
                        }
                        else{
                            if(!passetallocationdb->EraseLastAssetAllocation(receiverAllocationTuple))
                                return false;
                        }              
                    }    
                }
                
            }
        }
    }
    for (unsigned int n = 0; n< blockVtx.size(); n++) {
        const CTransactionRef txRef = blockVtx[n];
        if (!txRef)
            continue;
        const CTransaction &tx = *txRef;
        if (tx.nVersion == SYSCOIN_TX_VERSION_ASSET)
        {
            if (DecodeAssetAllocationTx(tx, op, vvchArgs) && op != OP_ASSET_COLLECT_INTEREST)
            {   
                CAssetAllocation theAssetAllocation(tx);
                if(theAssetAllocation.IsNull())
                    continue;
                const CAssetAllocationTuple assetAllocationTuple(theAssetAllocation.vchAsset, theAssetAllocation.vchAddress);
                if(passetallocationdb->ExistsLastAssetAllocationMiner(assetAllocationTuple)){
                    if(!passetallocationdb->EraseLastAssetAllocationMiner(assetAllocationTuple))
                        return false;
                }
              
                
                if (!theAssetAllocation.listSendingAllocationAmounts.empty()) {
                    for (auto& amountTuple : theAssetAllocation.listSendingAllocationAmounts) {
                        const CAssetAllocationTuple receiverAllocationTuple(theAssetAllocation.vchAsset, amountTuple.first);
                        if(passetallocationdb->ExistsLastAssetAllocationMiner(receiverAllocationTuple)){
                            if(!passetallocationdb->EraseLastAssetAllocationMiner(receiverAllocationTuple))
                                return false;
                        }
                                                         
                    }
                }
                else if (!theAssetAllocation.listSendingAllocationInputs.empty()) {
                    for (auto& inputTuple : theAssetAllocation.listSendingAllocationInputs) {
                        const CAssetAllocationTuple receiverAllocationTuple(theAssetAllocation.vchAsset, inputTuple.first);
                        if(passetallocationdb->ExistsLastAssetAllocationMiner(receiverAllocationTuple)){
                            if(!passetallocationdb->EraseLastAssetAllocationMiner(receiverAllocationTuple))
                                return false;
                        }           
                    }    
                }
                
            }
        }
    }    
    return true;       
}
bool CheckAssetAllocationInputs(const CTransaction &tx, const CCoinsViewCache &inputs, int op, const vector<vector<unsigned char> > &vvchArgs,
        bool fJustCheck, int nHeight, sorted_vector<CAssetAllocationTuple> &revertedAssetAllocations, string &errorMessage, bool bSanityCheck, bool bMiner) {
	if (passetallocationdb == nullptr)
		return false;
	if (tx.IsCoinBase() && !fJustCheck && !bSanityCheck)
	{
		LogPrint(BCLog::SYS, "*Trying to add assetallocation in coinbase transaction, skipping...");
		return false;
	}
	const uint256 & txHash = tx.GetHash();
	if (!bSanityCheck)
		LogPrint(BCLog::SYS,"*** ASSET ALLOCATION %d %d %s %s\n", nHeight,
			chainActive.Tip()->nHeight, txHash.ToString().c_str(),
			fJustCheck ? "JUSTCHECK" : "BLOCK");

	// unserialize assetallocation from txn, check for valid
	CAssetAllocation theAssetAllocation;
	vector<unsigned char> vchData;
	vector<unsigned char> vchHash;
	int nDataOut, tmpOp;
	if(!GetSyscoinData(tx, vchData, vchHash, nDataOut, tmpOp) || tmpOp != OP_SYSCOIN_ASSET_ALLOCATION || !theAssetAllocation.UnserializeFromData(vchData, vchHash))
	{
		errorMessage = "SYSCOIN_ASSET_ALLOCATION_CONSENSUS_ERROR ERRCODE: 1001 - " + _("Cannot unserialize data inside of this transaction relating to an assetallocation");
		return error(errorMessage.c_str());
	}

	if(fJustCheck)
	{
		if((op != OP_ASSET_ALLOCATION_BURN && vvchArgs.size() != 1) || (op == OP_ASSET_ALLOCATION_BURN && vvchArgs.size() != 4))
		{
			errorMessage = "SYSCOIN_ASSET_ALLOCATION_CONSENSUS_ERROR: ERRCODE: 1002 - " + _("Asset arguments incorrect size");
			return error(errorMessage.c_str());
		}		
		if(vchHash != vvchArgs[0])
		{
			errorMessage = "SYSCOIN_ASSET_ALLOCATION_CONSENSUS_ERROR: ERRCODE: 1003 - " + _("Hash provided doesn't match the calculated hash of the data");
			return error(errorMessage.c_str());
		}		
	}
	string retError = "";
	if(fJustCheck)
	{
		switch (op) {
		case OP_ASSET_ALLOCATION_SEND:
			if (theAssetAllocation.listSendingAllocationInputs.empty() && theAssetAllocation.listSendingAllocationAmounts.empty())
			{
				errorMessage = "SYSCOIN_ASSET_ALLOCATION_CONSENSUS_ERROR: ERRCODE: 1004 - " + _("Asset send must send an input or transfer balance");
				return error(errorMessage.c_str());
			}
			if (theAssetAllocation.listSendingAllocationInputs.size() > 250 || theAssetAllocation.listSendingAllocationAmounts.size() > 250)
			{
				errorMessage = "SYSCOIN_ASSET_ALLOCATION_CONSENSUS_ERROR: ERRCODE: 1005 - " + _("Too many receivers in one allocation send, maximum of 250 is allowed at once");
				return error(errorMessage.c_str());
			}
			if (theAssetAllocation.vchMemo.size() > MAX_MEMO_LENGTH)
			{
				errorMessage = "SYSCOIN_ASSET_ALLOCATION_CONSENSUS_ERROR: ERRCODE: 1006 - " + _("memo too long, must be 128 character or less");
				return error(errorMessage.c_str());
			}
			break;
		case OP_ASSET_COLLECT_INTEREST:
			if (!theAssetAllocation.listSendingAllocationInputs.empty() || !theAssetAllocation.listSendingAllocationAmounts.empty())
			{
				errorMessage = "SYSCOIN_ASSET_ALLOCATION_CONSENSUS_ERROR: ERRCODE: 1007 - " + _("Cannot send tokens in an interest collection transaction");
				return error(errorMessage.c_str());
			}
			if (theAssetAllocation.vchMemo.size() > 0)
			{
				errorMessage = "SYSCOIN_ASSET_ALLOCATION_CONSENSUS_ERROR: ERRCODE: 1008 - " + _("Cannot send memo when collecting interest");
				return error(errorMessage.c_str());
			}
			break;
		case OP_ASSET_ALLOCATION_BURN:
			if (!theAssetAllocation.listSendingAllocationInputs.empty())
			{
				errorMessage = "SYSCOIN_ASSET_ALLOCATION_CONSENSUS_ERROR: ERRCODE: 1007 - " + _("Cannot send tokens in an interest collection transaction");
				return error(errorMessage.c_str());
			}
			if (theAssetAllocation.listSendingAllocationAmounts.size() != 1)
			{
				errorMessage = "SYSCOIN_ASSET_ALLOCATION_CONSENSUS_ERROR: ERRCODE: 1007 - " + _("Must send exactly one output to burn transaction");
				return error(errorMessage.c_str());
			}
			if (theAssetAllocation.vchMemo.size() > 0)
			{
				errorMessage = "SYSCOIN_ASSET_ALLOCATION_CONSENSUS_ERROR: ERRCODE: 1008 - " + _("Cannot send memo when collecting interest");
				return error(errorMessage.c_str());
			}
			break;
		default:
			errorMessage = "SYSCOIN_ASSET_ALLOCATION_CONSENSUS_ERROR: ERRCODE: 1009 - " + _("Asset transaction has unknown op");
			return error(errorMessage.c_str());
		}
	}
	const string &user1 = bech32::Encode(Params().Bech32HRP(),theAssetAllocation.vchAddress);

	const CAssetAllocationTuple assetAllocationTuple(theAssetAllocation.vchAsset, theAssetAllocation.vchAddress);

	string strResponseEnglish = "";
	string strResponseGUID = "";
	CTransaction txTmp;
	GetSyscoinTransactionDescription(txTmp, op, strResponseEnglish, OP_SYSCOIN_ASSET_ALLOCATION, strResponseGUID);
	CAssetAllocation dbAssetAllocation;
	CAsset dbAsset;
	bool bRevert = false;
	bool bBalanceOverrun = false;
	bool bAddAllReceiversToConflictList = false;
	if (op == OP_ASSET_COLLECT_INTEREST)
	{
		if (!GetAssetAllocation(assetAllocationTuple, dbAssetAllocation))
		{
			errorMessage = "SYSCOIN_ASSET_ALLOCATION_CONSENSUS_ERROR: ERRCODE: 1010 - " + _("Cannot find asset allocation to collect interest on");
			return error(errorMessage.c_str());
		}
		if (!GetAsset(dbAssetAllocation.vchAsset, dbAsset))
		{
			errorMessage = "SYSCOIN_ASSET_ALLOCATION_CONSENSUS_ERROR: ERRCODE: 1011 - " + _("Failed to read from asset DB");
			return error(errorMessage.c_str());
		}
		if (dbAsset.fInterestRate <= 0 || dbAssetAllocation.fInterestRate <= 0)
		{
			errorMessage = "SYSCOIN_ASSET_ALLOCATION_CONSENSUS_ERROR: ERRCODE: 1012 - " + _("Cannot collect interest on this asset, no interest rate has been defined");
			return error(errorMessage.c_str());
		}
		if (!bSanityCheck) {
			bRevert = !fJustCheck;
			if (bRevert) {
				if (!RevertAssetAllocation(assetAllocationTuple, dbAsset, txHash, nHeight, revertedAssetAllocations))
				{
					errorMessage = "SYSCOIN_ASSET_ALLOCATION_CONSENSUS_ERROR: ERRCODE: 1014 - " + _("Failed to revert asset allocation");
					return error(errorMessage.c_str());
				}
			}
		}
		if (bRevert && !GetAssetAllocation(assetAllocationTuple, dbAssetAllocation))
		{
			errorMessage = "SYSCOIN_ASSET_ALLOCATION_CONSENSUS_ERROR: ERRCODE: 1015 - " + _("Cannot find sender asset allocation.");
			return error(errorMessage.c_str());
		}
		theAssetAllocation = dbAssetAllocation;
		// only apply interest on PoW
		if (!fJustCheck) {
			string errorMessageCollection = "";
			if(!ApplyAssetAllocationInterest(dbAsset, theAssetAllocation, nHeight, errorMessageCollection))
			{
				errorMessage = "SYSCOIN_ASSET_ALLOCATION_CONSENSUS_ERROR: ERRCODE: 1013 - " + _("You cannot collect interest on this asset: ") + errorMessageCollection;
				return error(errorMessage.c_str());
			}
			if (!bSanityCheck && !passetdb->WriteAsset(dbAsset, OP_ASSET_UPDATE))
			{
				errorMessage = "SYSCOIN_ASSET_ALLOCATION_CONSENSUS_ERROR: ERRCODE: 1014 - " + _("Failed to write to asset DB");
				return error(errorMessage.c_str());
			}
		}
		if(bSanityCheck)
			theAssetAllocation = dbAssetAllocation;

	}
	else if (op == OP_ASSET_ALLOCATION_BURN)
	{				
		if (!GetAssetAllocation(assetAllocationTuple, dbAssetAllocation))
		{
			errorMessage = "SYSCOIN_ASSET_ALLOCATION_CONSENSUS_ERROR: ERRCODE: 1010 - " + _("Cannot find asset allocation to collect interest on");
			return error(errorMessage.c_str());
		}	
		if (dbAssetAllocation.vchAddress != theAssetAllocation.vchAddress || !FindAssetOwnerInTx(inputs, tx, user1))
		{
			errorMessage = "SYSCOIN_ASSET_ALLOCATION_CONSENSUS_ERROR: ERRCODE: 1015 - " + _("Cannot send this asset. Asset allocation owner must sign off on this change");
			return error(errorMessage.c_str());
		}
		if(assetAllocationTuple.vchAsset != vvchArgs[1])
		{
			errorMessage = "SYSCOIN_ASSET_ALLOCATION_CONSENSUS_ERROR: ERRCODE: 1010 - " + _("Invalid asset details entered in the script output");
			return error(errorMessage.c_str());
		}		
		if (!GetAsset(dbAssetAllocation.vchAsset, dbAsset))
		{
			errorMessage = "SYSCOIN_ASSET_ALLOCATION_CONSENSUS_ERROR: ERRCODE: 1011 - " + _("Failed to read from asset DB");
			return error(errorMessage.c_str());
		}

		if (dbAsset.bUseInputRanges)
		{
			errorMessage = "SYSCOIN_ASSET_ALLOCATION_CONSENSUS_ERROR: ERRCODE: 1010 - " + _("Currently NFT Assets are not supported to be burned for SYSX");
			return error(errorMessage.c_str());
		}
		if(dbAsset.vchContract.empty() || dbAsset.vchContract != vvchArgs[3])
		{
			errorMessage = "SYSCOIN_ASSET_ALLOCATION_CONSENSUS_ERROR: ERRCODE: 1010 - " + _("Invalid contract entered in the script output or Syscoin Asset does not have the ethereum contract configured");
			return error(errorMessage.c_str());
		}
        const auto amountTuple = theAssetAllocation.listSendingAllocationAmounts[0];
        if (amountTuple.second <= 0)
        {
            errorMessage = "SYSCOIN_ASSET_ALLOCATION_CONSENSUS_ERROR: ERRCODE: 1020 - " + _("Burning amount must be positive");
            return error(errorMessage.c_str());
        } 
        if(AssetAmountFromValueNonNeg(stringFromVch(vvchArgs[2]), dbAsset.nPrecision, dbAsset.bUseInputRanges) != amountTuple.second)
        {
            errorMessage = "SYSCOIN_ASSET_ALLOCATION_CONSENSUS_ERROR: ERRCODE: 1015 - " + _("Invalid amount entered in the script output");
            return error(errorMessage.c_str());
        }   
        if(amountTuple.first != vchFromStringUint8("burn"))
        {
            errorMessage = "SYSCOIN_ASSET_ALLOCATION_CONSENSUS_ERROR: ERRCODE: 1015 - " + _("Must send output to burn address");
            return error(errorMessage.c_str());
        } 
		if (!bSanityCheck) {
			bRevert = !fJustCheck;
			if (bRevert) {
				if (!RevertAssetAllocation(assetAllocationTuple, dbAsset, txHash, nHeight, revertedAssetAllocations))
				{
					errorMessage = "SYSCOIN_ASSET_ALLOCATION_CONSENSUS_ERROR: ERRCODE: 1014 - " + _("Failed to revert asset allocation");
					return error(errorMessage.c_str());
				}
			}
		}
		if (bRevert && !GetAssetAllocation(assetAllocationTuple, dbAssetAllocation))
		{
			errorMessage = "SYSCOIN_ASSET_ALLOCATION_CONSENSUS_ERROR: ERRCODE: 1015 - " + _("Cannot find sender asset allocation.");
			return error(errorMessage.c_str());
		}
        const vector<unsigned char> vchMemo = theAssetAllocation.vchMemo;
        theAssetAllocation = dbAssetAllocation;
		const CAmount &nBalanceAfterSend = theAssetAllocation.nBalance - amountTuple.second;
		if (nBalanceAfterSend < 0) {
			errorMessage = "SYSCOIN_ASSET_ALLOCATION_CONSENSUS_ERROR: ERRCODE: 1016 - " + _("Sender balance is insufficient");
			return error(errorMessage.c_str());
		}
		if (!fJustCheck && !bSanityCheck) {
			const CAssetAllocationTuple receiverAllocationTuple(theAssetAllocation.vchAsset, amountTuple.first);
            // one of the first things we do per receiver is revert it to last pow state on the pow(!fJustCheck)
            if (bRevert) {
                if (!RevertAssetAllocation(receiverAllocationTuple, dbAsset, txHash, nHeight, revertedAssetAllocations, bMiner))
                {
                    errorMessage = "SYSCOIN_ASSET_ALLOCATION_CONSENSUS_ERROR: ERRCODE: 1019 - " + _("Failed to revert asset allocation");
                    return error(errorMessage.c_str());
                }
            } 
			CAssetAllocation receiverAllocation;
			if (!GetAssetAllocation(receiverAllocationTuple, receiverAllocation)) {
				receiverAllocation.vchAddress = receiverAllocationTuple.vchAddress;
				receiverAllocation.vchAsset = receiverAllocationTuple.vchAsset;
				receiverAllocation.nLastInterestClaimHeight = nHeight;
				receiverAllocation.nHeight = nHeight;
				receiverAllocation.fInterestRate = dbAsset.fInterestRate;
			}
			
			receiverAllocation.txHash = txHash;
			if (dbAsset.fInterestRate > 0) {
				// accumulate balances as sender allocations balances are adjusted
				AccumulateInterestSinceLastClaim(theAssetAllocation, nHeight);
			}
			receiverAllocation.fInterestRate = dbAsset.fInterestRate;
			receiverAllocation.nHeight = nHeight;
			receiverAllocation.vchMemo = vchMemo;
			receiverAllocation.nBalance += amountTuple.second;
			theAssetAllocation.nBalance -= amountTuple.second;
			const string& receiverAddress = "burn";
			if (!passetallocationdb->WriteAssetAllocation(receiverAllocation, nBalanceAfterSend, amountTuple.second, dbAsset, INT64_MAX, user1, receiverAddress, fJustCheck, bMiner))
			{
				errorMessage = "SYSCOIN_ASSET_ALLOCATION_CONSENSUS_ERROR: ERRCODE: 1023 - " + _("Failed to write to asset allocation DB");
				return error(errorMessage.c_str());
			}
		}else if (!bSanityCheck) {
			// add conflicting sender if using ZDAG
			assetAllocationConflicts.insert(assetAllocationTuple);
		}
	}
	else if (op == OP_ASSET_ALLOCATION_SEND)
	{
		LOCK(cs_assetallocation);
		if (!GetAssetAllocation(assetAllocationTuple, dbAssetAllocation))
		{
			errorMessage = "SYSCOIN_ASSET_ALLOCATION_CONSENSUS_ERROR: ERRCODE: 1015 - " + _("Cannot find sender asset allocation.");
			return error(errorMessage.c_str());
		}
		if (dbAssetAllocation.vchAddress != theAssetAllocation.vchAddress || !FindAssetOwnerInTx(inputs, tx, user1))
		{
            LogPrintf("dbAssetAllocation.vchAddress %s theAssetAllocation.vchAddress %s user1 %s\n", bech32::Encode(Params().Bech32HRP(),dbAssetAllocation.vchAddress).c_str(),bech32::Encode(Params().Bech32HRP(),theAssetAllocation.vchAddress).c_str(), user1.c_str());
			errorMessage = "SYSCOIN_ASSET_ALLOCATION_CONSENSUS_ERROR: ERRCODE: 1015a - " + _("Cannot send this asset. Asset allocation owner must sign off on this change");
			return error(errorMessage.c_str());
		}	
		if (!GetAsset(assetAllocationTuple.vchAsset, dbAsset))
		{
			errorMessage = "SYSCOIN_ASSET_ALLOCATION_CONSENSUS_ERROR: ERRCODE: 1016 - " + _("Failed to read from asset DB");
			return error(errorMessage.c_str());
		}
		if (!bSanityCheck) {
			bRevert = !fJustCheck;
			if (bRevert) {
				if (!RevertAssetAllocation(assetAllocationTuple, dbAsset, txHash, nHeight, revertedAssetAllocations, bMiner))
				{
					errorMessage = "SYSCOIN_ASSET_ALLOCATION_CONSENSUS_ERROR: ERRCODE: 1014 - " + _("Failed to revert asset allocation");
					return error(errorMessage.c_str());
				}
			}
		}
		if (bRevert && !GetAssetAllocation(assetAllocationTuple, dbAssetAllocation))
		{
			errorMessage = "SYSCOIN_ASSET_ALLOCATION_CONSENSUS_ERROR: ERRCODE: 1015 - " + _("Cannot find sender asset allocation.");
			return error(errorMessage.c_str());
		}
		theAssetAllocation.nBalance = dbAssetAllocation.nBalance;
		// cannot modify interest claim height when sending
		theAssetAllocation.nLastInterestClaimHeight = dbAssetAllocation.nLastInterestClaimHeight;
		theAssetAllocation.nHeight = dbAssetAllocation.nHeight;
		theAssetAllocation.fInterestRate = dbAssetAllocation.fInterestRate;
		theAssetAllocation.fAccumulatedInterestSinceLastInterestClaim = dbAssetAllocation.fAccumulatedInterestSinceLastInterestClaim;
		theAssetAllocation.nAccumulatedBalanceSinceLastInterestClaim = dbAssetAllocation.nAccumulatedBalanceSinceLastInterestClaim;
		// get sender assetallocation
		// if no custom allocations are sent with request
			// if sender assetallocation has custom allocations, break as invalid assetsend request
			// ensure sender balance >= balance being sent
			// ensure balance being sent >= minimum divisible quantity
				// if minimum divisible quantity is 0, ensure the balance being sent is a while quantity
			// deduct balance from sender and add to receiver(s) in loop
		// if custom allocations are sent with index numbers in an array
			// loop through array of allocations that are sent along with request
				// get qty of allocation
				// get receiver assetallocation allocation if exists through receiver address/assetallocation id tuple key
				// check the sender has the allocation in senders allocation list, remove from senders allocation list
				// add allocation to receivers allocation list
				// deduct qty from sender and add to receiver
				// commit receiver details to database using  through receiver address/assetallocation id tuple as key
		// commit sender details to database
		if (!theAssetAllocation.listSendingAllocationAmounts.empty()) {
			if (dbAsset.bUseInputRanges) {
				errorMessage = "SYSCOIN_ASSET_ALLOCATION_CONSENSUS_ERROR: ERRCODE: 1018 - " + _("Invalid asset send, request to send amounts but asset uses input ranges");
				return error(errorMessage.c_str());
			}
			// check balance is sufficient on sender
			CAmount nTotal = 0;
			for (auto& amountTuple : theAssetAllocation.listSendingAllocationAmounts) {
				const CAssetAllocationTuple receiverAllocationTuple(theAssetAllocation.vchAsset, amountTuple.first);
				// one of the first things we do per receiver is revert it to last pow state on the pow(!fJustCheck)
				if (bRevert) {
					if (!RevertAssetAllocation(receiverAllocationTuple, dbAsset, txHash, nHeight, revertedAssetAllocations, bMiner))
					{
						errorMessage = "SYSCOIN_ASSET_ALLOCATION_CONSENSUS_ERROR: ERRCODE: 1019 - " + _("Failed to revert asset allocation");
						return error(errorMessage.c_str());
					}
				}
				nTotal += amountTuple.second;
				if (amountTuple.second <= 0)
				{
					errorMessage = "SYSCOIN_ASSET_ALLOCATION_CONSENSUS_ERROR: ERRCODE: 1020 - " + _("Receiving amount must be positive");
					return error(errorMessage.c_str());
				}
			}
			const CAmount &nBalanceAfterSend = dbAssetAllocation.nBalance - nTotal;
			if (nBalanceAfterSend < 0) {
				bBalanceOverrun = true;
				if(bSanityCheck)
					errorMessage = "SYSCOIN_ASSET_ALLOCATION_CONSENSUS_ERROR: ERRCODE: 1021 - " + _("Sender balance is insufficient");
				if (fJustCheck && !bSanityCheck) {
					// add conflicting sender
					assetAllocationConflicts.insert(assetAllocationTuple);
				}
			}
			else if (fJustCheck) {
				// if sender was is flagged as conflicting, add all receivers to conflict list
				if (assetAllocationConflicts.find(assetAllocationTuple) != assetAllocationConflicts.end())
				{			
					bAddAllReceiversToConflictList = true;
				}
			}
			for (auto& amountTuple : theAssetAllocation.listSendingAllocationAmounts) {
				CAssetAllocation receiverAllocation;
				if (amountTuple.first == theAssetAllocation.vchAddress) {
					errorMessage = "SYSCOIN_ASSET_ALLOCATION_CONSENSUS_ERROR: ERRCODE: 1022 - " + _("Cannot send an asset allocation to yourself");
					return error(errorMessage.c_str());
				}
				if (!bSanityCheck) {
					const CAssetAllocationTuple receiverAllocationTuple(theAssetAllocation.vchAsset, amountTuple.first);
					if (fJustCheck) {
						if (bAddAllReceiversToConflictList || bBalanceOverrun) {
							assetAllocationConflicts.insert(receiverAllocationTuple);
						}
					}
					if (!bBalanceOverrun) {
						CAssetAllocation receiverAllocation;
						if (!GetAssetAllocation(receiverAllocationTuple, receiverAllocation)) {
							receiverAllocation.vchAddress = receiverAllocationTuple.vchAddress;
							receiverAllocation.vchAsset = receiverAllocationTuple.vchAsset;
							receiverAllocation.nLastInterestClaimHeight = nHeight;
							receiverAllocation.nHeight = nHeight;
							receiverAllocation.fInterestRate = dbAsset.fInterestRate;
						}
					
						receiverAllocation.txHash = txHash;
						if (dbAsset.fInterestRate > 0) {
							// accumulate balances as sender/receiver allocations balances are adjusted
							AccumulateInterestSinceLastClaim(receiverAllocation, nHeight);
							AccumulateInterestSinceLastClaim(theAssetAllocation, nHeight);
						}
						receiverAllocation.fInterestRate = dbAsset.fInterestRate;
						receiverAllocation.nHeight = nHeight;
						receiverAllocation.vchMemo = theAssetAllocation.vchMemo;
						receiverAllocation.nBalance += amountTuple.second;
						theAssetAllocation.nBalance -= amountTuple.second;
					
						const string& receiverAddress = bech32::Encode(Params().Bech32HRP(),receiverAllocation.vchAddress);
						if (!dbAsset.vchBlacklist.empty() && std::find(dbAsset.vchBlacklist.begin(), dbAsset.vchBlacklist.end(), receiverAllocation.vchAddress) != dbAsset.vchBlacklist.end())
						{
							errorMessage = "SYSCOIN_ASSET_CONSENSUS_ERROR: ERRCODE: 2034 - " + _("Receiver has been blacklisted cannot send: ") + receiverAddress;
							return error(errorMessage.c_str());
						}
						if (!passetallocationdb->WriteAssetAllocation(receiverAllocation, nBalanceAfterSend, amountTuple.second, dbAsset, INT64_MAX, user1, receiverAddress, fJustCheck, bMiner))
						{
							errorMessage = "SYSCOIN_ASSET_ALLOCATION_CONSENSUS_ERROR: ERRCODE: 1023 - " + _("Failed to write to asset allocation DB");
							return error(errorMessage.c_str());
						}
					}
				}
			}
		}
		else if (!theAssetAllocation.listSendingAllocationInputs.empty()) {
			if (!dbAsset.bUseInputRanges) {
				errorMessage = "SYSCOIN_ASSET_ALLOCATION_CONSENSUS_ERROR: ERRCODE: 1024 - " + _("Invalid asset send, request to send input ranges but asset uses amounts");
				return error(errorMessage.c_str());
			}
			// check balance is sufficient on sender
			CAmount nTotal = 0;
			vector<CAmount> rangeTotals;
			for (auto& inputTuple : theAssetAllocation.listSendingAllocationInputs) {
				const CAssetAllocationTuple receiverAllocationTuple(theAssetAllocation.vchAsset, inputTuple.first);
				// one of the first things we do per receiver is revert it to last pow state on the pow(!fJustCheck)
				if (bRevert) {
					if (!RevertAssetAllocation(receiverAllocationTuple, dbAsset, txHash, nHeight, revertedAssetAllocations, bMiner))
					{
						errorMessage = "SYSCOIN_ASSET_ALLOCATION_CONSENSUS_ERROR: ERRCODE: 1025 - " + _("Failed to revert asset allocation");
						return error(errorMessage.c_str());
					}
				}
				const unsigned int &rangeTotal = validateRangesAndGetCount(inputTuple.second);
				if(rangeTotal == 0)
				{
					errorMessage = "SYSCOIN_ASSET_ALLOCATION_CONSENSUS_ERROR: ERRCODE: 1026 - " + _("Invalid input ranges");
					return error(errorMessage.c_str());
				}
				const CAmount rangeTotalAmount = rangeTotal;
				rangeTotals.emplace_back(std::move(rangeTotalAmount));
				nTotal += rangeTotals.back();
			}
			const CAmount &nBalanceAfterSend = dbAssetAllocation.nBalance - nTotal;
			if (nBalanceAfterSend < 0) {
				bBalanceOverrun = true;
				if(bSanityCheck)
					errorMessage = "SYSCOIN_ASSET_ALLOCATION_CONSENSUS_ERROR: ERRCODE: 1027 - " + _("Sender balance is insufficient");
				if (fJustCheck && !bSanityCheck) {
					// add conflicting sender
					assetAllocationConflicts.insert(assetAllocationTuple);
				}
			}
			else if (fJustCheck) {
				// if sender was is flagged as conflicting, add all receivers to conflict list
				if (assetAllocationConflicts.find(assetAllocationTuple) != assetAllocationConflicts.end())
				{
					bAddAllReceiversToConflictList = true;
				}
			}
			for (unsigned int i = 0; i < theAssetAllocation.listSendingAllocationInputs.size();i++) {
				InputRanges &input = theAssetAllocation.listSendingAllocationInputs[i];
				CAssetAllocation receiverAllocation;
				if (input.first == theAssetAllocation.vchAddress) {
					errorMessage = "SYSCOIN_ASSET_ALLOCATION_CONSENSUS_ERROR: ERRCODE: 1028 - " + _("Cannot send an asset allocation to yourself");
					return error(errorMessage.c_str());
				}
				if (!bSanityCheck) {
					const CAssetAllocationTuple receiverAllocationTuple(theAssetAllocation.vchAsset, input.first);
					if (fJustCheck) {
						if (bAddAllReceiversToConflictList || bBalanceOverrun) {
							assetAllocationConflicts.insert(receiverAllocationTuple);
						}

					}
					// ensure entire allocation range being subtracted exists on sender (full inclusion check)
					if (!doesRangeContain(dbAssetAllocation.listAllocationInputs, input.second))
					{
						bBalanceOverrun = true;
						if (bSanityCheck)
							errorMessage = "SYSCOIN_ASSET_ALLOCATION_CONSENSUS_ERROR: ERRCODE: 1029 - " + _("Input not found");
						if (fJustCheck && !bSanityCheck) {
							// add conflicting sender
							assetAllocationConflicts.insert(assetAllocationTuple);
						}
					}
					if (!bBalanceOverrun) {
						if (!GetAssetAllocation(receiverAllocationTuple, receiverAllocation)) {
							receiverAllocation.vchAddress = receiverAllocationTuple.vchAddress;
							receiverAllocation.vchAsset = receiverAllocationTuple.vchAsset;
							receiverAllocation.nLastInterestClaimHeight = nHeight;
							receiverAllocation.nHeight = nHeight;
							receiverAllocation.fInterestRate = dbAsset.fInterestRate;
						}
					
						receiverAllocation.txHash = txHash;
						receiverAllocation.fInterestRate = dbAsset.fInterestRate;
						receiverAllocation.nHeight = nHeight;
						receiverAllocation.vchMemo = theAssetAllocation.vchMemo;
						// figure out receivers added ranges and balance
						vector<CRange> outputMerge;
						receiverAllocation.listAllocationInputs.insert(std::end(receiverAllocation.listAllocationInputs), std::begin(input.second), std::end(input.second));
						mergeRanges(receiverAllocation.listAllocationInputs, outputMerge);
						receiverAllocation.listAllocationInputs = outputMerge;
						receiverAllocation.nBalance += rangeTotals[i];

						// figure out senders subtracted ranges and balance
						vector<CRange> outputSubtract;
						subtractRanges(dbAssetAllocation.listAllocationInputs, input.second, outputSubtract);
						theAssetAllocation.listAllocationInputs = outputSubtract;
						theAssetAllocation.nBalance -= rangeTotals[i];
					
						const string& receiverAddress = bech32::Encode(Params().Bech32HRP(),receiverAllocation.vchAddress);
						if (!dbAsset.vchBlacklist.empty() && std::find(dbAsset.vchBlacklist.begin(), dbAsset.vchBlacklist.end(), receiverAllocation.vchAddress) != dbAsset.vchBlacklist.end())
						{
							errorMessage = "SYSCOIN_ASSET_CONSENSUS_ERROR: ERRCODE: 2034 - " + _("Receiver has been blacklisted cannot send: ") + receiverAddress;
							return error(errorMessage.c_str());
						}
						if (!passetallocationdb->WriteAssetAllocation(receiverAllocation, nBalanceAfterSend, rangeTotals[i], dbAsset, INT64_MAX, user1, receiverAddress, fJustCheck, bMiner))
						{
							errorMessage = "SYSCOIN_ASSET_ALLOCATION_CONSENSUS_ERROR: ERRCODE: 1030 - " + _("Failed to write to asset allocation DB");
							return error(errorMessage.c_str());
						}
					}
				}
			}
		}
	}

	// write assetallocation  
	// asset sends are the only ones confirming without PoW
	if (!bBalanceOverrun && !bSanityCheck && ((op != OP_ASSET_ALLOCATION_SEND && !fJustCheck) || op == OP_ASSET_ALLOCATION_SEND)) {
		// set the assetallocation's txn-dependent 
		
		theAssetAllocation.nHeight = nHeight;
		theAssetAllocation.txHash = txHash;
		
		int64_t ms = INT64_MAX;
		if (fJustCheck) {
			ms = GetTimeMillis();
		}
		const string &user = op == OP_ASSET_COLLECT_INTEREST ? user1 : "";
		if (!passetallocationdb->WriteAssetAllocation(theAssetAllocation, theAssetAllocation.nBalance, 0, dbAsset, ms, user, user, fJustCheck, bMiner))
		{
			errorMessage = "SYSCOIN_ASSET_ALLOCATION_CONSENSUS_ERROR: ERRCODE: 1031 - " + _("Failed to write to asset allocation DB");
			return error(errorMessage.c_str());
		}
		// debug
		
		LogPrint(BCLog::SYS,"CONNECTED ASSET ALLOCATION: op=%s assetallocation=%s hash=%s height=%d fJustCheck=%d at time %lld\n",
				assetAllocationFromOp(op).c_str(),
				assetAllocationTuple.ToString().c_str(),
				txHash.ToString().c_str(),
				nHeight,
				fJustCheck ? 1 : 0, (long long)ms);

	}
    return true;
}
UniValue tpstestinfo(const JSONRPCRequest& request) {
	const UniValue &params = request.params;
	if (request.fHelp || 0 != params.size())
		throw runtime_error("tpstestinfo\n"
			"Gets TPS Test information for receivers of assetallocation transfers\n");
	if(!fTPSTest)
		throw runtime_error("SYSCOIN_ASSET_ALLOCATION_RPC_ERROR: ERRCODE: 1501 - " + _("This function requires tpstest configuration to be set upon startup. Please shutdown and enable it by adding it to your syscoin.conf file and then call 'tpstestsetenabled true'."));
	
	UniValue oTPSTestResults(UniValue::VOBJ);
	UniValue oTPSTestReceivers(UniValue::VARR);
	UniValue oTPSTestReceiversMempool(UniValue::VARR);
	oTPSTestResults.pushKV("enabled", fTPSTestEnabled);
    oTPSTestResults.pushKV("testinitiatetime", (int64_t)nTPSTestingStartTime);
	oTPSTestResults.pushKV("teststarttime", (int64_t)nTPSTestingSendRawEndTime);
	for (auto &receivedTime : vecTPSTestReceivedTimesMempool) {
		UniValue oTPSTestStatusObj(UniValue::VOBJ);
		oTPSTestStatusObj.pushKV("txid", receivedTime.first.GetHex());
		oTPSTestStatusObj.pushKV("time", receivedTime.second);
		oTPSTestReceiversMempool.push_back(oTPSTestStatusObj);
	}
	oTPSTestResults.pushKV("receivers", oTPSTestReceiversMempool);
	return oTPSTestResults;
}
UniValue tpstestsetenabled(const JSONRPCRequest& request) {
	const UniValue &params = request.params;
	if (request.fHelp || 1 != params.size())
		throw runtime_error("tpstestsetenabled [enabled]\n"
			"\nSet TPS Test to enabled/disabled state. Must have -tpstest configuration set to make this call.\n"
			"\nArguments:\n"
			"1. enabled                  (boolean, required) TPS Test enabled state. Set to true for enabled and false for disabled.\n"
			"\nExample:\n"
			+ HelpExampleCli("tpstestsetenabled", "true"));
	if(!fTPSTest)
		throw runtime_error("SYSCOIN_ASSET_ALLOCATION_RPC_ERROR: ERRCODE: 1501 - " + _("This function requires tpstest configuration to be set upon startup. Please shutdown and enable it by adding it to your syscoin.conf file and then try again."));
	fTPSTestEnabled = params[0].get_bool();
	if (!fTPSTestEnabled) {
		vecTPSTestReceivedTimesMempool.clear();
		nTPSTestingSendRawEndTime = 0;
		nTPSTestingStartTime = 0;
	}
	UniValue result(UniValue::VOBJ);
	result.pushKV("status", "success");
	return result;
}
UniValue tpstestadd(const JSONRPCRequest& request) {
	const UniValue &params = request.params;
	if (request.fHelp || 1 > params.size() || params.size() > 2)
		throw runtime_error("tpstestadd [starttime] [{\"tx\":\"hex\"},...]\n"
			"\nAdds raw transactions to the test raw tx queue to be sent to the network at starttime.\n"
			"\nArguments:\n"
			"1. starttime                  (numeric, required) Unix epoch time in micro seconds for when to send the raw transaction queue to the network. If set to 0, will not send transactions until you call this function again with a defined starttime.\n"
			"2. \"raw transactions\"                (array, not-required) A json array of signed raw transaction strings\n"
			"     [\n"
			"       {\n"
			"         \"tx\":\"hex\",    (string, required) The transaction hex\n"
			"       } \n"
			"       ,...\n"
			"     ]\n"
			"\nExample:\n"
			+ HelpExampleCli("tpstestadd", "\"223233433839384\" \"[{\\\"tx\\\":\\\"first raw hex tx\\\"},{\\\"tx\\\":\\\"second raw hex tx\\\"}]\""));
	if (!fTPSTest)
		throw runtime_error("SYSCOIN_ASSET_ALLOCATION_RPC_ERROR: ERRCODE: 1501 - " + _("This function requires tpstest configuration to be set upon startup. Please shutdown and enable it by adding it to your syscoin.conf file and then call 'tpstestsetenabled true'."));

	bool bFirstTime = vecTPSRawTransactions.empty();
	nTPSTestingStartTime = params[0].get_int64();
	UniValue txs;
	if(params.size() > 1)
		txs = params[1].get_array();
	if (fTPSTestEnabled) {
		for (unsigned int idx = 0; idx < txs.size(); idx++) {
			const UniValue& tx = txs[idx];
			UniValue paramsRawTx(UniValue::VARR);
			paramsRawTx.push_back(find_value(tx.get_obj(), "tx").get_str());

			JSONRPCRequest request;
			request.params = paramsRawTx;
			vecTPSRawTransactions.push_back(request);
		}
		if (bFirstTime) {
			// define a task for the worker to process
			std::packaged_task<void()> task([]() {
				while (nTPSTestingStartTime <= 0 || GetTimeMicros() < nTPSTestingStartTime) {
					MilliSleep(0);
				}
				nTPSTestingSendRawStartTime = nTPSTestingStartTime;

				for (auto &txReq : vecTPSRawTransactions) {
					sendrawtransaction(txReq);
				}
			});
			bool isThreadPosted = false;
			for (int numTries = 1; numTries <= 50; numTries++)
			{
				// send task to threadpool pointer from init.cpp
				isThreadPosted = threadpool->tryPost(task);
				if (isThreadPosted)
				{
					break;
				}
				MilliSleep(10);
			}
			if (!isThreadPosted)
				throw runtime_error("SYSCOIN_ASSET_ALLOCATION_RPC_ERROR: ERRCODE: 1501 - " + _("thread pool queue is full"));
		}
	}
	UniValue result(UniValue::VOBJ);
	result.pushKV("status", "success");
	return result;
}
UniValue assetallocationburn(const JSONRPCRequest& request) {
    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    CWallet* const pwallet = wallet.get();
	const UniValue &params = request.params;
	if (request.fHelp || 3 != params.size())
		throw runtime_error(
			"assetallocationburn [asset] [owner] [amount]\n"
			"<asset> Asset guid.\n"
			"<owner> Address that owns this asset allocation.\n"
			"<amount> Amount of asset to burn to SYSX.\n"
			+ HelpRequiringPassphrase(pwallet));

	vector<unsigned char> vchAsset = ParseHex(params[0].get_str());
	string strAddress = params[1].get_str();

	string strAddressFrom;
	const CTxDestination &addressFrom = DecodeDestination(strAddress);
	strAddressFrom = strAddress;
	LOCK(cs_assetallocation);
	CAssetAllocation theAssetAllocation;
	const CAssetAllocationTuple assetAllocationTuple(vchAsset, bech32::Decode(strAddress).second);
	if (!GetAssetAllocation(assetAllocationTuple, theAssetAllocation))
		throw runtime_error("SYSCOIN_ASSET_ALLOCATION_RPC_ERROR: ERRCODE: 1500 - " + _("Could not find a asset allocation with this key"));

	CAsset theAsset;
	if (!GetAsset(vchAsset, theAsset))
		throw runtime_error("SYSCOIN_ASSET_ALLOCATION_RPC_ERROR: ERRCODE: 1501 - " + _("Could not find a asset with this key"));

	CScript scriptPubKeyFromOrig;
	if (!strAddressFrom.empty()) {
		scriptPubKeyFromOrig = GetScriptForDestination(addressFrom);
	}
    
	CAmount amount = AssetAmountFromValueNonNeg(params[2], theAsset.nPrecision, theAsset.bUseInputRanges);
	
	theAssetAllocation.ClearAssetAllocation();
	theAssetAllocation.vchAsset = assetAllocationTuple.vchAsset;
	theAssetAllocation.vchAddress = assetAllocationTuple.vchAddress;
    string burnAddr = "burn";
	theAssetAllocation.listSendingAllocationAmounts.push_back(make_pair(vchFromStringUint8("burn"), amount));

	vector<unsigned char> data;
	theAssetAllocation.Serialize(data);
	uint256 hash = Hash(data.begin(), data.end());

	vector<unsigned char> vchHashAsset = vector<unsigned char>(hash.begin(), hash.end());
	if (!theAssetAllocation.UnserializeFromData(data, vchHashAsset))
		throw runtime_error("SYSCOIN_ASSET_ALLOCATION_RPC_ERROR: ERRCODE: 1505 - " + _("Could not unserialize asset allocation data"));

	CScript scriptPubKey;
	scriptPubKey << CScript::EncodeOP_N(OP_SYSCOIN_ASSET_ALLOCATION) << CScript::EncodeOP_N(OP_ASSET_ALLOCATION_BURN) << vchHashAsset << vchAsset << vchFromString(ValueFromAssetAmount(amount,theAsset.nPrecision, theAsset.bUseInputRanges).getValStr()) << theAsset.vchContract << OP_2DROP << OP_2DROP << OP_2DROP << OP_DROP;
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

	return syscointxfund_helper("", vecSend);
}
UniValue assetallocationsend(const JSONRPCRequest& request) {
    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    CWallet* const pwallet = wallet.get();
	const UniValue &params = request.params;
	if (request.fHelp || params.size() != 5)
		throw runtime_error(
			"assetallocationsend [asset] [owner] ([{\"ownerto\":\"address\",\"amount\":amount},...]  or [{\"ownerto\":\"address\",\"ranges\":[{\"start\":index,\"end\":index},...]},...]) [memo] [witness]\n"
			"Send an asset allocation you own to another address. Maximimum recipients is 250.\n"
			"<asset> Asset guid.\n"
			"<owner> Address that owns this asset allocation.\n"
			"<ownerto> Address to transfer to.\n"
			"<amount> Quantity of asset to send.\n"
			"<ranges> Ranges of inputs to send in integers specified in the start and end fields.\n"
			"<memo> Message to include in this asset allocation transfer.\n"
			"<witness> Witness address that will sign for web-of-trust notarization of this transaction.\n"
			"<escrowspend> Set to any non-empty value if this is sending an asset for escrow payment. Used by API internally in conjunction with marketplace offers being sold for assets.\n"
			"The third parameter can be either an array of address and amounts if sending amount pairs or an array of address and array of start/end pairs of indexes for input ranges.\n"
			+ HelpRequiringPassphrase(pwallet));

	// gather & validate inputs
	vector<unsigned char> vchAsset = ParseHex(params[0].get_str());
	string vchAddressFrom = params[1].get_str();
	UniValue valueTo = params[2];
	vector<unsigned char> vchMemo = vchFromValue(params[3]);
	vector<unsigned char> vchWitness;
    string strWitness = params[4].get_str();
	if (!valueTo.isArray())
		throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Array of receivers not found");
	string strAddressFrom;
	const string &strAddress = vchAddressFrom;
    CTxDestination addressFrom;
    if(strAddress != "burn"){
	    addressFrom = DecodeDestination(strAddress);
    	if (IsValidDestination(addressFrom)) {
    		strAddressFrom = strAddress;
    	}
    }
	LOCK(cs_assetallocation);
	CAssetAllocation theAssetAllocation;
	const CAssetAllocationTuple assetAllocationTuple(vchAsset, strAddress == "burn"? vchFromStringUint8("burn"): bech32::Decode(strAddress).second);
	if (!GetAssetAllocation(assetAllocationTuple, theAssetAllocation))
		throw runtime_error("SYSCOIN_ASSET_ALLOCATION_RPC_ERROR: ERRCODE: 1500 - " + _("Could not find a asset allocation with this key"));

	CAsset theAsset;
	if (!GetAsset(vchAsset, theAsset))
		throw runtime_error("SYSCOIN_ASSET_ALLOCATION_RPC_ERROR: ERRCODE: 1501 - " + _("Could not find a asset with this key"));

	theAssetAllocation.ClearAssetAllocation();
	theAssetAllocation.vchMemo = vchMemo;
	theAssetAllocation.vchAsset = assetAllocationTuple.vchAsset;
	theAssetAllocation.vchAddress = assetAllocationTuple.vchAddress;
	UniValue receivers = valueTo.get_array();
	
	for (unsigned int idx = 0; idx < receivers.size(); idx++) {
		const UniValue& receiver = receivers[idx];
		if (!receiver.isObject())
			throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "expected object with {\"ownerto'\",\"inputranges\" or \"amount\"}");

		UniValue receiverObj = receiver.get_obj();
		const string &toStr = find_value(receiverObj, "ownerto").get_str();
        CTxDestination dest = DecodeDestination(toStr);
		if (!IsValidDestination(dest))
			throw runtime_error("SYSCOIN_ASSET_ALLOCATION_RPC_ERROR: ERRCODE: 1502 - " + _("Asset must be sent to a valid syscoin address"));
        vector<uint8_t> vchAddressTo = bech32::Decode(toStr).second;
	
		UniValue inputRangeObj = find_value(receiverObj, "ranges");
		UniValue amountObj = find_value(receiverObj, "amount");
		if (inputRangeObj.isArray()) {
			UniValue inputRanges = inputRangeObj.get_array();
			vector<CRange> vectorOfRanges;
			for (unsigned int rangeIndex = 0; rangeIndex < inputRanges.size(); rangeIndex++) {
				const UniValue& inputRangeObj = inputRanges[rangeIndex];
				if(!inputRangeObj.isObject())
					throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "expected object with {\"start'\",\"end\"}");
				UniValue startRangeObj = find_value(inputRangeObj, "start");
				UniValue endRangeObj = find_value(inputRangeObj, "end");
				if(!startRangeObj.isNum())
					throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "start range not found for an input");
				if(!endRangeObj.isNum())
					throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "end range not found for an input");
				vectorOfRanges.push_back(CRange(startRangeObj.get_int(), endRangeObj.get_int()));
			}
			theAssetAllocation.listSendingAllocationInputs.push_back(make_pair(vchAddressTo, vectorOfRanges));
		}
		else if (amountObj.isNum()) {
			const CAmount &amount = AssetAmountFromValue(amountObj, theAsset.nPrecision, theAsset.bUseInputRanges);
			if (amount <= 0)
				throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "amount must be positive");
			theAssetAllocation.listSendingAllocationAmounts.push_back(make_pair(vchAddressTo, amount));
		}
		else
			throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "expected inputrange as string or amount as number in receiver array");

	}

	CScript scriptPubKeyFromOrig;
	if (!strAddressFrom.empty()) {
		scriptPubKeyFromOrig = GetScriptForDestination(addressFrom);
	}
    
	CScript scriptPubKey;

	// check to see if a transaction for this asset/address tuple has arrived before minimum latency period
	ArrivalTimesMap arrivalTimes;
	passetallocationdb->ReadISArrivalTimes(assetAllocationTuple, arrivalTimes);
	const int64_t & nNow = GetTimeMillis();
	int minLatency = ZDAG_MINIMUM_LATENCY_SECONDS * 1000;
	if (fUnitTest)
		minLatency = 1000;
	for (auto& arrivalTime : arrivalTimes) {
		// if this tx arrived within the minimum latency period flag it as potentially conflicting
		if ((nNow - arrivalTime.second) < minLatency) {
			throw runtime_error("SYSCOIN_ASSET_ALLOCATION_RPC_ERROR: ERRCODE: 1503 - " + _("Please wait a few more seconds and try again..."));
		}
	}

	vector<unsigned char> data;
	theAssetAllocation.Serialize(data);
	uint256 hash = Hash(data.begin(), data.end());

	vector<unsigned char> vchHashAsset = vector<unsigned char>(hash.begin(), hash.end());
	if (!theAssetAllocation.UnserializeFromData(data, vchHashAsset))
		throw runtime_error("SYSCOIN_ASSET_ALLOCATION_RPC_ERROR: ERRCODE: 1505 - " + _("Could not unserialize asset allocation data"));
	scriptPubKey << CScript::EncodeOP_N(OP_SYSCOIN_ASSET_ALLOCATION) << CScript::EncodeOP_N(OP_ASSET_ALLOCATION_SEND) << vchHashAsset << OP_2DROP << OP_DROP;
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

	return syscointxfund_helper(strWitness, vecSend);
}
UniValue assetallocationcollectinterest(const JSONRPCRequest& request) {
    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    CWallet* const pwallet = wallet.get();
	const UniValue &params = request.params;
	if (request.fHelp || params.size() != 3)
		throw runtime_error(
			"assetallocationcollectinterest [asset] [owner] [witness]\n"
			"Collect interest on this asset allocation if an interest rate is set on this asset.\n"
			"<asset> Asset guid.\n"
			"<owner> Address which owns this asset allocation.\n"
			"<witness> Witness address that will sign for web-of-trust notarization of this transaction.\n"
			+ HelpRequiringPassphrase(pwallet));

	// gather & validate inputs
	vector<unsigned char> vchAsset = ParseHex(params[0].get_str());
	string vchAddressFrom = params[1].get_str();
    vector<unsigned char> vchWitness;
    string strWitness = params[2].get_str();

	string strAddressFrom;
	const string &strAddress = vchAddressFrom;
	const CTxDestination address = DecodeDestination(strAddress);
	if (IsValidDestination(address)) {
		strAddressFrom = strAddress;
	}
	LOCK(cs_assetallocation);
	CAssetAllocation theAssetAllocation;
	const CAssetAllocationTuple assetAllocationTuple(vchAsset, bech32::Decode(strAddress).second);
	if (!GetAssetAllocation(assetAllocationTuple, theAssetAllocation))
		throw runtime_error("SYSCOIN_ASSET_ALLOCATION_RPC_ERROR: ERRCODE: 1506 - " + _("Could not find a asset allocation with this key"));
	CScript scriptPubKeyFromOrig;
	if (!strAddressFrom.empty()) {
		scriptPubKeyFromOrig = GetScriptForDestination(address);
	}
    
	CScript scriptPubKey;
	theAssetAllocation.ClearAssetAllocation();
	theAssetAllocation.vchAsset = assetAllocationTuple.vchAsset;
	theAssetAllocation.vchAddress = assetAllocationTuple.vchAddress;
	vector<unsigned char> data;
	theAssetAllocation.Serialize(data);
	uint256 hash = Hash(data.begin(), data.end());

	vector<unsigned char> vchHashAsset =  vector<unsigned char>(hash.begin(), hash.end());
	scriptPubKey << CScript::EncodeOP_N(OP_SYSCOIN_ASSET_ALLOCATION) << CScript::EncodeOP_N(OP_ASSET_COLLECT_INTEREST) << vchHashAsset << OP_2DROP << OP_DROP;
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

	return syscointxfund_helper(strWitness, vecSend);
}

UniValue assetallocationinfo(const JSONRPCRequest& request) {
	const UniValue &params = request.params;
    if (request.fHelp || 3 != params.size())
        throw runtime_error("assetallocationinfo <asset> <owner> <getinputs>\n"
                "Show stored values of a single asset allocation. Set getinputs to true if you want to get the allocation inputs, if applicable.\n");

    vector<unsigned char> vchAsset = ParseHex(params[0].get_str());
	string vchAddressFrom = params[1].get_str();
	bool bGetInputs = params[2].get_bool();
	UniValue oAssetAllocation(UniValue::VOBJ);
	string strAddressFrom = vchAddressFrom;
	const CAssetAllocationTuple assetAllocationTuple(vchAsset, strAddressFrom == "burn"? vchFromStringUint8("burn"): bech32::Decode(strAddressFrom).second);
	CAssetAllocation txPos;
	LOCK(cs_assetallocation);
	if (passetallocationdb == nullptr || !passetallocationdb->ReadAssetAllocation(assetAllocationTuple, txPos))
		throw runtime_error("SYSCOIN_ASSET_ALLOCATION_RPC_ERROR: ERRCODE: 1507 - " + _("Failed to read from assetallocation DB"));

	CAsset theAsset;
	if (!GetAsset(vchAsset, theAsset))
		throw runtime_error("SYSCOIN_ASSET_ALLOCATION_RPC_ERROR: ERRCODE: 1508 - " + _("Could not find a asset with this key"));


	if(!BuildAssetAllocationJson(txPos, theAsset, bGetInputs, oAssetAllocation))
		oAssetAllocation.clear();
    return oAssetAllocation;
}
int DetectPotentialAssetAllocationSenderConflicts(const CAssetAllocationTuple& assetAllocationTupleSender, const uint256& lookForTxHash) {
	LOCK2(cs_main, mempool.cs);
	CAssetAllocation dbLastAssetAllocation, dbAssetAllocation;
	ArrivalTimesMap arrivalTimes;
	// get last POW asset allocation balance to ensure we use POW balance to check for potential conflicts in mempool (real-time balances).
	// The idea is that real-time spending amounts can in some cases overrun the POW balance safely whereas in some cases some of the spends are 
	// put in another block due to not using enough fees or for other reasons that miners don't mine them.
	// We just want to flag them as level 1 so it warrants deeper investigation on receiver side if desired (if fund amounts being transferred are not negligible)
	if (passetallocationdb == nullptr || !passetallocationdb->ReadLastAssetAllocation(assetAllocationTupleSender, dbLastAssetAllocation))
		return ZDAG_NOT_FOUND;

	if (!passetallocationdb->ReadAssetAllocation(assetAllocationTupleSender, dbAssetAllocation))
		return ZDAG_NOT_FOUND;

	// ensure that this transaction exists in the arrivalTimes DB (which is the running stored lists of all real-time asset allocation sends not in POW)
	// the arrivalTimes DB is only added to for valid asset allocation sends that happen in real-time and it is removed once there is POW on that transaction
	if(!passetallocationdb->ReadISArrivalTimes(assetAllocationTupleSender, arrivalTimes))
		return ZDAG_NOT_FOUND;
	// sort the arrivalTimesMap ascending based on arrival time value

	// Declaring the type of Predicate for comparing arrivalTimesMap
	typedef std::function<bool(std::pair<uint256, int64_t>, std::pair<uint256, int64_t>)> Comparator;

	// Defining a lambda function to compare two pairs. It will compare two pairs using second field
	Comparator compFunctor =
		[](std::pair<uint256, int64_t> elem1, std::pair<uint256, int64_t> elem2)
	{
		return elem1.second < elem2.second;
	};

	// Declaring a set that will store the pairs using above comparision logic
	std::set<std::pair<uint256, int64_t>, Comparator> arrivalTimesSet(
		arrivalTimes.begin(), arrivalTimes.end(), compFunctor);

	// go through arrival times and check that balances don't overrun the POW balance
	pair<uint256, int64_t> lastArrivalTime;
	lastArrivalTime.second = GetTimeMillis();
	map<std::vector<uint8_t>, CAmount> mapBalances;
	// init sender balance, track balances by address
	// this is important because asset allocations can be sent/received within blocks and will overrun balances prematurely if not tracked properly, for example pow balance 3, sender sends 3, gets 2 sends 2 (total send 3+2=5 > balance of 3 from last stored state, this is a valid scenario and shouldn't be flagged)
	CAmount &senderBalance = mapBalances[assetAllocationTupleSender.vchAddress];
	senderBalance = dbLastAssetAllocation.nBalance;
	int minLatency = ZDAG_MINIMUM_LATENCY_SECONDS * 1000;
	if (fUnitTest)
		minLatency = 1000;
	for (auto& arrivalTime : arrivalTimesSet)
	{
		// ensure mempool has this transaction and it is not yet mined, get the transaction in question
		const CTransactionRef txRef = mempool.get(arrivalTime.first);
		if (!txRef)
			continue;
		const CTransaction &tx = *txRef;

		// if this tx arrived within the minimum latency period flag it as potentially conflicting
		if (abs(arrivalTime.second - lastArrivalTime.second) < minLatency) {
			return ZDAG_MINOR_CONFLICT;
		}
		const uint256& txHash = tx.GetHash();
		// get asset allocation object from this tx, if for some reason it doesn't have it, just skip (shouldn't happen)
		CAssetAllocation assetallocation(tx);
		if (assetallocation.IsNull())
			continue;

		if (!assetallocation.listSendingAllocationAmounts.empty()) {
			for (auto& amountTuple : assetallocation.listSendingAllocationAmounts) {
				senderBalance -= amountTuple.second;
				mapBalances[amountTuple.first] += amountTuple.second;
				// if running balance overruns the stored balance then we have a potential conflict
				if (senderBalance < 0) {
					return ZDAG_MINOR_CONFLICT;
				}
			}
		}
		else if (!assetallocation.listSendingAllocationInputs.empty()) {
			for (auto& inputTuple : assetallocation.listSendingAllocationInputs) {
				const unsigned int &rangeCount = validateRangesAndGetCount(inputTuple.second);
				if (rangeCount == 0)
					continue;
				senderBalance -= rangeCount;
				mapBalances[inputTuple.first] += rangeCount;
				// if running balance overruns the stored balance then we have a potential conflict
				if (senderBalance < 0) {
					return ZDAG_MINOR_CONFLICT;
				}
			}
		}
		// even if the sender may be flagged, the order of events suggests that this receiver should get his money confirmed upon pow because real-time balance is sufficient for this receiver
		if (txHash == lookForTxHash) {
			return ZDAG_STATUS_OK;
		}
	}
	return lookForTxHash.IsNull()? ZDAG_STATUS_OK: ZDAG_NOT_FOUND;
}
UniValue assetallocationsenderstatus(const JSONRPCRequest& request) {
	const UniValue &params = request.params;
	if (request.fHelp || 3 != params.size())
		throw runtime_error("assetallocationsenderstatus <asset> <owner> <txid>\n"
			"Show status as it pertains to any current Z-DAG conflicts or warnings related to a sender or sender/txid combination of an asset allocation transfer. Leave txid empty if you are not checking for a specific transfer.\n"
			"Return value is in the status field and can represent 3 levels(0, 1 or 2)\n"
			"Level -1 means not found, not a ZDAG transaction, perhaps it is already confirmed.\n"
			"Level 0 means OK.\n"
			"Level 1 means warning (checked that in the mempool there are more spending balances than current POW sender balance). An active stance should be taken and perhaps a deeper analysis as to potential conflicts related to the sender.\n"
			"Level 2 means an active double spend was found and any depending asset allocation sends are also flagged as dangerous and should wait for POW confirmation before proceeding.\n");

	vector<unsigned char> vchAsset = ParseHex(params[0].get_str());
	string vchAddressSender = params[1].get_str();
	const string &strAddressSender = vchAddressSender;
	uint256 txid;
	txid.SetNull();
	if(!params[2].get_str().empty())
		txid.SetHex(params[2].get_str());
	UniValue oAssetAllocationStatus(UniValue::VOBJ);
    LOCK(cs_assetallocation);
    
    const int64_t & nNow = GetTimeMillis();
	const CAssetAllocationTuple assetAllocationTupleSender(vchAsset, bech32::Decode(strAddressSender).second);
    
       // if arrival times have expired, then expire any conflicting status for this sender as well
    ArrivalTimesMap arrivalTimes;
    passetallocationdb->ReadISArrivalTimes(assetAllocationTupleSender, arrivalTimes); 
    bool allArrivalsExpired = true;
    for (auto& arrivalTime : arrivalTimes) {
        // if its been less than 30m then we keep conflict status
        if((nNow - arrivalTime.second) <= 1800000){
            allArrivalsExpired = false;
            break;
        }   
    }   
 
    if(allArrivalsExpired){
        passetallocationdb->EraseISArrivalTimes(assetAllocationTupleSender);
        sorted_vector<CAssetAllocationTuple>::const_iterator it = assetAllocationConflicts.find(assetAllocationTupleSender);
        if (it != assetAllocationConflicts.end()) {
            assetAllocationConflicts.V.erase(const_iterator_cast(assetAllocationConflicts.V, it));
        }
    }
    
	int nStatus = ZDAG_STATUS_OK;
	if (assetAllocationConflicts.find(assetAllocationTupleSender) != assetAllocationConflicts.end())
		nStatus = ZDAG_MAJOR_CONFLICT;
	else {
		nStatus = DetectPotentialAssetAllocationSenderConflicts(assetAllocationTupleSender, txid);
	}
	oAssetAllocationStatus.pushKV("status", nStatus);
	return oAssetAllocationStatus;
}
bool BuildAssetAllocationJson(CAssetAllocation& assetallocation, const CAsset& asset, const bool bGetInputs, UniValue& oAssetAllocation)
{
    oAssetAllocation.pushKV("_id", CAssetAllocationTuple(assetallocation.vchAsset, assetallocation.vchAddress).ToString());
	oAssetAllocation.pushKV("asset", HexStr(assetallocation.vchAsset));
	oAssetAllocation.pushKV("symbol", stringFromVch(asset.vchSymbol));
	oAssetAllocation.pushKV("interest_rate", assetallocation.fInterestRate);
    oAssetAllocation.pushKV("txid", assetallocation.txHash.GetHex());
    oAssetAllocation.pushKV("height", (int)assetallocation.nHeight);
	oAssetAllocation.pushKV("owner",  bech32::Encode(Params().Bech32HRP(),assetallocation.vchAddress));
	oAssetAllocation.pushKV("balance", ValueFromAssetAmount(assetallocation.nBalance, asset.nPrecision, asset.bUseInputRanges));
	oAssetAllocation.pushKV("interest_claim_height", (int)assetallocation.nLastInterestClaimHeight);
	oAssetAllocation.pushKV("memo", stringFromVch(assetallocation.vchMemo));
	if (bGetInputs) {
		UniValue oAssetAllocationInputsArray(UniValue::VARR);
		for (auto& input : assetallocation.listAllocationInputs) {
			UniValue oAssetAllocationInputObj(UniValue::VOBJ);
			oAssetAllocationInputObj.pushKV("start", (int)input.start);
			oAssetAllocationInputObj.pushKV("end", (int)input.end);
			oAssetAllocationInputsArray.push_back(oAssetAllocationInputObj);
		}
		oAssetAllocation.pushKV("inputs", oAssetAllocationInputsArray);
	}
	string errorMessage;
	oAssetAllocation.pushKV("accumulated_interest", ValueFromAssetAmount(GetAssetAllocationInterest(assetallocation, chainActive.Tip()->nHeight, errorMessage), asset.nPrecision, asset.bUseInputRanges));
	return true;
}
bool BuildAssetAllocationIndexerJson(const CAssetAllocation& assetallocation, const CAsset& asset, const CAmount& nSenderBalance, const CAmount& nAmount, const string& strSender, const string& strReceiver, bool &isMine, UniValue& oAssetAllocation)
{
	CAmount nAmountDisplay = nAmount;
	int64_t nTime = 0;
	bool bConfirmed = false;
	if (chainActive.Height() >= assetallocation.nHeight - 1) {
		bConfirmed = (chainActive.Height() - assetallocation.nHeight) >= 1;
		CBlockIndex *pindex = chainActive[chainActive.Height() >= assetallocation.nHeight ? assetallocation.nHeight : assetallocation.nHeight - 1];
		if (pindex) {
			nTime = pindex->GetMedianTimePast();
		}
	}

	oAssetAllocation.pushKV("_id", CAssetAllocationTuple(assetallocation.vchAsset, assetallocation.vchAddress).ToString());
	oAssetAllocation.pushKV("txid", assetallocation.txHash.GetHex());
	oAssetAllocation.pushKV("time", nTime);
	oAssetAllocation.pushKV("asset", HexStr(assetallocation.vchAsset));
	oAssetAllocation.pushKV("symbol", stringFromVch(asset.vchSymbol));
	oAssetAllocation.pushKV("interest_rate", assetallocation.fInterestRate);
	oAssetAllocation.pushKV("height", (int)assetallocation.nHeight);
	oAssetAllocation.pushKV("sender", strSender);
	oAssetAllocation.pushKV("sender_balance", ValueFromAssetAmount(nSenderBalance, asset.nPrecision, asset.bUseInputRanges));
	oAssetAllocation.pushKV("receiver", strReceiver);
	oAssetAllocation.pushKV("receiver_balance", ValueFromAssetAmount(assetallocation.nBalance, asset.nPrecision, asset.bUseInputRanges));
	oAssetAllocation.pushKV("memo", stringFromVch(assetallocation.vchMemo));
	oAssetAllocation.pushKV("confirmed", bConfirmed);
	if (fAssetAllocationIndex) {
        CWallet* const pwallet = GetDefaultWallet();
		string strCat = "";
		if (!strSender.empty() || !strReceiver.empty()) {
			isminefilter filter = ISMINE_SPENDABLE;
            if(!strSender.empty() && strSender != "burn" && pwallet)
            {
    			isminefilter mine = IsMine(*pwallet, DecodeDestination(strSender));
    			if ((mine & filter)) {
    				strCat = "send";
    				nAmountDisplay *= -1;
    			}
            }
			else if(!strReceiver.empty() && strReceiver != "burn" && pwallet){
				isminefilter mine = IsMine(*pwallet, DecodeDestination(strReceiver));
				if ((mine & filter))
					strCat = "receive";
			}
		}

		oAssetAllocation.pushKV("category", strCat);
	}
	oAssetAllocation.pushKV("amount", ValueFromAssetAmount(nAmountDisplay, asset.nPrecision, asset.bUseInputRanges));
	return true;
}
void AssetAllocationTxToJSON(const int op, const std::vector<unsigned char> &vchData, const std::vector<unsigned char> &vchHash, UniValue &entry)
{
	string opName = assetAllocationFromOp(op);
	CAssetAllocation assetallocation;
	if(!assetallocation.UnserializeFromData(vchData, vchHash))
		return;
	CAsset dbAsset;
	GetAsset(assetallocation.vchAsset, dbAsset);

	entry.pushKV("txtype", opName);
	entry.pushKV("_id", CAssetAllocationTuple(assetallocation.vchAsset, assetallocation.vchAddress).ToString());
	entry.pushKV("asset", HexStr(assetallocation.vchAsset));
	entry.pushKV("owner", bech32::Encode(Params().Bech32HRP(), assetallocation.vchAddress));
	entry.pushKV("memo", stringFromVch(assetallocation.vchMemo));
	UniValue oAssetAllocationReceiversArray(UniValue::VARR);
	if (!assetallocation.listSendingAllocationAmounts.empty()) {
		for (auto& amountTuple : assetallocation.listSendingAllocationAmounts) {
			UniValue oAssetAllocationReceiversObj(UniValue::VOBJ);
			oAssetAllocationReceiversObj.pushKV("owner", bech32::Encode(Params().Bech32HRP(),amountTuple.first));
			oAssetAllocationReceiversObj.pushKV("amount", ValueFromAssetAmount(amountTuple.second, dbAsset.nPrecision, dbAsset.bUseInputRanges));
			oAssetAllocationReceiversArray.push_back(oAssetAllocationReceiversObj);
		}
	}
	else if (!assetallocation.listSendingAllocationInputs.empty()) {
		for (auto& inputTuple : assetallocation.listSendingAllocationInputs) {
			UniValue oAssetAllocationReceiversObj(UniValue::VOBJ);
			UniValue oAssetAllocationInputsArray(UniValue::VARR);
			oAssetAllocationReceiversObj.pushKV("owner", bech32::Encode(Params().Bech32HRP(),inputTuple.first));
			for (auto& inputRange : inputTuple.second) {
				UniValue oInput(UniValue::VOBJ);
				oInput.pushKV("start", (int)inputRange.start);
				oInput.pushKV("end", (int)inputRange.end);
				oAssetAllocationInputsArray.push_back(oInput);
			}
			oAssetAllocationReceiversObj.pushKV("inputs", oAssetAllocationInputsArray);
			oAssetAllocationReceiversArray.push_back(oAssetAllocationReceiversObj);
		}
	}
	entry.pushKV("allocations", oAssetAllocationReceiversArray);


}
bool CAssetAllocationTransactionsDB::ScanAssetAllocationIndex(const int count, const int from, const UniValue& oOptions, UniValue& oRes) {
	string strTxid = "";
	vector<string> vecSenders;
	vector<string> vecReceivers;
	string strAsset = "";
	bool bParseKey = false;
	int nStartBlock = 0;
	if (!oOptions.isNull()) {
		const UniValue &txid = find_value(oOptions, "txid");
		if (txid.isStr()) {
			strTxid = txid.get_str();
			bParseKey = true;
		}
		const UniValue &asset = find_value(oOptions, "asset");
		if (asset.isStr()) {
			strAsset = asset.get_str();
			bParseKey = true;
		}

		const UniValue &owners = find_value(oOptions, "receivers");
		if (owners.isArray()) {
			const UniValue &ownersArray = owners.get_array();
			for (unsigned int i = 0; i < ownersArray.size(); i++) {
				const UniValue &owner = ownersArray[i].get_obj();
				const UniValue &ownerStr = find_value(owner, "receiver");
				if (ownerStr.isStr()) {
					bParseKey = true;
					vecReceivers.push_back(ownerStr.get_str());
				}
			}
		}

		const UniValue &senders = find_value(oOptions, "senders");
		if (senders.isArray()) {
			const UniValue &sendersArray = senders.get_array();
			for (unsigned int i = 0; i < sendersArray.size(); i++) {
				const UniValue &sender = sendersArray[i].get_obj();
				const UniValue &senderStr = find_value(sender, "sender");
				if (senderStr.isStr()) {
					bParseKey = true;
					vecSenders.push_back(senderStr.get_str());
				}
			}
		}
		const UniValue &startblock = find_value(oOptions, "startblock");
		if (startblock.isNum()) {
			nStartBlock = startblock.get_int();
		}
	}
	int index = 0;
	UniValue assetValue;
	vector<string> contents;
	contents.reserve(5);
	for (auto&indexObj : boost::adaptors::reverse(AssetAllocationIndex)) {
		if (nStartBlock > 0 && indexObj.first < nStartBlock)
			continue;
		for (auto& indexItem : indexObj.second) {
			if (bParseKey) {
				boost::algorithm::split(contents, indexItem.first, boost::is_any_of("-"));
				if (!strTxid.empty() && strTxid != contents[0])
					continue;
				if (!strAsset.empty() && strAsset != contents[1])
					continue;
				if (!vecSenders.empty() && std::find(vecSenders.begin(), vecSenders.end(), contents[2]) == vecSenders.end())
					continue;
				if (!vecReceivers.empty() && std::find(vecReceivers.begin(), vecReceivers.end(), contents[3]) == vecReceivers.end())
					continue;
			}
			index += 1;
			if (index <= from) {
				continue;
			}
			if (assetValue.read(indexItem.second))
				oRes.push_back(assetValue);
			if (index >= count + from)
				break;
		}
		if (index >= count + from)
			break;
	}
	return true;
}

bool CAssetAllocationDB::ScanAssetAllocations(const int count, const int from, const UniValue& oOptions, UniValue& oRes) {
	string strTxid = "";
	vector<vector<uint8_t> > vchAddresses;
	vector<unsigned char> vchAsset;
	int nStartBlock = 0;
	if (!oOptions.isNull()) {
		const UniValue &txid = find_value(oOptions, "txid");
		if (txid.isStr()) {
			strTxid = txid.get_str();
		}
		const UniValue &assetObj = find_value(oOptions, "asset");
		if(assetObj.isStr()) {
			vchAsset = ParseHex(assetObj.get_str());
		}

		const UniValue &owners = find_value(oOptions, "receivers");
		if (owners.isArray()) {
			const UniValue &ownersArray = owners.get_array();
			for (unsigned int i = 0; i < ownersArray.size(); i++) {
				const UniValue &owner = ownersArray[i].get_obj();
				const UniValue &ownerStr = find_value(owner, "receiver");
				if (ownerStr.isStr()) {
					vchAddresses.push_back(bech32::Decode(ownerStr.get_str()).second);
				}
			}
		}

		const UniValue &startblock = find_value(oOptions, "startblock");
		if (startblock.isNum()) {
			nStartBlock = startblock.get_int();
		}
	}

	LOCK(cs_assetallocation);
	boost::scoped_ptr<CDBIterator> pcursor(NewIterator());
	pcursor->SeekToFirst();
	CAssetAllocation txPos;
	pair<string, vector<unsigned char> > key;
	bool bGetInputs = true;
	CAsset theAsset;
	int index = 0;
	while (pcursor->Valid()) {
		boost::this_thread::interruption_point();
		try {
			if (pcursor->GetKey(key) && key.first == "assetallocationi") {
				pcursor->GetValue(txPos);
				if (!GetAsset(txPos.vchAsset, theAsset))
				{
					pcursor->Next();
					continue;
				}
				if (nStartBlock > 0 && txPos.nHeight < nStartBlock)
				{
					pcursor->Next();
					continue;
				}
				if (!strTxid.empty() && strTxid != txPos.txHash.GetHex())
				{
					pcursor->Next();
					continue;
				}
				if (!vchAsset.empty() && vchAsset != txPos.vchAsset)
				{
					pcursor->Next();
					continue;
				}
				if (!vchAddresses.empty() && std::find(vchAddresses.begin(), vchAddresses.end(), txPos.vchAddress) == vchAddresses.end())
				{
					pcursor->Next();
					continue;
				}
				UniValue oAssetAllocation(UniValue::VOBJ);
				if (!BuildAssetAllocationJson(txPos, theAsset, bGetInputs, oAssetAllocation)) 
				{
					pcursor->Next();
					continue;
				}
				index += 1;
				if (index <= from) {
					pcursor->Next();
					continue;
				}
				oRes.push_back(oAssetAllocation);
				if (index >= count + from) {
					break;
				}
			}
			pcursor->Next();
		}
		catch (std::exception &e) {
			return error("%s() : deserialize error", __PRETTY_FUNCTION__);
		}
	}
	return true;
}
UniValue listassetallocationtransactions(const JSONRPCRequest& request) {
	const UniValue &params = request.params;
	if (request.fHelp || 3 < params.size())
		throw runtime_error("listassetallocationtransactions [count] [from] [{options}]\n"
			"list asset allocations sent or recieved in this wallet. -assetallocationindex must be set as a startup parameter to use this function.\n"
			"[count]          (numeric, optional, default=10) The number of results to return, 0 to return all.\n"
			"[from]           (numeric, optional, default=0) The number of results to skip.\n"
			"[options]        (object, optional) A json object with options to filter results\n"
			"    {\n"
			"      \"txid\":txid					(string) Transaction ID to filter.\n"
			"	   \"asset\":guid					(string) Asset GUID to filter.\n"
			"	   \"senders\"						(array, optional) a json array with senders\n"
			"		[\n"
			"			{\n"
			"				\"sender\":string		(string) Sender address to filter.\n"
			"			} \n"
			"			,...\n"
			"		]\n"
			"	   \"receivers\"					(array, optional) a json array with receivers\n"
			"		[\n"
			"			{\n"
			"				\"receiver\":string		(string) Receiver address to filter.\n"
			"			} \n"
			"			,...\n"
			"		]\n"
			"      \"startblock\":block 			(number) Earliest block to filter from. Block number is the block at which the transaction would have entered your mempool.\n"
			"    }\n"
			+ HelpExampleCli("listassetallocationtransactions", "0 10")
			+ HelpExampleCli("listassetallocationtransactions", "0 0 '{\"asset\":\"32bff1fa844c124\",\"startblock\":0}'")
			+ HelpExampleCli("listassetallocationtransactions", "0 0 '{\"senders\":[{\"sender\":\"SfaMwYY19Dh96B9qQcJQuiNykVRTzXMsZR\"},{\"sender\":\"SfaMwYY19Dh96B9qQcJQuiNykVRTzXMsZR\"}]}'")
			+ HelpExampleCli("listassetallocationtransactions", "0 0 '{\"txid\":\"1c7f966dab21119bac53213a2bc7532bff1fa844c124fd750a7d0b1332440bd1\"}'")
		);
	UniValue options;
	int count = 10;
	int from = 0;
	if (params.size() > 0)
		count = params[0].get_int();
	if (params.size() > 1)
		from = params[1].get_int();
	if (params.size() > 2)
		options = params[2];
	if (!fAssetAllocationIndex) {
		throw runtime_error("SYSCOIN_ASSET_ALLOCATION_RPC_ERROR: ERRCODE: 1509 - " + _("Asset allocation index not enabled, you must enable -assetallocationindex as a startup parameter or through syscoin.conf file to use this function.")); 
	}
	LOCK(cs_assetallocationindex);
	UniValue oRes(UniValue::VARR);
	if (!passetallocationtransactionsdb->ScanAssetAllocationIndex(count, from, options, oRes))
		throw runtime_error("SYSCOIN_ASSET_ALLOCATION_RPC_ERROR: ERRCODE: 1509 - " + _("Scan failed"));
	return oRes;
}
UniValue listassetallocations(const JSONRPCRequest& request) {
	const UniValue &params = request.params;
	if (request.fHelp || 3 < params.size())
		throw runtime_error("listassetallocations [count] [from] [{options}]\n"
			"scan through all asset allocations.\n"
			"[count]          (numeric, optional, unbounded=0, default=10) The number of results to return, 0 to return all.\n"
			"[from]           (numeric, optional, default=0) The number of results to skip.\n"
			"[options]        (array, optional) A json object with options to filter results\n"
			"    {\n"
			"      \"txid\":txid					(string) Transaction ID to filter.\n"
			"	   \"asset\":guid					(string) Asset GUID to filter.\n"
			"	   \"receivers\"					(array, optional) a json array with receivers\n"
			"		[\n"
			"			{\n"
			"				\"receiver\":string		(string) Receiver address to filter.\n"
			"			} \n"
			"			,...\n"
			"		]\n"
			"      \"startblock\":block				(number) Earliest block to filter from. Block number is the block at which the transaction would have confirmed.\n"
			"    }\n"
			+ HelpExampleCli("listassetallocations", "0")
			+ HelpExampleCli("listassetallocations", "10 10")
			+ HelpExampleCli("listassetallocations", "0 0 '{\"asset\":\"32bff1fa844c124\",\"startblock\":0}'")
			+ HelpExampleCli("listassetallocations", "0 0 '{\"receivers\":[{\"receiver\":\"SfaMwYY19Dh96B9qQcJQuiNykVRTzXMsZR\"},{\"receiver\":\"SfaMwYY19Dh96B9qQcJQuiNykVRTzXMsZR\"}]}'")
			+ HelpExampleCli("listassetallocations", "0 0 '{\"txid\":\"1c7f966dab21119bac53213a2bc7532bff1fa844c124fd750a7d0b1332440bd1\"}'")
		);
	UniValue options;
	int count = 10;
	int from = 0;
	if (params.size() > 0) {
		count = params[0].get_int();
		if (count == 0) {
			count = INT_MAX;
		} else
		if (count < 0) {
			throw runtime_error("SYSCOIN_ASSET_ALLOCATION_RPC_ERROR: ERRCODE: 1510 - " + _("'count' must be 0 or greater"));
		}
	}
	if (params.size() > 1) {
		from = params[1].get_int();
		if (from < 0) {
			throw runtime_error("SYSCOIN_ASSET_ALLOCATION_RPC_ERROR: ERRCODE: 1510 - " + _("'from' must be 0 or greater"));
		}
	}
	if (params.size() > 2) {
		options = params[2];
	}
	LOCK(cs_assetallocation);
	UniValue oRes(UniValue::VARR);
	if (!passetallocationdb->ScanAssetAllocations(count, from, options, oRes))
		throw runtime_error("SYSCOIN_ASSET_ALLOCATION_RPC_ERROR: ERRCODE: 1510 - " + _("Scan failed"));
	return oRes;
}
