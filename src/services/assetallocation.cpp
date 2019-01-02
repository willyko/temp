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
#include <bech32.h>
using namespace std;
using namespace boost::multiprecision;
AssetAllocationIndexItemMap AssetAllocationIndex;
AssetBalanceMap mempoolMapAssetBalances;
ArrivalTimesMapImpl arrivalTimesMap;
bool IsAssetAllocationOp(int op) {
	return op == OP_ASSET_ALLOCATION_SEND || op == OP_ASSET_ALLOCATION_BURN;
}
string CAssetAllocationTuple::ToString() const {
	return boost::lexical_cast<string>(nAsset) + "-" + bech32::Encode(Params().Bech32HRP(),vchAddress);
}
string assetAllocationFromOp(int op) {
    switch (op) {
	case OP_ASSET_SEND:
		return "assetsend";
	case OP_ASSET_ALLOCATION_SEND:
		return "assetallocationsend";
	case OP_ASSET_ALLOCATION_BURN:
		return "assetallocationburn";
    default:
        return "<unknown assetallocation op>";
    }
}
bool CAssetAllocation::UnserializeFromData(const vector<unsigned char> &vchData) {
    try {
        CDataStream dsAsset(vchData, SER_NETWORK, PROTOCOL_VERSION);
        dsAsset >> *this;
    } catch (std::exception &e) {
		SetNull();
        return false;
    }
	return true;
}
bool CAssetAllocation::UnserializeFromTx(const CTransaction &tx) {
	vector<unsigned char> vchData;
	int nOut, op;
	if (!GetSyscoinData(tx, vchData, nOut, op) || op != OP_SYSCOIN_ASSET_ALLOCATION)
	{
		SetNull();
		return false;
	}
	if(!UnserializeFromData(vchData))
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
void CAssetAllocationDB::WriteAssetAllocationIndex(const CAssetAllocation& assetallocation, const uint256 &txHash, int nHeight, const CAsset& asset, const CAmount& nSenderBalance, const CAmount& nAmount, const std::string& strSender) {
	if (gArgs.IsArgSet("-zmqpubassetallocation") || fAssetAllocationIndex) {
		UniValue oName(UniValue::VOBJ);
		bool isMine = true;
        const string& strReceiver = bech32::Encode(Params().Bech32HRP(),assetallocation.assetAllocationTuple.vchAddress);
		if (BuildAssetAllocationIndexerJson(assetallocation, asset, nSenderBalance, nAmount, strSender, strReceiver, isMine, oName)) {
			const string& strObj = oName.write();
			GetMainSignals().NotifySyscoinUpdate(strObj.c_str(), "assetallocation");
			if (isMine && fAssetAllocationIndex) {
				const string& strKey = txHash.GetHex()+"-"+boost::lexical_cast<string>(asset.nAsset)+"-"+ strSender +"-"+ strReceiver;
				{
					LOCK2(mempool.cs, cs_assetallocationindex);
					// we want to the height from mempool if it exists or use the one passed in
					CTxMemPool::txiter it = mempool.mapTx.find(txHash);
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

bool ResetAssetAllocation(const CAssetAllocationTuple &assetAllocationToRemove,  const uint256 &txHash, const bool &bMiner=false) {

    if(!bMiner){
        const string& receiverStr = assetAllocationToRemove.ToString();
        {
            LOCK(cs_assetallocationarrival);
        	// remove the conflict once we revert since it is assumed to be resolved on POW
        	ArrivalTimesMap &arrivalTimes = arrivalTimesMap[receiverStr];
            
        	bool removeAllConflicts = true;
        	// remove only if all arrival times are either expired (30 mins) or no more zdag transactions left for this sender
        	for(auto& arrivalTime: arrivalTimes){
        		if((chainActive.Tip()->GetMedianTimePast() - arrivalTime.second) <= 1800000){
        			removeAllConflicts = false;
        			break;
        		}
        	}
        	if(removeAllConflicts){
                arrivalTimesMap.erase(receiverStr);
                sorted_vector<string>::const_iterator it = assetAllocationConflicts.find(receiverStr);
                if (it != assetAllocationConflicts.end()) {
                    assetAllocationConflicts.V.erase(const_iterator_cast(assetAllocationConflicts.V, it));
                }   
        	}
            else
                arrivalTimes.erase(txHash);
        }
    }
	

	return true;
	
}

bool CheckAssetAllocationInputs(const CTransaction &tx, const CCoinsViewCache &inputs, int op, const vector<vector<unsigned char> > &vvchArgs,
        bool fJustCheck, int nHeight, AssetAllocationMap &mapAssetAllocations, AssetBalanceMap &blockMapAssetBalances, string &errorMessage, bool bSanityCheck, bool bMiner) {
    if (passetallocationdb == nullptr)
		return false;
	const uint256 & txHash = tx.GetHash();
	if (!bSanityCheck)
		LogPrint(BCLog::SYS,"*** ASSET ALLOCATION %d %d %s %s\n", nHeight,
			chainActive.Tip()->nHeight, txHash.ToString().c_str(),
			fJustCheck ? "JUSTCHECK" : "BLOCK");
            

	// unserialize assetallocation from txn, check for valid
	CAssetAllocation theAssetAllocation;
	vector<unsigned char> vchData;
	int nDataOut, tmpOp;
	if(!GetSyscoinData(tx, vchData, nDataOut, tmpOp) || tmpOp != OP_SYSCOIN_ASSET_ALLOCATION || !theAssetAllocation.UnserializeFromData(vchData))
	{
		errorMessage = "SYSCOIN_ASSET_ALLOCATION_CONSENSUS_ERROR ERRCODE: 1001 - " + _("Cannot unserialize data inside of this transaction relating to an assetallocation");
		return error(errorMessage.c_str());
	}

	if(fJustCheck)
	{
		if(op == OP_ASSET_ALLOCATION_BURN && vvchArgs.size() != 3)
		{
			errorMessage = "SYSCOIN_ASSET_ALLOCATION_CONSENSUS_ERROR: ERRCODE: 1002 - " + _("Asset arguments incorrect size");
			return error(errorMessage.c_str());
		}		
		
	}
	string retError = "";
	if(fJustCheck)
	{
		switch (op) {
		case OP_ASSET_ALLOCATION_SEND:
			if (theAssetAllocation.listSendingAllocationAmounts.empty())
			{
				errorMessage = "SYSCOIN_ASSET_ALLOCATION_CONSENSUS_ERROR: ERRCODE: 1004 - " + _("Asset send must send an input or transfer balance");
				return error(errorMessage.c_str());
			}
			if (theAssetAllocation.listSendingAllocationAmounts.size() > 250)
			{
				errorMessage = "SYSCOIN_ASSET_ALLOCATION_CONSENSUS_ERROR: ERRCODE: 1005 - " + _("Too many receivers in one allocation send, maximum of 250 is allowed at once");
				return error(errorMessage.c_str());
			}
			break;
		case OP_ASSET_ALLOCATION_BURN:
			if (theAssetAllocation.listSendingAllocationAmounts.size() != 1)
			{
				errorMessage = "SYSCOIN_ASSET_ALLOCATION_CONSENSUS_ERROR: ERRCODE: 1007 - " + _("Must send exactly one output to burn transaction");
				return error(errorMessage.c_str());
			}
			break;
		default:
			errorMessage = "SYSCOIN_ASSET_ALLOCATION_CONSENSUS_ERROR: ERRCODE: 1009 - " + _("Asset transaction has unknown op");
			return error(errorMessage.c_str());
		}
	}
	const string &user1 = bech32::Encode(Params().Bech32HRP(),theAssetAllocation.assetAllocationTuple.vchAddress);
	const CAssetAllocationTuple &assetAllocationTuple = theAssetAllocation.assetAllocationTuple;
    const string & senderTupleStr = assetAllocationTuple.ToString();

	CAssetAllocation dbAssetAllocation;
	CAsset dbAsset;
	bool bReset = false;
	bool bBalanceOverrun = false;
	bool bAddAllReceiversToConflictList = false;
	if (op == OP_ASSET_ALLOCATION_BURN)
	{		
    
		if (!GetAssetAllocation(assetAllocationTuple, dbAssetAllocation))
		{
			errorMessage = "SYSCOIN_ASSET_ALLOCATION_CONSENSUS_ERROR: ERRCODE: 1010 - " + _("Cannot find asset allocation to burn");
			return error(errorMessage.c_str());
		}	
		if (dbAssetAllocation.assetAllocationTuple != theAssetAllocation.assetAllocationTuple || !FindAssetOwnerInTx(inputs, tx, user1))
		{
			errorMessage = "SYSCOIN_ASSET_ALLOCATION_CONSENSUS_ERROR: ERRCODE: 1015 - " + _("Cannot send this asset. Asset allocation owner must sign off on this change");
			return error(errorMessage.c_str());
		}
		if(assetAllocationTuple.nAsset != boost::lexical_cast<int>(stringFromVch(vvchArgs[0])))
		{
			errorMessage = "SYSCOIN_ASSET_ALLOCATION_CONSENSUS_ERROR: ERRCODE: 1010 - " + _("Invalid asset details entered in the script output");
			return error(errorMessage.c_str());
		}		
		if (!GetAsset(dbAssetAllocation.assetAllocationTuple.nAsset, dbAsset))
		{
			errorMessage = "SYSCOIN_ASSET_ALLOCATION_CONSENSUS_ERROR: ERRCODE: 1011 - " + _("Failed to read from asset DB");
			return error(errorMessage.c_str());
		}
		if(dbAsset.vchContract.empty() || dbAsset.vchContract != vvchArgs[2])
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
        if(AssetAmountFromValueNonNeg(stringFromVch(vvchArgs[1]), dbAsset.nPrecision) != amountTuple.second)
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
			bReset = !fJustCheck;
			if (bReset) {
				if (!ResetAssetAllocation(assetAllocationTuple, txHash))
				{
					errorMessage = "SYSCOIN_ASSET_ALLOCATION_CONSENSUS_ERROR: ERRCODE: 1014 - " + _("Failed to revert asset allocation");
					return error(errorMessage.c_str());
                }
            }
        }
        
        theAssetAllocation.nBalance = dbAssetAllocation.nBalance;
        
        CAmount mapBalanceSenderCopy;
        if(fJustCheck){
            LOCK(cs_assetallocation);
            AssetBalanceMap::iterator mapBalanceSender = mempoolMapAssetBalances.find(senderTupleStr);
            if(mapBalanceSender == mempoolMapAssetBalances.end()){
                mempoolMapAssetBalances.emplace(std::make_pair(senderTupleStr, theAssetAllocation.nBalance));
                mapBalanceSenderCopy = theAssetAllocation.nBalance;
            }  
            else{
                mapBalanceSenderCopy = mapBalanceSender->second;
            } 
        } else{
            AssetBalanceMap::iterator mapBalanceSender = blockMapAssetBalances.find(senderTupleStr);
            if(mapBalanceSender == blockMapAssetBalances.end()){
                blockMapAssetBalances.emplace(std::make_pair(senderTupleStr, theAssetAllocation.nBalance));
                mapBalanceSenderCopy = theAssetAllocation.nBalance;
            }  
            else{
                mapBalanceSenderCopy = mapBalanceSender->second;
            } 
        }          
        
		const CAmount &nBalanceAfterSend = mapBalanceSenderCopy - amountTuple.second;
		if (nBalanceAfterSend < 0) {
			errorMessage = "SYSCOIN_ASSET_ALLOCATION_CONSENSUS_ERROR: ERRCODE: 1016 - " + _("Sender balance is insufficient");
			return error(errorMessage.c_str());
		}
		if (!fJustCheck && !bSanityCheck) {
			const CAssetAllocationTuple receiverAllocationTuple(theAssetAllocation.assetAllocationTuple.nAsset, amountTuple.first);
            if (bReset) {
                if (!ResetAssetAllocation(receiverAllocationTuple, txHash, bMiner))
                {
                    errorMessage = "SYSCOIN_ASSET_ALLOCATION_CONSENSUS_ERROR: ERRCODE: 1019 - " + _("Failed to revert asset allocation");
                    return error(errorMessage.c_str());
                }
            } 
			CAssetAllocation receiverAllocation;
			if (!GetAssetAllocation(receiverAllocationTuple, receiverAllocation)) {
				receiverAllocation.assetAllocationTuple = receiverAllocationTuple;
			}
         
            passetallocationdb->WriteAssetAllocationIndex(receiverAllocation, txHash, nHeight, dbAsset, nBalanceAfterSend, amountTuple.second, user1);
            const string& receiverTupleStr = receiverAllocationTuple.ToString();
            if(fJustCheck){
                LOCK(cs_assetallocation);
                AssetBalanceMap::iterator mapBalanceReceiver = mempoolMapAssetBalances.find(receiverTupleStr);
                if(mapBalanceReceiver == mempoolMapAssetBalances.end()){
                    receiverAllocation.nBalance += amountTuple.second;
                    mempoolMapAssetBalances.emplace(std::make_pair(receiverTupleStr, receiverAllocation.nBalance)); 
                }
                else{
                    mapBalanceReceiver->second += amountTuple.second;
                    receiverAllocation.nBalance = mapBalanceReceiver->second;
                }
                
                AssetBalanceMap::iterator mapBalanceSender = mempoolMapAssetBalances.find(senderTupleStr);                        
                if(mapBalanceSender == mempoolMapAssetBalances.end()){
                    theAssetAllocation.nBalance -= amountTuple.second; 
                    mempoolMapAssetBalances.emplace(std::make_pair(senderTupleStr, theAssetAllocation.nBalance));
                }  
                else{
                    mapBalanceSender->second -= amountTuple.second;
                    theAssetAllocation.nBalance = mapBalanceSender->second; 
                }  
            }else{
                AssetBalanceMap::iterator mapBalanceReceiver = blockMapAssetBalances.find(receiverTupleStr);
                if(mapBalanceReceiver == blockMapAssetBalances.end()){
                    receiverAllocation.nBalance += amountTuple.second;
                    blockMapAssetBalances.emplace(std::make_pair(receiverTupleStr, receiverAllocation.nBalance)); 
                }
                else{
                    mapBalanceReceiver->second += amountTuple.second;
                    receiverAllocation.nBalance = mapBalanceReceiver->second;
                }
                
                AssetBalanceMap::iterator mapBalanceSender = blockMapAssetBalances.find(senderTupleStr);                        
                if(mapBalanceSender == blockMapAssetBalances.end()){
                    theAssetAllocation.nBalance -= amountTuple.second; 
                    blockMapAssetBalances.emplace(std::make_pair(senderTupleStr, theAssetAllocation.nBalance));
                }  
                else{
                    mapBalanceSender->second -= amountTuple.second;
                    theAssetAllocation.nBalance = mapBalanceSender->second; 
                } 
            }
            auto rv = mapAssetAllocations.emplace(std::move(receiverTupleStr), std::move(receiverAllocation));
            if (!rv.second)
                rv.first->second = std::move(receiverAllocation);
                
            
		}else if (!bSanityCheck) {
            LOCK(cs_assetallocationarrival);
			// add conflicting sender if using ZDAG
			assetAllocationConflicts.insert(senderTupleStr);
		}
	}
	else if (op == OP_ASSET_ALLOCATION_SEND)
	{
		if (!GetAssetAllocation(assetAllocationTuple, dbAssetAllocation))
		{
			errorMessage = "SYSCOIN_ASSET_ALLOCATION_CONSENSUS_ERROR: ERRCODE: 1015 - " + _("Cannot find sender asset allocation.");
			return error(errorMessage.c_str());
		}
		if (dbAssetAllocation.assetAllocationTuple != theAssetAllocation.assetAllocationTuple || !FindAssetOwnerInTx(inputs, tx, user1))
		{
			errorMessage = "SYSCOIN_ASSET_ALLOCATION_CONSENSUS_ERROR: ERRCODE: 1015a - " + _("Cannot send this asset. Asset allocation owner must sign off on this change");
			return error(errorMessage.c_str());
		}	
		if (!GetAsset(assetAllocationTuple.nAsset, dbAsset))
		{
			errorMessage = "SYSCOIN_ASSET_ALLOCATION_CONSENSUS_ERROR: ERRCODE: 1016 - " + _("Failed to read from asset DB");
			return error(errorMessage.c_str());
		}
		if (!bSanityCheck) {
			bReset = !fJustCheck;
			if (bReset) {
				if (!ResetAssetAllocation(assetAllocationTuple, txHash, bMiner))
				{
					errorMessage = "SYSCOIN_ASSET_ALLOCATION_CONSENSUS_ERROR: ERRCODE: 1014 - " + _("Failed to revert asset allocation");
					return error(errorMessage.c_str());
				}
			}
		}     
		theAssetAllocation.nBalance = dbAssetAllocation.nBalance;
		
		// check balance is sufficient on sender
		CAmount nTotal = 0;
		for (const auto& amountTuple : theAssetAllocation.listSendingAllocationAmounts) {
			const CAssetAllocationTuple receiverAllocationTuple(theAssetAllocation.assetAllocationTuple.nAsset, amountTuple.first);
			
			if (bReset) {
				if (!ResetAssetAllocation(receiverAllocationTuple, txHash, bMiner))
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
        CAmount mapBalanceSenderCopy;
        
        if(fJustCheck){
            LOCK(cs_assetallocation);
            AssetBalanceMap::iterator mapBalanceSender = mempoolMapAssetBalances.find(senderTupleStr);
            if(mapBalanceSender == mempoolMapAssetBalances.end()){
                mempoolMapAssetBalances.emplace(std::make_pair(senderTupleStr, dbAssetAllocation.nBalance));
                mapBalanceSenderCopy = dbAssetAllocation.nBalance;
            }  
            else{
                mapBalanceSenderCopy = mapBalanceSender->second;
            } 
        } else{
            AssetBalanceMap::iterator mapBalanceSender = blockMapAssetBalances.find(senderTupleStr);
            if(mapBalanceSender == blockMapAssetBalances.end()){
                blockMapAssetBalances.emplace(std::make_pair(senderTupleStr, dbAssetAllocation.nBalance));
                mapBalanceSenderCopy = dbAssetAllocation.nBalance;
            }  
            else{
                mapBalanceSenderCopy = mapBalanceSender->second;
            }
        } 
		const CAmount &nBalanceAfterSend = mapBalanceSenderCopy - nTotal;
		if (nBalanceAfterSend < 0) {
			bBalanceOverrun = true;
			if(bSanityCheck)
				errorMessage = "SYSCOIN_ASSET_ALLOCATION_CONSENSUS_ERROR: ERRCODE: 1021 - " + _("Sender balance is insufficient");
			if (fJustCheck && !bSanityCheck) {
                LOCK(cs_assetallocationarrival);
				// add conflicting sender
				assetAllocationConflicts.insert(senderTupleStr);
			}
		}
		else if (fJustCheck) {
            LOCK(cs_assetallocationarrival);
			// if sender was is flagged as conflicting, add all receivers to conflict list
			if (assetAllocationConflicts.find(senderTupleStr) != assetAllocationConflicts.end())
			{			
				bAddAllReceiversToConflictList = true;
			}
		}
		for (const auto& amountTuple : theAssetAllocation.listSendingAllocationAmounts) {
           
			CAssetAllocation receiverAllocation;
			if (amountTuple.first == theAssetAllocation.assetAllocationTuple.vchAddress) {
				errorMessage = "SYSCOIN_ASSET_ALLOCATION_CONSENSUS_ERROR: ERRCODE: 1022 - " + _("Cannot send an asset allocation to yourself");
				return error(errorMessage.c_str());
			}
			if (!bSanityCheck) {
				const CAssetAllocationTuple receiverAllocationTuple(theAssetAllocation.assetAllocationTuple.nAsset, amountTuple.first);
                const string &receiverTupleStr = receiverAllocationTuple.ToString();

				if (fJustCheck) {
					if (bAddAllReceiversToConflictList || bBalanceOverrun) {
                        LOCK(cs_assetallocationarrival);
						assetAllocationConflicts.insert(receiverTupleStr);
					}
				}
               
				if (!bBalanceOverrun) {
					CAssetAllocation receiverAllocation;
					if (!GetAssetAllocation(receiverAllocationTuple, receiverAllocation)) {
                        if(!fJustCheck){
							receiverAllocation.assetAllocationTuple = receiverAllocationTuple;
                        }
					} 					
				
                    
                    if(fJustCheck){
                        LOCK(cs_assetallocation);
                        AssetBalanceMap::iterator mapBalanceReceiver = mempoolMapAssetBalances.find(receiverTupleStr);
                        if(mapBalanceReceiver == mempoolMapAssetBalances.end()){
                            receiverAllocation.nBalance += amountTuple.second;
                            mempoolMapAssetBalances.emplace(std::make_pair(receiverTupleStr, receiverAllocation.nBalance)); 
                        }
                        else{
                            mapBalanceReceiver->second += amountTuple.second;
                            receiverAllocation.nBalance = mapBalanceReceiver->second;
                        }
                                                
                        AssetBalanceMap::iterator mapBalanceSender = mempoolMapAssetBalances.find(senderTupleStr);
                        if(mapBalanceSender == mempoolMapAssetBalances.end()){
                            theAssetAllocation.nBalance -= amountTuple.second; 
                            mempoolMapAssetBalances.emplace(std::make_pair(senderTupleStr, theAssetAllocation.nBalance));
                        }  
                        else{
                            mapBalanceSender->second -= amountTuple.second;
                            theAssetAllocation.nBalance = mapBalanceSender->second; 
                        } 
                    }
                    else{
                   
                        AssetBalanceMap::iterator mapBalanceReceiver = blockMapAssetBalances.find(receiverTupleStr);
                        if(mapBalanceReceiver == blockMapAssetBalances.end()){
                            receiverAllocation.nBalance += amountTuple.second;
                            blockMapAssetBalances.emplace(std::make_pair(receiverTupleStr, receiverAllocation.nBalance)); 
                        }
                        else{
                            mapBalanceReceiver->second += amountTuple.second;
                            receiverAllocation.nBalance = mapBalanceReceiver->second;
                        }
                                                
                        AssetBalanceMap::iterator mapBalanceSender = blockMapAssetBalances.find(senderTupleStr);
                        if(mapBalanceSender == blockMapAssetBalances.end()){
                            theAssetAllocation.nBalance -= amountTuple.second; 
                            blockMapAssetBalances.emplace(std::make_pair(senderTupleStr, theAssetAllocation.nBalance));
                        }  
                        else{
                            mapBalanceSender->second -= amountTuple.second;
                            theAssetAllocation.nBalance = mapBalanceSender->second; 
                        } 
                    }                                               
                    
                    if(!fJustCheck){
    
                        passetallocationdb->WriteAssetAllocationIndex(receiverAllocation, txHash, nHeight, dbAsset, nBalanceAfterSend, amountTuple.second, user1);
                        
                        auto rv = mapAssetAllocations.emplace(std::move(receiverTupleStr), std::move(receiverAllocation));
                        if (!rv.second)
                            rv.first->second = std::move(receiverAllocation);                                                  
                    }
				}
			}
		}	
	}
	// write assetallocation  
	// asset sends are the only ones confirming without PoW
	if (!bBalanceOverrun && !bSanityCheck) {
		// set the assetallocation's txn-dependent 
		if(fJustCheck && op == OP_ASSET_ALLOCATION_SEND){
            LOCK(cs_assetallocationarrival);
            ArrivalTimesMap &arrivalTimes = arrivalTimesMap[senderTupleStr];
            arrivalTimes[txHash] = GetTimeMillis();
        
            
        }
        else if(!fJustCheck){
    		theAssetAllocation.listSendingAllocationAmounts.clear();
            passetallocationdb->WriteAssetAllocationIndex(theAssetAllocation, txHash, nHeight, dbAsset, theAssetAllocation.nBalance, 0, ""); 
            auto rv = mapAssetAllocations.emplace(std::move(senderTupleStr), std::move(theAssetAllocation));
            if (!rv.second)
                rv.first->second = std::move(theAssetAllocation);

    		LogPrint(BCLog::SYS,"CONNECTED ASSET ALLOCATION: op=%s assetallocation=%s hash=%s height=%d fJustCheck=%d\n",
    				assetAllocationFromOp(op).c_str(),
    				senderTupleStr.c_str(),
    				txHash.ToString().c_str(),
    				nHeight,
    				fJustCheck ? 1 : 0);
        }
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

	const int &nAsset = params[0].get_int();
	string strAddress = params[1].get_str();

	string strAddressFrom;
	const CTxDestination &addressFrom = DecodeDestination(strAddress);
	strAddressFrom = strAddress;
	
	CAssetAllocation theAssetAllocation;
	const CAssetAllocationTuple assetAllocationTuple(nAsset, bech32::Decode(strAddress).second);
	if (!GetAssetAllocation(assetAllocationTuple, theAssetAllocation))
		throw runtime_error("SYSCOIN_ASSET_ALLOCATION_RPC_ERROR: ERRCODE: 1500 - " + _("Could not find a asset allocation with this key"));

	CAsset theAsset;
	if (!GetAsset(nAsset, theAsset))
		throw runtime_error("SYSCOIN_ASSET_ALLOCATION_RPC_ERROR: ERRCODE: 1501 - " + _("Could not find a asset with this key"));

	CScript scriptPubKeyFromOrig;
	if (!strAddressFrom.empty()) {
		scriptPubKeyFromOrig = GetScriptForDestination(addressFrom);
	}
    
	CAmount amount = AssetAmountFromValueNonNeg(params[2], theAsset.nPrecision);
	
	theAssetAllocation.ClearAssetAllocation();
	theAssetAllocation.assetAllocationTuple = assetAllocationTuple;
    string burnAddr = "burn";
	theAssetAllocation.listSendingAllocationAmounts.push_back(make_pair(vchFromStringUint8("burn"), amount));

    vector<unsigned char> data;
    theAssetAllocation.Serialize(data);

	CScript scriptPubKey;
	scriptPubKey << CScript::EncodeOP_N(OP_SYSCOIN_ASSET_ALLOCATION) << CScript::EncodeOP_N(OP_ASSET_ALLOCATION_BURN) << vchFromString(boost::lexical_cast<string>(nAsset)) << vchFromString(ValueFromAssetAmount(amount,theAsset.nPrecision).getValStr()) << theAsset.vchContract << OP_2DROP << OP_2DROP << OP_2DROP;
	scriptPubKey += scriptPubKeyFromOrig;
	// send the asset pay txn
	vector<CRecipient> vecSend;
	CRecipient recipient;
	CreateRecipient(scriptPubKey, recipient);
	vecSend.push_back(recipient);

	CScript scriptData;
	scriptData << OP_RETURN << CScript::EncodeOP_N(OP_SYSCOIN_ASSET_ALLOCATION) << data;
	CRecipient fee;
	CreateFeeRecipient(scriptData, fee);
	vecSend.push_back(fee);

	return syscointxfund_helper("", vecSend);
}
UniValue assetallocationsend(const JSONRPCRequest& request) {
    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    CWallet* const pwallet = wallet.get();
	const UniValue &params = request.params;
	if (request.fHelp || params.size() != 4)
		throw runtime_error(
			"assetallocationsend [asset] [owner] ([{\"ownerto\":\"address\",\"amount\":amount},...] [witness]\n"
			"Send an asset allocation you own to another address. Maximimum recipients is 250.\n"
			"<asset> Asset guid.\n"
			"<owner> Address that owns this asset allocation.\n"
			"<ownerto> Address to transfer to.\n"
			"<amount> Quantity of asset to send.\n"
			"<witness> Witness address that will sign for web-of-trust notarization of this transaction.\n"
			+ HelpRequiringPassphrase(pwallet));

	// gather & validate inputs
	const int &nAsset = params[0].get_int();
	string vchAddressFrom = params[1].get_str();
	UniValue valueTo = params[2];
	vector<unsigned char> vchWitness;
    string strWitness = params[3].get_str();
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
	CAssetAllocation theAssetAllocation;
	const CAssetAllocationTuple assetAllocationTuple(nAsset, strAddress == "burn"? vchFromStringUint8("burn"): bech32::Decode(strAddress).second);
	if (!GetAssetAllocation(assetAllocationTuple, theAssetAllocation))
		throw runtime_error("SYSCOIN_ASSET_ALLOCATION_RPC_ERROR: ERRCODE: 1500 - " + _("Could not find a asset allocation with this key"));

	CAsset theAsset;
	if (!GetAsset(nAsset, theAsset))
		throw runtime_error("SYSCOIN_ASSET_ALLOCATION_RPC_ERROR: ERRCODE: 1501 - " + _("Could not find a asset with this key"));

	theAssetAllocation.ClearAssetAllocation();
	theAssetAllocation.assetAllocationTuple = assetAllocationTuple;
	UniValue receivers = valueTo.get_array();
	
	for (unsigned int idx = 0; idx < receivers.size(); idx++) {
		const UniValue& receiver = receivers[idx];
		if (!receiver.isObject())
			throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "expected object with {\"ownerto'\" or \"amount\"}");

		const UniValue &receiverObj = receiver.get_obj();
		const string &toStr = find_value(receiverObj, "ownerto").get_str();
        const CTxDestination &dest = DecodeDestination(toStr);
		if (!IsValidDestination(dest))
			throw runtime_error("SYSCOIN_ASSET_ALLOCATION_RPC_ERROR: ERRCODE: 1502 - " + _("Asset must be sent to a valid syscoin address"));
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

	CScript scriptPubKeyFromOrig;
	if (!strAddressFrom.empty()) {
		scriptPubKeyFromOrig = GetScriptForDestination(addressFrom);
	}
    
	CScript scriptPubKey;
    {
        LOCK(cs_assetallocationarrival);
    	// check to see if a transaction for this asset/address tuple has arrived before minimum latency period
    	const ArrivalTimesMap &arrivalTimes = arrivalTimesMap[assetAllocationTuple.ToString()];
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
    }

	vector<unsigned char> data;
	theAssetAllocation.Serialize(data);   

	scriptPubKey << CScript::EncodeOP_N(OP_SYSCOIN_ASSET_ALLOCATION) << CScript::EncodeOP_N(OP_ASSET_ALLOCATION_SEND) << OP_2DROP;
	scriptPubKey += scriptPubKeyFromOrig;
	// send the asset pay txn
	vector<CRecipient> vecSend;
	CRecipient recipient;
	CreateRecipient(scriptPubKey, recipient);
	vecSend.push_back(recipient);	
	
	CScript scriptData;
	scriptData << OP_RETURN << CScript::EncodeOP_N(OP_SYSCOIN_ASSET_ALLOCATION) << data;
	CRecipient fee;
	CreateFeeRecipient(scriptData, fee);
	vecSend.push_back(fee);

	return syscointxfund_helper(strWitness, vecSend);
}


UniValue assetallocationinfo(const JSONRPCRequest& request) {
	const UniValue &params = request.params;
    if (request.fHelp || 2 != params.size())
        throw runtime_error("assetallocationinfo <asset> <owner>\n"
                "Show stored values of a single asset allocation.\n");

    const int &nAsset = params[0].get_int();
	string strAddressFrom = params[1].get_str();
	UniValue oAssetAllocation(UniValue::VOBJ);
	const CAssetAllocationTuple assetAllocationTuple(nAsset, strAddressFrom == "burn"? vchFromStringUint8("burn"): bech32::Decode(strAddressFrom).second);
	CAssetAllocation txPos;
	if (passetallocationdb == nullptr || !passetallocationdb->ReadAssetAllocation(assetAllocationTuple, txPos))
		throw runtime_error("SYSCOIN_ASSET_ALLOCATION_RPC_ERROR: ERRCODE: 1507 - " + _("Failed to read from assetallocation DB"));

	CAsset theAsset;
	if (!GetAsset(nAsset, theAsset))
		throw runtime_error("SYSCOIN_ASSET_ALLOCATION_RPC_ERROR: ERRCODE: 1508 - " + _("Could not find a asset with this key"));


	if(!BuildAssetAllocationJson(txPos, theAsset, oAssetAllocation))
		oAssetAllocation.clear();
    return oAssetAllocation;
}
int DetectPotentialAssetAllocationSenderConflicts(const CAssetAllocationTuple& assetAllocationTupleSender, const uint256& lookForTxHash) {
	CAssetAllocation dbAssetAllocation;
	// get last POW asset allocation balance to ensure we use POW balance to check for potential conflicts in mempool (real-time balances).
	// The idea is that real-time spending amounts can in some cases overrun the POW balance safely whereas in some cases some of the spends are 
	// put in another block due to not using enough fees or for other reasons that miners don't mine them.
	// We just want to flag them as level 1 so it warrants deeper investigation on receiver side if desired (if fund amounts being transferred are not negligible)
	if (passetallocationdb == nullptr || !passetallocationdb->ReadAssetAllocation(assetAllocationTupleSender, dbAssetAllocation))
		return ZDAG_NOT_FOUND;
        

	// ensure that this transaction exists in the arrivalTimes DB (which is the running stored lists of all real-time asset allocation sends not in POW)
	// the arrivalTimes DB is only added to for valid asset allocation sends that happen in real-time and it is removed once there is POW on that transaction
    const ArrivalTimesMap& arrivalTimes = arrivalTimesMap[assetAllocationTupleSender.ToString()];
	if(arrivalTimes.empty())
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
	senderBalance = dbAssetAllocation.nBalance;
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

	const int &nAsset = params[0].get_int();
	string vchAddressSender = params[1].get_str();
	const string &strAddressSender = vchAddressSender;
	uint256 txid;
	txid.SetNull();
	if(!params[2].get_str().empty())
		txid.SetHex(params[2].get_str());
	UniValue oAssetAllocationStatus(UniValue::VOBJ);
    LOCK2(cs_main, mempool.cs);
    LOCK(cs_assetallocationarrival);
    
    const int64_t & nNow = GetTimeMillis();
	const CAssetAllocationTuple assetAllocationTupleSender(nAsset, bech32::Decode(strAddressSender).second);
    
       // if arrival times have expired, then expire any conflicting status for this sender as well
    const ArrivalTimesMap &arrivalTimes = arrivalTimesMap[assetAllocationTupleSender.ToString()];
    bool allArrivalsExpired = true;
    for (auto& arrivalTime : arrivalTimes) {
        // if its been less than 30m then we keep conflict status
        if((nNow - arrivalTime.second) <= 1800000){
            allArrivalsExpired = false;
            break;
        }   
    }   
 
    if(allArrivalsExpired){
        arrivalTimesMap.erase(assetAllocationTupleSender.ToString());
        sorted_vector<string>::const_iterator it = assetAllocationConflicts.find(assetAllocationTupleSender.ToString());
        if (it != assetAllocationConflicts.end()) {
            assetAllocationConflicts.V.erase(const_iterator_cast(assetAllocationConflicts.V, it));
        }        
    }
    
	int nStatus = ZDAG_STATUS_OK;
	if (assetAllocationConflicts.find(assetAllocationTupleSender.ToString()) != assetAllocationConflicts.end())
		nStatus = ZDAG_MAJOR_CONFLICT;
	else {
		nStatus = DetectPotentialAssetAllocationSenderConflicts(assetAllocationTupleSender, txid);
	}
	oAssetAllocationStatus.pushKV("status", nStatus);
	return oAssetAllocationStatus;
}
bool BuildAssetAllocationJson(CAssetAllocation& assetallocation, const CAsset& asset, UniValue& oAssetAllocation)
{
    CAmount nBalanceZDAG = assetallocation.nBalance;
    const string &allocationTupleStr = assetallocation.assetAllocationTuple.ToString();
    {
        LOCK(cs_assetallocation);
        AssetBalanceMap::iterator mapIt =  mempoolMapAssetBalances.find(allocationTupleStr);
        if(mapIt != mempoolMapAssetBalances.end())
            nBalanceZDAG = mapIt->second;
    }
    oAssetAllocation.pushKV("_id", allocationTupleStr);
	oAssetAllocation.pushKV("asset", assetallocation.assetAllocationTuple.nAsset);
	oAssetAllocation.pushKV("owner",  bech32::Encode(Params().Bech32HRP(),assetallocation.assetAllocationTuple.vchAddress));
	oAssetAllocation.pushKV("balance", ValueFromAssetAmount(assetallocation.nBalance, asset.nPrecision));
    oAssetAllocation.pushKV("balance_zdag", ValueFromAssetAmount(nBalanceZDAG, asset.nPrecision));
	return true;
}
bool BuildAssetAllocationIndexerJson(const CAssetAllocation& assetallocation, const CAsset& asset, const CAmount& nSenderBalance, const CAmount& nAmount, const string& strSender, const string& strReceiver, bool &isMine, UniValue& oAssetAllocation)
{
	CAmount nAmountDisplay = nAmount;   
	oAssetAllocation.pushKV("_id", assetallocation.assetAllocationTuple.ToString());
	oAssetAllocation.pushKV("asset", assetallocation.assetAllocationTuple.nAsset);
	oAssetAllocation.pushKV("sender", strSender);
	oAssetAllocation.pushKV("sender_balance", ValueFromAssetAmount(nSenderBalance, asset.nPrecision));
	oAssetAllocation.pushKV("receiver", strReceiver);
	oAssetAllocation.pushKV("receiver_balance", ValueFromAssetAmount(assetallocation.nBalance, asset.nPrecision));
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
	oAssetAllocation.pushKV("amount", ValueFromAssetAmount(nAmountDisplay, asset.nPrecision));
	return true;
}
void AssetAllocationTxToJSON(const int op, const std::vector<unsigned char> &vchData, UniValue &entry)
{
	string opName = assetAllocationFromOp(op);
	CAssetAllocation assetallocation;
	if(!assetallocation.UnserializeFromData(vchData))
		return;
	CAsset dbAsset;
	GetAsset(assetallocation.assetAllocationTuple.nAsset, dbAsset);

	entry.pushKV("txtype", opName);
	entry.pushKV("_id", assetallocation.assetAllocationTuple.ToString());
	entry.pushKV("asset", assetallocation.assetAllocationTuple.nAsset);
	entry.pushKV("owner", bech32::Encode(Params().Bech32HRP(), assetallocation.assetAllocationTuple.vchAddress));
	UniValue oAssetAllocationReceiversArray(UniValue::VARR);
	if (!assetallocation.listSendingAllocationAmounts.empty()) {
		for (auto& amountTuple : assetallocation.listSendingAllocationAmounts) {
			UniValue oAssetAllocationReceiversObj(UniValue::VOBJ);
			oAssetAllocationReceiversObj.pushKV("owner", bech32::Encode(Params().Bech32HRP(),amountTuple.first));
			oAssetAllocationReceiversObj.pushKV("amount", ValueFromAssetAmount(amountTuple.second, dbAsset.nPrecision));
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
	}
	int index = 0;
	UniValue assetValue;
	vector<string> contents;
	contents.reserve(5);
    LOCK(cs_assetallocationindex);
	for (auto&indexObj : boost::adaptors::reverse(AssetAllocationIndex)) {
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
bool CAssetAllocationDB::Flush(const AssetAllocationMap &mapAssetAllocations){
    if(mapAssetAllocations.empty())
        return true;
    CDBBatch batch(*this);
    for (const auto &key : mapAssetAllocations) {
        const CAssetAllocation &assetallocation = key.second;
        batch.Write(make_pair(assetAllocationKey, assetallocation.assetAllocationTuple), assetallocation);
    }
    LogPrint(BCLog::SYS, "Flushing %d asset allocations\n", mapAssetAllocations.size());
    return WriteBatch(batch);
}
bool CAssetAllocationDB::ScanAssetAllocations(const int count, const int from, const UniValue& oOptions, UniValue& oRes) {
	string strTxid = "";
	vector<vector<uint8_t> > vchAddresses;
	int32_t nAsset = 0;
	if (!oOptions.isNull()) {
		const UniValue &assetObj = find_value(oOptions, "asset");
		if(assetObj.isNum()) {
			nAsset = boost::lexical_cast<int32_t>(assetObj.get_int());
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
	}

	boost::scoped_ptr<CDBIterator> pcursor(NewIterator());
	pcursor->SeekToFirst();
	CAssetAllocation txPos;
	pair<string, CAssetAllocationTuple > key;
	CAsset theAsset;
	int index = 0;
	while (pcursor->Valid()) {
		boost::this_thread::interruption_point();
		try {
			if (pcursor->GetKey(key) && key.first == assetAllocationKey && (nAsset == 0 || nAsset != key.second.nAsset)) {
				pcursor->GetValue(txPos);
				if (!vchAddresses.empty() && std::find(vchAddresses.begin(), vchAddresses.end(), txPos.assetAllocationTuple.vchAddress) == vchAddresses.end())
				{
					pcursor->Next();
					continue;
				}
				UniValue oAssetAllocation(UniValue::VOBJ);
				if (!BuildAssetAllocationJson(txPos, theAsset, oAssetAllocation)) 
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
			"	   \"asset\":guid					(number) Asset GUID to filter.\n"
			"	   \"senders\"						(array) a json array with senders\n"
			"		[\n"
			"			{\n"
			"				\"sender\":string		(string) Sender address to filter.\n"
			"			} \n"
			"			,...\n"
			"		]\n"
			"	   \"receivers\"					(array) a json array with receivers\n"
			"		[\n"
			"			{\n"
			"				\"receiver\":string		(string) Receiver address to filter.\n"
			"			} \n"
			"			,...\n"
			"		]\n"
			"    }\n"
			+ HelpExampleCli("listassetallocationtransactions", "0 10")
			+ HelpExampleCli("listassetallocationtransactions", "0 0 '{\"asset\":343773}'")
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
			"[count]          (numeric, optional, default=10) The number of results to return.\n"
			"[from]           (numeric, optional, default=0) The number of results to skip.\n"
			"[options]        (array, optional) A json object with options to filter results\n"
			"    {\n"
			"	   \"asset\":guid					(number) Asset GUID to filter.\n"
			"	   \"receivers\"					(array) a json array with receivers\n"
			"		[\n"
			"			{\n"
			"				\"receiver\":string		(string) Receiver address to filter.\n"
			"			} \n"
			"			,...\n"
			"		]\n"
			"    }\n"
			+ HelpExampleCli("listassetallocations", "0")
			+ HelpExampleCli("listassetallocations", "10 10")
			+ HelpExampleCli("listassetallocations", "0 0 '{\"asset\":92922}'")
			+ HelpExampleCli("listassetallocations", "0 0 '{\"receivers\":[{\"receiver\":\"SfaMwYY19Dh96B9qQcJQuiNykVRTzXMsZR\"},{\"receiver\":\"SfaMwYY19Dh96B9qQcJQuiNykVRTzXMsZR\"}]}'")
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
	UniValue oRes(UniValue::VARR);
	if (!passetallocationdb->ScanAssetAllocations(count, from, options, oRes))
		throw runtime_error("SYSCOIN_ASSET_ALLOCATION_RPC_ERROR: ERRCODE: 1510 - " + _("Scan failed"));
	return oRes;
}
