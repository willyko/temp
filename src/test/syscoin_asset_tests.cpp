// Copyright (c) 2016-2018 The Syscoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "test/test_syscoin_services.h"
#include "utiltime.h"
#include "util.h"
#include "rpc/server.h"
#include "services/asset.h"
#include "base58.h"
#include "chainparams.h"
#include <boost/test/unit_test.hpp>
#include <boost/lexical_cast.hpp>
#include <iterator>
#include "core_io.h"
#include <key.h>
#include <math.h>
#include <key_io.h>
using namespace std;
BOOST_GLOBAL_FIXTURE( SyscoinTestingSetup );
BOOST_FIXTURE_TEST_SUITE (syscoin_asset_tests, BasicSyscoinTestingSetup)



BOOST_AUTO_TEST_CASE(generate_big_assetdata)
{
	RandomInit();
	ECC_Start();
	StartNodes();
	GenerateSpendableCoins();
	printf("Running generate_big_assetdata...\n");
	GenerateBlocks(5);
	string newaddress = GetNewFundedAddress("node1");
	// 256 bytes long
	string gooddata = "SfsddfdfsdsfSfsdfdfsdsfDsdsdsdsfsfsdsfsdsfdsfsdsfdsfsdsfsdSfsdfdfsdsfSfsdfdfsdsfDsdsdsdsfsfsdsfsdsfdsfsdsfdsfsdsfsdSfsdfdfsdsfSfsdfdfsdsfDsdsdsdsfsfsdsfsdsfdsfsdsfdsfsdsfsdSfsdfdfsdsfSfsdfdfsdsfDsdsdsdsfsfsdsfsdsfdsfsdsfdsfsdsfsdSfsdfdfsdsfSfsdfdfsdsDfdfdd";
	// 257 bytes long
	UniValue r;
	string baddata = gooddata + "a";
	string guid = AssetNew("node1", newaddress, gooddata);
	BOOST_CHECK_NO_THROW(r = CallRPC("node1", "listassets"));
	UniValue rArray = r.get_array();
	BOOST_CHECK(rArray.size() > 0);
	BOOST_CHECK_EQUAL(boost::lexical_cast<string>(find_value(rArray[0].get_obj(), "_id").get_int()), guid);
	string guid1 = AssetNew("node1", newaddress, gooddata);
	BOOST_CHECK_NO_THROW(r = CallRPC("node1", "assetinfo " + guid));
	BOOST_CHECK(boost::lexical_cast<string>(find_value(r.get_obj(), "_id").get_int()) == guid);
	BOOST_CHECK_NO_THROW(r = CallRPC("node1", "assetinfo " + guid1));
	BOOST_CHECK(boost::lexical_cast<string>(find_value(r.get_obj(), "_id").get_int()) == guid1);
}
BOOST_AUTO_TEST_CASE(generate_asset_throughput)
{
    int64_t start = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
	UniValue r;
	printf("Running generate_asset_throughput...\n");
	GenerateBlocks(5, "node1");
	GenerateBlocks(5, "node3"); 
    vector<string> vecAssets;
	// setup senders and receiver node addresses
	vector<string> senders;
	vector<string> receivers;
	senders.push_back("node1");
	senders.push_back("node2");
	receivers.push_back("node3");
	BOOST_CHECK(receivers.size() == 1);
    // user modifiable variables
	
    // for every asset you add numberOfAssetSendsPerBlock tx's effectively
    int numAssets = 2000;
    BOOST_CHECK(numAssets >= 1);

    int numberOfAssetSendsPerBlock = 250;
    BOOST_CHECK(numberOfAssetSendsPerBlock >= 1 && numberOfAssetSendsPerBlock <= 250);
  
      // NOT MEANT TO BE MODIFIED! CALCULATE INSTEAD!
    const int numberOfTransactionToSend = numAssets*numberOfAssetSendsPerBlock;
    
    // make sure numberOfAssetSendsPerBlock isn't a fraction of numberOfTransactionToSend
    BOOST_CHECK((numberOfTransactionToSend % numberOfAssetSendsPerBlock) == 0);
    
    vector<string> unfundedAccounts;
    vector<string> rawSignedAssetAllocationSends;
    vector<string> vecFundedAddresses;
    GenerateBlocks((numAssets+1)/250);  
    // PHASE 1:  GENERATE UNFUNDED ADDRESSES FOR RECIPIENTS TO ASSETALLOCATIONSEND
    printf("Throughput test: Total transaction count: %d, Receivers Per Asset Allocation Transfer %d, Total Number of Assets needed %d\n\n", numberOfTransactionToSend, numberOfAssetSendsPerBlock, numAssets);
    printf("creating %d unfunded addresses...\n", numberOfAssetSendsPerBlock);
    for(int i =0;i<numberOfAssetSendsPerBlock;i++){
        BOOST_CHECK_NO_THROW(r = CallExtRPC("node1", "getnewaddress"));
        unfundedAccounts.emplace_back(r.get_str());
    }
    
   // PHASE 2:  GENERATE FUNDED ADDRESSES FOR CREATING AND SENDING ASSETS
    // create address for funding
    BOOST_CHECK_NO_THROW(r = CallExtRPC("node1", "getnewaddress"));
    string fundedAccount = r.get_str();
    printf("creating %d funded accounts for using with assetsend/assetallocationsend in subsequent steps...\n", numAssets*250);
    string sendManyString = "";
    for(int i =0;i<numAssets;i++){
        BOOST_CHECK_NO_THROW(r = CallExtRPC("node1", "getnewaddress"));
        string fundedAccount = r.get_str();
        if(sendManyString != "") 
            sendManyString += ",";
        sendManyString += "\"" + fundedAccount + "\":1";
        if(((i+1)%250)==0){
            printf("Sending funds to batch of 250 funded accounts, approx. %d batches remaining\n", (numAssets-i)/250);
            std::string strSendMany = "sendmany \"\" {" + sendManyString + "}";
            CallExtRPC("node1", "sendmany", "\"\",{" + sendManyString + "}");
            sendManyString = "";
        }
        vecFundedAddresses.push_back(fundedAccount);
    }
    if(!sendManyString.empty()){
        std::string strSendMany = "sendmany \"\" {" + sendManyString + "}";
        CallExtRPC("node1", "sendmany", "\"\",{" + sendManyString + "}");
    }
    GenerateBlocks(5);  
    
    // PHASE 3:  SET tpstestsetenabled ON ALL SENDER/RECEIVER NODES
    for (auto &sender : senders)
        BOOST_CHECK_NO_THROW(CallExtRPC(sender, "tpstestsetenabled", "true"));
    for (auto &receiver : receivers)
        BOOST_CHECK_NO_THROW(CallExtRPC(receiver, "tpstestsetenabled", "true"));
    
     // PHASE 4:  CREATE ASSETS
    // create assets needed
    printf("creating %d sender assets...\n", numAssets);
    for(int i =0;i<numAssets;i++){
    printf("num asset %d\n", i);
        BOOST_CHECK_NO_THROW(r = CallRPC("node1", "assetnew " + vecFundedAddresses[i] + " '' '' 8 250 250 31 ''"));
        UniValue arr = r.get_array();
        string guid = boost::lexical_cast<string>(arr[1].get_int());
        BOOST_CHECK_NO_THROW(r = CallRPC("node1", "syscointxfund " + arr[0].get_str() + " " + vecFundedAddresses[i]));
        arr = r.get_array();
        BOOST_CHECK_NO_THROW(r = CallRPC("node1", "signrawtransactionwithwallet " + arr[0].get_str()));
        string hex_str = find_value(r.get_obj(), "hex").get_str();
        BOOST_CHECK_NO_THROW(r = CallRPC("node1", "sendrawtransaction " + hex_str, true, false));
  
        vecAssets.push_back(guid);
    }  
    GenerateBlocks(5);
    printf("sending assets with assetsend...\n");
    // PHASE 5:  SEND ASSETS TO NEW ALLOCATIONS
    for(int i =0;i<numAssets;i++){
        BOOST_CHECK_NO_THROW(r = CallRPC("node1", "assetsend " + vecAssets[i] + " \"[{\\\"ownerto\\\":\\\"" + vecFundedAddresses[i] + "\\\",\\\"amount\\\":250}]\" ''"));
        UniValue arr = r.get_array();
        BOOST_CHECK_NO_THROW(r = CallRPC("node1", "syscointxfund " + arr[0].get_str() + " " + vecFundedAddresses[i]));
        arr = r.get_array();           
        BOOST_CHECK_NO_THROW(r = CallRPC("node1", "signrawtransactionwithwallet " + arr[0].get_str()));
        string hex_str = find_value(r.get_obj(), "hex").get_str();
        BOOST_CHECK_NO_THROW(r = CallRPC("node1", "sendrawtransaction " + hex_str, true, false));
    }  
   
	GenerateBlocks(5);
    
    // PHASE 6:  SEND ALLOCATIONS TO NEW ALLOCATIONS (UNFUNDED ADDRESSES) USING ZDAG
	printf("Creating assetallocationsend transactions...\n");
	int count = 0;
	int unfoundedAccountIndex = 0;
    int assetAllocationSendIndex = 0;
	// create vector of signed transactions
    string assetAllocationSendMany = "";
    int txCount = 0;
	for(int i =0;i<numAssets;i++){
        // send asset to numberOfAssetSendsPerBlock addresses
        string assetAllocationSendMany = "";
        // +1 to account for change output
        for (int j = 0; j < numberOfAssetSendsPerBlock; j++) {
            if(assetAllocationSendMany != "") 
                assetAllocationSendMany += ",";
            assetAllocationSendMany += "{\\\"ownerto\\\":\\\"" + unfundedAccounts[unfoundedAccountIndex++] + "\\\",\\\"amount\\\":1}";
            if(unfoundedAccountIndex >= unfundedAccounts.size())   
                unfoundedAccountIndex = 0;      
        }

        BOOST_CHECK_NO_THROW(r = CallRPC("node1", "assetallocationsend " + vecAssets[i] + " " + vecFundedAddresses[i] + " \"[" + assetAllocationSendMany + "]\" ''"));
        UniValue arr = r.get_array();
        r = CallRPC("node1", "syscointxfund " + arr[0].get_str() + " " + vecFundedAddresses[i] );
        arr = r.get_array();
        BOOST_CHECK_NO_THROW(r = CallRPC("node1", "signrawtransactionwithwallet " + arr[0].get_str()));
        string hex_str = find_value(r.get_obj(), "hex").get_str();
        rawSignedAssetAllocationSends.push_back(hex_str);                                            
    }
    BOOST_CHECK(assetAllocationSendMany.empty());
    
    // PHASE 7:  DISTRIBUTE LOAD AMONG SENDERS
    // push vector of signed transactions to tpstestadd on every sender node distributed evenly
    int txPerSender = rawSignedAssetAllocationSends.size() / senders.size();
    printf("Dividing work (%d transactions) between %d senders (%d per sender)...\n", rawSignedAssetAllocationSends.size(), senders.size(), txPerSender);  
    // max 10 tx per call for max buffer size sent to rpc
    if(txPerSender > 10)
        txPerSender = 10; 
    unsigned int j=0;
    unsigned int i=0;
    unsigned int senderIndex=0;
    while(j < rawSignedAssetAllocationSends.size()){
        string vecTX = "[";
        unsigned int currentTxIndex = i * txPerSender;
        unsigned int nextTxIndex = (i+1) * txPerSender;
        if((nextTxIndex+txPerSender) > rawSignedAssetAllocationSends.size())
            nextTxIndex += txPerSender;
        for(j=currentTxIndex;j< nextTxIndex;j++){
            if(j >= rawSignedAssetAllocationSends.size())
                break;
            if(vecTX != "[")
                vecTX += ",";
            vecTX += "{\"tx\":\"" + rawSignedAssetAllocationSends[j] + "\"}";
        }
        if(vecTX != "["){
            vecTX += "]";
            BOOST_CHECK_NO_THROW(CallExtRPC(senders[senderIndex++], "tpstestadd", "0," + vecTX));  
        } 
        if(senderIndex >= senders.size())
            senderIndex = 0;
        i++;
    }
    
    // PHASE 8:  CALL tpstestadd ON ALL SENDER/RECEIVER NODES WITH A FUTURE TIME
	// set the start time to 1 second from now (this needs to be profiled, if the tpstestadd setting time to every node exceeds say 500ms then this time should be extended to account for the latency).
	// rule of thumb if sender count is high (> 25) then profile how long it takes and multiple by 10 and get ceiling of next second needed to send this rpc to every node to have them sync up

	// this will set a start time to every node which will send the vector of signed txs to the network
	int64_t tpstarttime = GetTimeMicros();
	int microsInSecond = 1000 * 1000;
	tpstarttime = tpstarttime + 1 * microsInSecond;
	printf("Adding assetsend transactions to queue on sender nodes...\n");
	for (auto &sender : senders){
		BOOST_CHECK_NO_THROW(CallExtRPC(sender, "tpstestadd",  boost::lexical_cast<string>(tpstarttime)));
        BOOST_CHECK_NO_THROW(r = CallExtRPC(sender, "tpstestinfo"));
        BOOST_CHECK_EQUAL(find_value(r.get_obj(), "testinitiatetime").get_int64(), tpstarttime);
    }
	for (auto &receiver : receivers){
		BOOST_CHECK_NO_THROW(CallExtRPC(receiver, "tpstestadd", boost::lexical_cast<string>(tpstarttime)));
        BOOST_CHECK_EQUAL(find_value(r.get_obj(), "testinitiatetime").get_int64(), tpstarttime);
    }

	// PHASE 9:  WAIT 10 SECONDS + DELAY SET ABOVE (1 SECOND)
	printf("Waiting 11 seconds as per protocol...\n");
	// start 11 second wait
	MilliSleep(11000);
    
    // PHASE 10:  CALL tpstestinfo ON SENDERS AND GET AVERAGE START TIME (TIME SENDERS PUSHED TRANSACTIONS TO THE SOCKETS)
	// get the elapsed time of each node on how long it took to push the vector of signed txs to the network
	int64_t avgteststarttime = 0;
	for (auto &sender : senders) {
		BOOST_CHECK_NO_THROW(r = CallExtRPC(sender, "tpstestinfo"));
		avgteststarttime += find_value(r.get_obj(), "teststarttime").get_int64();
	}
	avgteststarttime /= senders.size();
    
    // PHASE 11:  CALL tpstestinfo ON RECEIVERS AND GET AVERAGE RECEIVE TIME, CALCULATE AVERAGE
	// gather received transfers on the receiver, you can query any receiver node here, in general they all should see the same state after the elapsed time.
	BOOST_CHECK_NO_THROW(r = CallExtRPC(receivers[0], "tpstestinfo"));
	UniValue tpsresponse = r.get_obj();
	UniValue tpsresponsereceivers = find_value(tpsresponse, "receivers").get_array();

	float totalTime = 0;
	for (size_t i = 0; i < tpsresponsereceivers.size(); i++) {
		const UniValue &responseObj = tpsresponsereceivers[i].get_obj();
		totalTime += find_value(responseObj, "time").get_int64() - avgteststarttime;
	}
	// average the start time - received time by the number of responses received (usually number of responses should match number of transactions sent beginning of test)
	totalTime /= tpsresponsereceivers.size();
    
    
    // PHASE 12:  DISPLAY RESULTS
    
	printf("tpstarttime %lld avgteststarttime %lld totaltime %.2f, num responses %zu\n", tpstarttime, avgteststarttime, totalTime, tpsresponsereceivers.size());
	for (auto &sender : senders)
		BOOST_CHECK_NO_THROW(CallExtRPC(sender, "tpstestsetenabled", "false"));
	for (auto &receiver : receivers)
		BOOST_CHECK_NO_THROW(CallExtRPC(receiver, "tpstestsetenabled", "false"));
    int64_t end = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    GenerateBlocks(2);
    printf("elapsed time in seconds: %lld\n", end-start);
}
BOOST_AUTO_TEST_CASE(generate_burn_syscoin)
{
    printf("Running generate_burn_syscoin...\n");
    UniValue r;
    string newaddress = GetNewFundedAddress("node1");
    BOOST_CHECK_NO_THROW(r = CallRPC("node1", "syscoinburn 9.9 true"));
    UniValue varray = r.get_array();
    BOOST_CHECK_NO_THROW(r = CallRPC("node1", "syscointxfund " + varray[0].get_str() + " " + newaddress));
    varray = r.get_array();
    BOOST_CHECK_NO_THROW(r = CallRPC("node1", "signrawtransactionwithwallet " + varray[0].get_str()));
    string hexStr = find_value(r.get_obj(), "hex").get_str();
    BOOST_CHECK_NO_THROW(r = CallRPC("node1", "sendrawtransaction " + hexStr, true, false));
    GenerateBlocks(5, "node1"); 
    CMutableTransaction txIn;
    BOOST_CHECK(DecodeHexTx(txIn, hexStr, true, true));
    CTransaction tx(txIn);
    BOOST_CHECK(tx.vout[0].scriptPubKey.IsUnspendable());   
    BOOST_CHECK_NO_THROW(r = CallRPC("node1", "syscoinburn 0.1 true"));
    varray = r.get_array();
    BOOST_CHECK_THROW(r = CallRPC("node1", "syscointxfund " + varray[0].get_str() + " " + newaddress), runtime_error);
}
BOOST_AUTO_TEST_CASE(generate_burn_syscoin_asset)
{
    UniValue r;
    printf("Running generate_burn_syscoin_asset...\n");
    GenerateBlocks(5);
    GenerateBlocks(5, "node2");
    GenerateBlocks(5, "node3");
    
    string creatoraddress = GetNewFundedAddress("node1");
    string useraddress = GetNewFundedAddress("node1");
    
    string assetguid = AssetNew("node1", creatoraddress, "pubdata", "0xc47bD54a3Df2273426829a7928C3526BF8F7Acaa");
    
    AssetSend("node1", assetguid, "\"[{\\\"ownerto\\\":\\\"" + useraddress + "\\\",\\\"amount\\\":0.5}]\"");
    // try to burn more than we own
    BOOST_CHECK_NO_THROW(r = CallRPC("node1", "assetallocationburn " + assetguid + " " + useraddress + " 0.6"));
    UniValue arr = r.get_array();
    BOOST_CHECK_THROW(r = CallRPC("node1", "syscointxfund " + arr[0].get_str() + " " + useraddress), runtime_error);
    // this one is ok
    BurnAssetAllocation("node1", assetguid, useraddress, "0.5");

    BOOST_CHECK_NO_THROW(r = CallRPC("node1", "assetallocationinfo " + assetguid + " " + useraddress));
    UniValue balance2 = find_value(r.get_obj(), "balance");
    BOOST_CHECK_EQUAL(balance2.getValStr(), "0.00000000");
    // make sure you can't move coins from burn recipient
    BOOST_CHECK_NO_THROW(r = CallRPC("node1", "assetallocationsend " + assetguid + " burn " + "\"[{\\\"ownerto\\\":\\\"" + useraddress + "\\\",\\\"amount\\\":0.5}]\"" + " ''"));
    arr = r.get_array();
    BOOST_CHECK_THROW(r = CallRPC("node1", "syscointxfund " + arr[0].get_str() + " burn"), runtime_error);


}
BOOST_AUTO_TEST_CASE(generate_burn_syscoin_asset_multiple)
{
    UniValue r;
    printf("Running generate_burn_syscoin_asset_multiple...\n");
    GenerateBlocks(5);
    GenerateBlocks(5, "node2");
    GenerateBlocks(5, "node3");
    
    string creatoraddress = GetNewFundedAddress("node1");
    string useraddress = GetNewFundedAddress("node1");
    CallExtRPC("node2", "sendtoaddress" , "\"" + useraddress + "\",\"1\"", false);
    CallExtRPC("node2", "sendtoaddress" , "\"" + useraddress + "\",\"1\"", false);
    GenerateBlocks(5, "node1");
    GenerateBlocks(5, "node2");
       
    string assetguid = AssetNew("node1", creatoraddress, "pubdata", "0xc47bD54a3Df2273426829a7928C3526BF8F7Acaa");
    
    AssetSend("node1", assetguid, "\"[{\\\"ownerto\\\":\\\"" + useraddress + "\\\",\\\"amount\\\":1.0}]\"");
 
    // 2 options for burns, all good, 1 good 1 bad
    // all good, burn 0.4 + 0.5 + 0.05

    BurnAssetAllocation("node1", assetguid, useraddress, "0.4", false);

    BurnAssetAllocation("node1", assetguid, useraddress, "0.5", false);

    BurnAssetAllocation("node1", assetguid, useraddress, "0.05", false);
    GenerateBlocks(5, "node1");
     
    BOOST_CHECK_NO_THROW(r = CallRPC("node1", "assetallocationinfo " + assetguid + " " + useraddress ));
    UniValue balance2 = find_value(r.get_obj(), "balance");
    BOOST_CHECK_EQUAL(balance2.getValStr(), "0.05000000");
    
    
    assetguid = AssetNew("node1", creatoraddress, "pubdata", "0xc47bD54a3Df2273426829a7928C3526BF8F7Acaa");
   
    AssetSend("node1", assetguid, "\"[{\\\"ownerto\\\":\\\"" + useraddress + "\\\",\\\"amount\\\":1.0}]\"");  
    // 1 bad 1 good, burn 0.6+0.6 only 1 should go through

    BurnAssetAllocation("node1", assetguid, useraddress, "0.6", false);

    BurnAssetAllocation("node1", assetguid, useraddress, "0.6", false);
    
    // this will stop the chain if both burns were allowed in the chain, the miner must throw away one of the burns to avoid his block from being flagged as invalid
    GenerateBlocks(5, "node1");
     
    BOOST_CHECK_NO_THROW(r = CallRPC("node1", "assetallocationinfo " + assetguid + " " + useraddress ));
    balance2 = find_value(r.get_obj(), "balance");
    BOOST_CHECK_EQUAL(balance2.getValStr(), "0.40000000"); 


}
// a = 1, a->b(0.4), a->c(0.2), burn a(0.4) (a=0, b=0.4, c=0.2 and burn=0.4)
BOOST_AUTO_TEST_CASE(generate_burn_syscoin_asset_zdag)
{
    UniValue r;
    printf("Running generate_burn_syscoin_asset_zdag...\n");
    GenerateBlocks(5);
    GenerateBlocks(5, "node2");
    GenerateBlocks(5, "node3");

    string creatoraddress = GetNewFundedAddress("node1");

    string useraddress2 = GetNewFundedAddress("node1");
    string useraddress3 = GetNewFundedAddress("node1");
    string useraddress1 = GetNewFundedAddress("node1");
    CallExtRPC("node2", "sendtoaddress" , "\"" + useraddress1 + "\",\"1\"", false);
    CallExtRPC("node2", "sendtoaddress" , "\"" + useraddress1 + "\",\"1\"", false);


    GenerateBlocks(5, "node1");
    GenerateBlocks(5, "node2");

    string assetguid = AssetNew("node1", creatoraddress, "pubdata", "0xc47bD54a3Df2273426829a7928C3526BF8F7Acaa");

    AssetSend("node1", assetguid, "\"[{\\\"ownerto\\\":\\\"" + useraddress1 + "\\\",\\\"amount\\\":1.0}]\"");

    AssetAllocationTransfer(true, "node1", assetguid, useraddress1, "\"[{\\\"ownerto\\\":\\\"" + useraddress2 + "\\\",\\\"amount\\\":0.4}]\"");
    MilliSleep(1000);

    AssetAllocationTransfer(true, "node1", assetguid, useraddress1, "\"[{\\\"ownerto\\\":\\\"" + useraddress3 + "\\\",\\\"amount\\\":0.2}]\"");

    BurnAssetAllocation("node1", assetguid, useraddress1, "0.4", false);

    GenerateBlocks(5, "node1");

    BOOST_CHECK_NO_THROW(r = CallRPC("node1", "assetallocationinfo " + assetguid + " " + useraddress1));
    UniValue balance = find_value(r.get_obj(), "balance");
    BOOST_CHECK_EQUAL(balance.getValStr(), "0.00000000");

    BOOST_CHECK_NO_THROW(r = CallRPC("node1", "assetallocationinfo " + assetguid + " " + useraddress2));
    balance = find_value(r.get_obj(), "balance");
    BOOST_CHECK_EQUAL(balance.getValStr(), "0.40000000");

    BOOST_CHECK_NO_THROW(r = CallRPC("node1", "assetallocationinfo " + assetguid + " " + useraddress3));
    balance = find_value(r.get_obj(), "balance");
    BOOST_CHECK_EQUAL(balance.getValStr(), "0.20000000");

    BOOST_CHECK_NO_THROW(r = CallRPC("node1", "assetallocationinfo " + assetguid + " burn"));
    balance = find_value(r.get_obj(), "balance");
    BOOST_CHECK_EQUAL(balance.getValStr(), "0.40000000");
}
// a = 1, burn a(0.8) a->b (0.4), a->c(0.2) (a=0.4, b=0.4, c=0.2 and burn=0)
BOOST_AUTO_TEST_CASE(generate_burn_syscoin_asset_zdag1)
{
    UniValue r;
    printf("Running generate_burn_syscoin_asset_zdag1...\n");
    GenerateBlocks(5);
    GenerateBlocks(5, "node2");
    GenerateBlocks(5, "node3");

    string creatoraddress = GetNewFundedAddress("node1");
    
    string useraddress2 = GetNewFundedAddress("node1");
    string useraddress3 = GetNewFundedAddress("node1");
    string useraddress1 = GetNewFundedAddress("node1");
    CallExtRPC("node2", "sendtoaddress" , "\"" + useraddress1 + "\",\"1\"", false);
    CallExtRPC("node2", "sendtoaddress" , "\"" + useraddress1 + "\",\"1\"", false);


    GenerateBlocks(5, "node1");
    GenerateBlocks(5, "node2");

    string assetguid = AssetNew("node1", creatoraddress, "pubdata", "0xc47bD54a3Df2273426829a7928C3526BF8F7Acaa");
    
    AssetSend("node1", assetguid, "\"[{\\\"ownerto\\\":\\\"" + useraddress1 + "\\\",\\\"amount\\\":1.0}]\"");

    BurnAssetAllocation("node1", assetguid, useraddress1, "0.8", false);

    AssetAllocationTransfer(true, "node1", assetguid, useraddress1, "\"[{\\\"ownerto\\\":\\\"" + useraddress2 + "\\\",\\\"amount\\\":0.4}]\"");
    MilliSleep(1000);

    AssetAllocationTransfer(true, "node1", assetguid, useraddress1, "\"[{\\\"ownerto\\\":\\\"" + useraddress3 + "\\\",\\\"amount\\\":0.2}]\"");

    GenerateBlocks(5, "node1");

    BOOST_CHECK_NO_THROW(r = CallRPC("node1", "assetallocationinfo " + assetguid + " " + useraddress1));
    UniValue balance = find_value(r.get_obj(), "balance");
    BOOST_CHECK_EQUAL(balance.getValStr(), "0.40000000");

    BOOST_CHECK_NO_THROW(r = CallRPC("node1", "assetallocationinfo " + assetguid + " " + useraddress2));
    balance = find_value(r.get_obj(), "balance");
    BOOST_CHECK_EQUAL(balance.getValStr(), "0.40000000");

    BOOST_CHECK_NO_THROW(r = CallRPC("node1", "assetallocationinfo " + assetguid + " " + useraddress3));
    balance = find_value(r.get_obj(), "balance");
    BOOST_CHECK_EQUAL(balance.getValStr(), "0.20000000");

    // no burn found        
    BOOST_CHECK_THROW(r = CallRPC("node1", "assetallocationinfo " + assetguid + " burn"), runtime_error);
}
// a = 1, b = 1, c = 1, a->b (0.2), b->a(0.2),  a->c(0.2), c->a(0.2), burn a(0.5), burn a(0.5), burn a(0.2), a->c(0.2), burn c(0.2) (a=0.1, b=1, c=1 and burn=0.9)
// what happens during POW (burn happens at the end) a->b (0.2), b->a(0.2),  a->c(0.2), c->a(0.2), a->c(0.2), burn a(0.5), burn a(0.5) (this one won't go through), burn a(0.2) (now I should have 0.1 in a), burn c(0.2)
BOOST_AUTO_TEST_CASE(generate_burn_syscoin_asset_zdag2)
{
    UniValue r;
    printf("Running generate_burn_syscoin_asset_zdag2...\n");
    GenerateBlocks(5);
    GenerateBlocks(5, "node2");
    GenerateBlocks(5, "node3");

    string creatoraddress = GetNewFundedAddress("node1");
    string useraddress1 = GetNewFundedAddress("node1");
    string useraddress2 = GetNewFundedAddress("node1");
    string useraddress3 = GetNewFundedAddress("node1");
    CallExtRPC("node2", "sendtoaddress" , "\"" + useraddress2 + "\",\"1\"", false);
    CallExtRPC("node2", "sendtoaddress" , "\"" + useraddress1 + "\",\"1\"", false);
    CallExtRPC("node2", "sendtoaddress" , "\"" + useraddress1 + "\",\"1\"", false);
    CallExtRPC("node2", "sendtoaddress" , "\"" + useraddress1 + "\",\"1\"", false);
    CallExtRPC("node2", "sendtoaddress" , "\"" + useraddress1 + "\",\"1\"", false);
    CallExtRPC("node2", "sendtoaddress" , "\"" + useraddress1 + "\",\"1\"", false); 
    CallExtRPC("node2", "sendtoaddress" , "\"" + useraddress3 + "\",\"1\"", false);
    GenerateBlocks(5, "node1");
    GenerateBlocks(5, "node2");
    GenerateBlocks(5, "node3");

    string assetguid = AssetNew("node1", creatoraddress, "pubdata", "0xc47bD54a3Df2273426829a7928C3526BF8F7Acaa", "8", "3");

    AssetSend("node1", assetguid, "\"[{\\\"ownerto\\\":\\\"" + useraddress1 + "\\\",\\\"amount\\\":1.0},{\\\"ownerto\\\":\\\"" + useraddress2 + "\\\",\\\"amount\\\":1.0},{\\\"ownerto\\\":\\\"" + useraddress3 + "\\\",\\\"amount\\\":1.0}]\""); 

    AssetAllocationTransfer(true, "node1", assetguid, useraddress1, "\"[{\\\"ownerto\\\":\\\"" + useraddress2 + "\\\",\\\"amount\\\":0.2}]\"");
    MilliSleep(1000);

    AssetAllocationTransfer(true, "node1", assetguid, useraddress2, "\"[{\\\"ownerto\\\":\\\"" + useraddress1 + "\\\",\\\"amount\\\":0.2}]\"");
    MilliSleep(1000);

    AssetAllocationTransfer(true, "node1", assetguid, useraddress1, "\"[{\\\"ownerto\\\":\\\"" + useraddress3 + "\\\",\\\"amount\\\":0.2}]\"");
    MilliSleep(1000);

    AssetAllocationTransfer(true, "node1", assetguid, useraddress3, "\"[{\\\"ownerto\\\":\\\"" + useraddress1 + "\\\",\\\"amount\\\":0.2}]\"");
    MilliSleep(1000);

    BurnAssetAllocation("node1", assetguid, useraddress1, "0.5", false);

    // this one should be thrown away for not enough balance on POW
    BurnAssetAllocation("node1", assetguid, useraddress1, "0.5", false);

    BurnAssetAllocation("node1", assetguid, useraddress1, "0.2", false);

    AssetAllocationTransfer(true, "node1", assetguid, useraddress1, "\"[{\\\"ownerto\\\":\\\"" + useraddress3 + "\\\",\\\"amount\\\":0.2}]\"");

    BurnAssetAllocation("node1", assetguid, useraddress3, "0.2", false);

    GenerateBlocks(5, "node1");

    BOOST_CHECK_NO_THROW(r = CallRPC("node1", "assetallocationinfo " + assetguid + " " + useraddress1));
    UniValue balance = find_value(r.get_obj(), "balance");
    BOOST_CHECK_EQUAL(balance.getValStr(), "0.10000000");

    BOOST_CHECK_NO_THROW(r = CallRPC("node1", "assetallocationinfo " + assetguid + " " + useraddress2));
    balance = find_value(r.get_obj(), "balance");
    BOOST_CHECK_EQUAL(balance.getValStr(), "1.00000000");

    BOOST_CHECK_NO_THROW(r = CallRPC("node1", "assetallocationinfo " + assetguid + " " + useraddress3));
    balance = find_value(r.get_obj(), "balance");
    BOOST_CHECK_EQUAL(balance.getValStr(), "1.00000000");

    BOOST_CHECK_NO_THROW(r = CallRPC("node1", "assetallocationinfo " + assetguid + " burn"));
    balance = find_value(r.get_obj(), "balance");
    BOOST_CHECK_EQUAL(balance.getValStr(), "0.90000000");

}
// a = 1, a->b(0.1), burn a(0.8), a->b (0.4), b->c(0.1), (a=0.5, b=0.4, c=0.1 and no burn) (check a, b are flagged as sender because burn on a + zdag with a and child is b associated with a)
BOOST_AUTO_TEST_CASE(generate_burn_syscoin_asset_zdag3)
{
    UniValue r;
    printf("Running generate_burn_syscoin_asset_zdag3...\n");
    GenerateBlocks(5);
    GenerateBlocks(5, "node2");
    GenerateBlocks(5, "node3");
    
    string creatoraddress = GetNewFundedAddress("node1");
    string useraddress1 = GetNewFundedAddress("node1");
    string useraddress2 = GetNewFundedAddress("node1");
    string useraddress3 = GetNewFundedAddress("node1");
    CallExtRPC("node1", "sendtoaddress" , "\"" + useraddress1 + "\",\"1\"", false);
    CallExtRPC("node1", "sendtoaddress" , "\"" + useraddress1 + "\",\"1\"", false);
    GenerateBlocks(5, "node1");
       
    string assetguid = AssetNew("node1", creatoraddress, "pubdata", "0xc47bD54a3Df2273426829a7928C3526BF8F7Acaa");
    
    AssetSend("node1", assetguid, "\"[{\\\"ownerto\\\":\\\"" + useraddress1 + "\\\",\\\"amount\\\":1.0}]\"");
    AssetAllocationTransfer(false, "node1", assetguid, useraddress1, "\"[{\\\"ownerto\\\":\\\"" + useraddress2 + "\\\",\\\"amount\\\":0.1}]\"");
    
    BurnAssetAllocation("node1", assetguid, useraddress1, "0.8", false);
    
    AssetAllocationTransfer(true, "node1", assetguid, useraddress1, "\"[{\\\"ownerto\\\":\\\"" + useraddress2 + "\\\",\\\"amount\\\":0.4}]\"");
    // wait for 1 second as required by unit test
    MilliSleep(1000);
    AssetAllocationTransfer(true, "node1", assetguid, useraddress2, "\"[{\\\"ownerto\\\":\\\"" + useraddress3 + "\\\",\\\"amount\\\":0.1}]\"");
    // wait for 1.5 second to clear minor warning status
    MilliSleep(1500);
    // check just sender, burn marks as major issue on zdag
    BOOST_CHECK_NO_THROW(r = CallRPC("node1", "assetallocationsenderstatus " + assetguid + " " + useraddress1 + " ''"));
    BOOST_CHECK_EQUAL(find_value(r.get_obj(), "status").get_int(), ZDAG_MAJOR_CONFLICT);
    // should affect downstream too
    BOOST_CHECK_NO_THROW(r = CallRPC("node1", "assetallocationsenderstatus " + assetguid + " " + useraddress2 + " ''"));
    BOOST_CHECK_EQUAL(find_value(r.get_obj(), "status").get_int(), ZDAG_MAJOR_CONFLICT);    
    
    GenerateBlocks(5, "node1");
     
    BOOST_CHECK_NO_THROW(r = CallRPC("node1", "assetallocationinfo " + assetguid + " " + useraddress1));
    UniValue balance = find_value(r.get_obj(), "balance");
    BOOST_CHECK_EQUAL(balance.getValStr(), "0.50000000"); 
    
    BOOST_CHECK_NO_THROW(r = CallRPC("node1", "assetallocationinfo " + assetguid + " " + useraddress2));
    balance = find_value(r.get_obj(), "balance");
    BOOST_CHECK_EQUAL(balance.getValStr(), "0.40000000"); 
    
    BOOST_CHECK_NO_THROW(r = CallRPC("node1", "assetallocationinfo " + assetguid + " " + useraddress3));
    balance = find_value(r.get_obj(), "balance");
    BOOST_CHECK_EQUAL(balance.getValStr(), "0.10000000");     
            
    // no burn found        
    BOOST_CHECK_THROW(r = CallRPC("node1", "assetallocationinfo " + assetguid + " burn"), runtime_error);
    
    // no zdag tx found after block
    BOOST_CHECK_NO_THROW(r = CallRPC("node1", "assetallocationsenderstatus " + assetguid + " " + useraddress1 + " ''"));
    BOOST_CHECK_EQUAL(find_value(r.get_obj(), "status").get_int(), ZDAG_NOT_FOUND);   
    BOOST_CHECK_NO_THROW(r = CallRPC("node1", "assetallocationsenderstatus " + assetguid + " " + useraddress2 + " ''"));
    BOOST_CHECK_EQUAL(find_value(r.get_obj(), "status").get_int(), ZDAG_NOT_FOUND);    

    // now do more zdag and check status are ok this time
    AssetAllocationTransfer(true, "node1", assetguid, useraddress1, "\"[{\\\"ownerto\\\":\\\"" + useraddress2 + "\\\",\\\"amount\\\":0.4}]\"");
    MilliSleep(1000);
    AssetAllocationTransfer(true, "node1", assetguid, useraddress2, "\"[{\\\"ownerto\\\":\\\"" + useraddress3 + "\\\",\\\"amount\\\":0.1}]\"");

    BOOST_CHECK_NO_THROW(r = CallRPC("node1", "assetallocationsenderstatus " + assetguid + " " + useraddress2 + " ''"));
    BOOST_CHECK_EQUAL(find_value(r.get_obj(), "status").get_int(), ZDAG_MINOR_CONFLICT);

    MilliSleep(1000);
    BOOST_CHECK_NO_THROW(r = CallRPC("node1", "assetallocationsenderstatus " + assetguid + " " + useraddress1 + " ''"));
    BOOST_CHECK_EQUAL(find_value(r.get_obj(), "status").get_int(), ZDAG_STATUS_OK);
    
    BOOST_CHECK_NO_THROW(r = CallRPC("node1", "assetallocationsenderstatus " + assetguid + " " + useraddress2 + " ''"));
    BOOST_CHECK_EQUAL(find_value(r.get_obj(), "status").get_int(), ZDAG_STATUS_OK);
}
BOOST_AUTO_TEST_CASE(generate_bad_assetmaxsupply_address)
{
    UniValue r;
	GenerateBlocks(5);
	printf("Running generate_bad_assetmaxsupply_address...\n");
	GenerateBlocks(5);
	string newaddress = GetNewFundedAddress("node1");
	// 256 bytes long
	string gooddata = "SfsddfdfsdsfSfsdfdfsdsfDsdsdsdsfsfsdsfsdsfdsfsdsfdsfsdsfsdSfsdfdfsdsfSfsdfdfsdsfDsdsdsdsfsfsdsfsdsfdsfsdsfdsfsdsfsdSfsdfdfsdsfSfsdfdfsdsfDsdsdsdsfsfsdsfsdsfdsfsdsfdsfsdsfsdSfsdfdfsdsfSfsdfdfsdsfDsdsdsdsfsfsdsfsdsfdsfsdsfdsfsdsfsdSfsdfdfsdsfSfsdfdfsdsDfdfdd";
	// 0 max supply bad
	BOOST_CHECK_THROW(CallRPC("node1", "assetnew " + newaddress + " " + gooddata + " '' 8 1 0 31 ''"), runtime_error);
	// 1 max supply good
	BOOST_CHECK_NO_THROW(CallRPC("node1", "assetnew " + newaddress + " " + gooddata + " '' 8 1 1 31 ''"));
	// balance > max supply
	BOOST_CHECK_NO_THROW(r = CallRPC("node1", "assetnew " + newaddress + " " + gooddata + " '' 3 2000 1000 31 ''"));
	UniValue arr = r.get_array();
	BOOST_CHECK_THROW(r = CallRPC("node1", "syscointxfund " + arr[0].get_str() + " " + newaddress), runtime_error);
}

BOOST_AUTO_TEST_CASE(generate_assetupdate_address)
{
	printf("Running generate_assetupdate_address...\n");
	string newaddress = GetNewFundedAddress("node1");
	string guid = AssetNew("node1", newaddress, "data");
	// update an asset that isn't yours
	UniValue r;
	//"assetupdate [asset] [public] [supply] [witness]\n"
	BOOST_CHECK_NO_THROW(r = CallRPC("node2", "assetupdate " + guid + " " + newaddress + " '' 1 31 ''"));
	UniValue arr = r.get_array();
    BOOST_CHECK_NO_THROW(r = CallRPC("node2", "syscointxfund " + arr[0].get_str() + " " + newaddress));
    arr = r.get_array();
	BOOST_CHECK_NO_THROW(r = CallRPC("node2", "signrawtransactionwithwallet " + arr[0].get_str()));
    string hex_str = find_value(r.get_obj(), "hex").get_str();
    BOOST_CHECK_THROW(r = CallRPC("node2", "sendrawtransaction " + hex_str), runtime_error);

	AssetUpdate("node1", guid, "pub1");
	// shouldnt update data, just uses prev data because it hasnt changed
	AssetUpdate("node1", guid);
	// update supply, ensure balance gets updated properly, 5+1, 1 comes from the initial assetnew, 1 above doesn't actually get set because asset wasn't yours so total should be 6
	AssetUpdate("node1", guid, "pub12", "5");
	BOOST_CHECK_NO_THROW(r = CallRPC("node1", "assetinfo " + guid));
	UniValue balance = find_value(r.get_obj(), "balance");
	BOOST_CHECK_EQUAL(AssetAmountFromValue(balance, 8), 6 * COIN);
	// update supply
    int updateflags = 31 & ~ASSET_UPDATE_SUPPLY;
	string guid1 = AssetNew("node1", newaddress, "data", "''", "8", "1", "10", "31");
    // can't change supply > max supply (current balance already 6, max is 10)
	BOOST_CHECK_NO_THROW(r = CallRPC("node1", "assetupdate " + guid + " " + newaddress + " '' 5 " + boost::lexical_cast<string>(updateflags) + " ''"));
	arr = r.get_array();
	BOOST_CHECK_THROW(r = CallRPC("node1", "syscointxfund " + arr[0].get_str() + " " + newaddress), runtime_error);

	AssetUpdate("node1", guid1, "pub12", "1", boost::lexical_cast<string>(updateflags));
	// ensure can't update supply (update flags is set to not allowsupply update)
	BOOST_CHECK_NO_THROW(r = CallRPC("node1", "assetupdate " + guid1 + " " + newaddress + " '' 1 " + boost::lexical_cast<string>(updateflags) + " ''"));
	arr = r.get_array();
	BOOST_CHECK_THROW(r = CallRPC("node1", "syscointxfund " + arr[0].get_str() + " " + newaddress), runtime_error);

}
BOOST_AUTO_TEST_CASE(generate_assetupdate_precision_address)
{
	printf("Running generate_assetupdate_precision_address...\n");
	UniValue r;
	for (int i = 0; i <= 8; i++) {
		string istr = boost::lexical_cast<string>(i);
		string addressName = GetNewFundedAddress("node1");
		// test max supply for every possible precision
		string guid = AssetNew("node1", addressName, "data","''", istr, "1", "-1");
		UniValue negonevalue(UniValue::VSTR);
		negonevalue.setStr("-1");
		CAmount precisionCoin = powf(10, i);
		// get max value - 1 (1 is already the supply, and this value is cumulative)
		CAmount negonesupply = AssetAmountFromValue(negonevalue, i) - precisionCoin;
		string maxstr = ValueFromAssetAmount(negonesupply, i).get_str();
		AssetUpdate("node1", guid, "pub12", maxstr);
		// can't go above max balance (10^18) / (10^i) for i decimal places
		BOOST_CHECK_NO_THROW(r = CallRPC("node1", "assetupdate " + guid + " pub '' 1 31 ''"));
		UniValue arr = r.get_array();
		BOOST_CHECK_THROW(r = CallRPC("node1", "syscointxfund " + arr[0].get_str() + " " + addressName), runtime_error);
		// can't create asset with more than max+1 balance or max+1 supply
		string maxstrplusone = ValueFromAssetAmount(negonesupply + (precisionCoin * 2), i).get_str();
		maxstr = ValueFromAssetAmount(negonesupply + precisionCoin, i).get_str();
		BOOST_CHECK_NO_THROW(CallRPC("node1", "assetnew " + addressName + " pub '' " + istr + " " + maxstr + " -1 31 ''"));
		BOOST_CHECK_NO_THROW(CallRPC("node1", "assetnew " + addressName + " pub '' " + istr + " 1 " + maxstr + " 31 ''"));
		BOOST_CHECK_THROW(CallRPC("node1", "assetnew " + addressName + " pub '' " + istr + " " + maxstrplusone + " -1 31 ''"), runtime_error);
		BOOST_CHECK_THROW(CallRPC("node1", "assetnew " + addressName + " pub '' " + istr + " 1 " + maxstrplusone + " 31 ''"), runtime_error);
	}
    string newaddress = GetNewFundedAddress("node1");
	// invalid precisions
	BOOST_CHECK_THROW(CallRPC("node1", "assetnew " + newaddress + " pub '' 9 1 2 31 ''"), runtime_error);
	BOOST_CHECK_THROW(CallRPC("node1", "assetnew " + newaddress + " pub '' -1 1 2 31 ''"), runtime_error);
}
BOOST_AUTO_TEST_CASE(generate_assetsend_address)
{
	UniValue r;
	printf("Running generate_assetsend_address...\n");
	string newaddress = GetNewFundedAddress("node1");
	string newaddress1 = GetNewFundedAddress("node1");
	string guid = AssetNew("node1", newaddress, "data", "''", "8", "10", "20");
	// [{\"ownerto\":\"address\",\"amount\":amount},...]
	AssetSend("node1", guid, "\"[{\\\"ownerto\\\":\\\"" + newaddress1 + "\\\",\\\"amount\\\":7}]\"");
	// ensure amounts are correct
	BOOST_CHECK_NO_THROW(r = CallRPC("node1", "assetinfo " + guid));
	UniValue balance = find_value(r.get_obj(), "balance");
	UniValue totalsupply = find_value(r.get_obj(), "total_supply");
	UniValue maxsupply = find_value(r.get_obj(), "max_supply");
	BOOST_CHECK_EQUAL(AssetAmountFromValue(balance, 8), 3 * COIN);
	BOOST_CHECK_EQUAL(AssetAmountFromValue(totalsupply, 8), 10 * COIN);
	BOOST_CHECK_EQUAL(AssetAmountFromValue(maxsupply, 8), 20 * COIN);
	
	// ensure receiver get's it
	BOOST_CHECK_NO_THROW(r = CallRPC("node1", "assetallocationinfo " + guid + " " + newaddress1 ));
	
	balance = find_value(r.get_obj(), "balance");
	BOOST_CHECK_EQUAL(AssetAmountFromValue(balance, 8), 7 * COIN);

	// add balances
	AssetUpdate("node1", guid, "pub12", "1");
	// check balance is added to end
	BOOST_CHECK_NO_THROW(r = CallRPC("node1", "assetinfo " + guid));
	balance = find_value(r.get_obj(), "balance");
	totalsupply = find_value(r.get_obj(), "total_supply");
	maxsupply = find_value(r.get_obj(), "max_supply");
	BOOST_CHECK_EQUAL(AssetAmountFromValue(balance, 8), 4 * COIN);
	BOOST_CHECK_EQUAL(AssetAmountFromValue(totalsupply, 8), 11 * COIN);
	BOOST_CHECK_EQUAL(AssetAmountFromValue(maxsupply, 8), 20 * COIN);
	
	AssetUpdate("node1", guid, "pub12", "9");
	// check balance is added to end
	BOOST_CHECK_NO_THROW(r = CallRPC("node1", "assetinfo " + guid ));
	balance = find_value(r.get_obj(), "balance");
	totalsupply = find_value(r.get_obj(), "total_supply");
	maxsupply = find_value(r.get_obj(), "max_supply");
	BOOST_CHECK_EQUAL(AssetAmountFromValue(balance, 8), 13 * COIN);
	BOOST_CHECK_EQUAL(AssetAmountFromValue(totalsupply, 8), 20 * COIN);
	BOOST_CHECK_EQUAL(AssetAmountFromValue(maxsupply, 8), 20 * COIN);
	
	// can't go over 20 supply
	BOOST_CHECK_NO_THROW(r = CallRPC("node1", "assetupdate " + guid + " " + newaddress + " '' 1 31 ''"));
	UniValue arr = r.get_array();
	BOOST_CHECK_THROW(r = CallRPC("node1", "syscointxfund " + arr[0].get_str() + " " + newaddress), runtime_error);
}


BOOST_AUTO_TEST_CASE(generate_assettransfer_address)
{
	printf("Running generate_assettransfer_address...\n");
	GenerateBlocks(5, "node1");
	GenerateBlocks(5, "node2");
	GenerateBlocks(5, "node3");
	string newaddres1 = GetNewFundedAddress("node1");
	string newaddres2 = GetNewFundedAddress("node2");
	string newaddres3 = GetNewFundedAddress("node3");

	string guid1 = AssetNew("node1", newaddres1, "pubdata");
	string guid2 = AssetNew("node1", newaddres1, "pubdata");
	AssetUpdate("node1", guid1, "pub3");
	UniValue r;
	AssetTransfer("node1", "node2", guid1, newaddres2);
	AssetTransfer("node1", "node3", guid2, newaddres3);
    BOOST_CHECK_NO_THROW(r = CallRPC("node1", "assetinfo " + guid1));
    BOOST_CHECK(find_value(r.get_obj(), "owner").get_str() == newaddres2);
	// xfer an asset that isn't yours
	BOOST_CHECK_NO_THROW(r = CallRPC("node1", "assettransfer " + guid1 + " " + newaddres2 + " ''"));
	UniValue arr = r.get_array();
    BOOST_CHECK_THROW(r = CallRPC("node1", "syscointxfund " + arr[0].get_str() + " " + newaddres1), runtime_error);
    
    GenerateBlocks(5, "node1");
    BOOST_CHECK_NO_THROW(r = CallRPC("node1", "assetinfo " + guid1));
    BOOST_CHECK(find_value(r.get_obj(), "owner").get_str() == newaddres2);
    
	// update xferred asset
	AssetUpdate("node2", guid1, "public");

	// retransfer asset
	AssetTransfer("node2", "node3", guid1, newaddres3);
}
BOOST_AUTO_TEST_SUITE_END ()
