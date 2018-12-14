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
#include "services/ranges.h"
#include "core_io.h"
#include <key.h>
using namespace std;
BOOST_GLOBAL_FIXTURE( SyscoinTestingSetup );
void printRangeVector (vector<CRange> &vecRange, string tag) {
	printf("Printing vector range %s: ", tag.c_str());
	for(size_t index = 0; index < vecRange.size(); index++) {
		printf("{%i,%i} ", vecRange[index].start, vecRange[index].end);
	}
	printf("\n");
}
void addToRangeVector (vector<CRange> &vecRange, int range_single) { 
	CRange range(range_single, range_single);
	vecRange.push_back(range);
}
void addToRangeVector (vector<CRange> &vecRange, int range_start, int range_end) { 
	CRange range(range_start, range_end);
	vecRange.push_back(range);
}

BOOST_FIXTURE_TEST_SUITE (syscoin_asset_tests, BasicSyscoinTestingSetup)

BOOST_AUTO_TEST_CASE(generate_range_merge)
{
	printf("Running generate_range_merge...\n");
	// start with {0,0} {2,3} {6,8}, add {4,5} to it and expect {0,0} {2,8}
	CheckRangeMerge("{0,0} {2,3} {6,8}", "{4,5}", "{0,0} {2,8}");

	CheckRangeMerge("{0,0} {2,3} {6,8}", "{4,5}", "{0,0} {2,8}");
	CheckRangeMerge("{2,3} {6,8}", "{0,0} {4,5}", "{0,0} {2,8}");
	CheckRangeMerge("{2,3}", "{0,0} {4,5} {6,8}", "{0,0} {2,8}");
	CheckRangeMerge("{0,0} {4,5} {6,8}", "{2,3}", "{0,0} {2,8}");

	CheckRangeMerge("{0,0} {2,2} {4,4} {6,6} {8,8}", "{1,1} {3,3} {5,5} {7,7} {9,9}", "{0,9}");
	CheckRangeMerge("{0,8}","{9,9}","{0,9}");
	CheckRangeMerge("{0,8}","{10,10}","{0,8} {10,10}");
	CheckRangeMerge("{0,0} {2,2} {4,4} {6,6} {8,8} {10,10} {12,12} {14,14} {16,16} {18,18} {20,20} {22,22} {24,24} {26,26} {28,28} {30,30} {32,32} {34,34} {36,36} {38,38} {40,40} {42,42} {44,44} {46,46} {48,48}", "{1,1} {3,3} {5,5} {7,7} {9,9} {11,11} {13,13} {15,15} {17,17} {19,19} {21,21} {23,23} {25,25} {27,27} {29,29} {31,31} {33,33} {35,35} {37,37} {39,39} {41,41} {43,43} {45,45} {47,47} {49,49}", "{0,49}");  

}
BOOST_AUTO_TEST_CASE(generate_range_subtract)
{
	printf("Running generate_range_subtract...\n");
	// start with {0,9}, subtract {0,0} {2,3} {6,8} from it and expect {1,1} {4,5} {9,9}
	CheckRangeSubtract("{0,9}", "{0,0} {2,3} {6,8}", "{1,1} {4,5} {9,9}");

	CheckRangeSubtract("{1,2} {3,3} {6,10}", "{0,0} {2,2} {3,3}", "{1,1} {6,10}");
	CheckRangeSubtract("{1,2} {3,3} {6,10}", "{0,0} {2,2}", "{1,1} {3,3} {6,10}");
}
BOOST_AUTO_TEST_CASE(generate_range_contain)
{
	printf("Running generate_range_contain...\n");
	// does {0,9} contain {0,0}?
	BOOST_CHECK(DoesRangeContain("{0,9}", "{0,0}"));
	BOOST_CHECK(DoesRangeContain("{0,2}", "{1,2}"));
	BOOST_CHECK(DoesRangeContain("{0,3}", "{2,2}"));
	BOOST_CHECK(DoesRangeContain("{0,0} {2,3} {6,8}", "{2,2}"));
	BOOST_CHECK(DoesRangeContain("{0,0} {2,3} {6,8}", "{2,3}"));
	BOOST_CHECK(DoesRangeContain("{0,0} {2,3} {6,8}", "{6,7}"));
	BOOST_CHECK(DoesRangeContain("{0,8}", "{0,0} {2,3} {6,8}"));
	BOOST_CHECK(DoesRangeContain("{0,8}", "{0,1} {2,4} {6,6}"));

	BOOST_CHECK(!DoesRangeContain("{1,9}", "{0,0}"));
	BOOST_CHECK(!DoesRangeContain("{1,9}", "{1,10}"));
	BOOST_CHECK(!DoesRangeContain("{1,2}", "{1,3}"));
	BOOST_CHECK(!DoesRangeContain("{1,2}", "{0,2}"));
	BOOST_CHECK(!DoesRangeContain("{1,2}", "{0,3}"));
	BOOST_CHECK(!DoesRangeContain("{0,0} {2,3} {6,8}", "{1,2}"));
	BOOST_CHECK(!DoesRangeContain("{0,0} {2,3} {6,8}", "{0,1}"));
	BOOST_CHECK(!DoesRangeContain("{0,0} {2,3} {6,8}", "{4,4}"));
	BOOST_CHECK(!DoesRangeContain("{0,0} {2,3} {6,8}", "{4,5}"));
	BOOST_CHECK(!DoesRangeContain("{0,0} {2,3} {6,8}", "{0,8}"));
	BOOST_CHECK(!DoesRangeContain("{0,8}", "{0,1} {2,4} {6,9}"));
	BOOST_CHECK(!DoesRangeContain("{0,8}", "{0,9} {2,4} {6,8}"));
}
BOOST_AUTO_TEST_CASE(generate_range_complex)
{
	/* Test 1:  Generate two large input, 1 all even number 1 all odd and merge them */
	/* This test uses Range Test Library that contains addition vector operations    */
	printf("Running generate_range_complex...\n");
	string input1="", input2="", expected_output="";
	int total_range = 10000;
	int64_t ms1 = 0, ms2 = 0;

	printf("ExpectedOutput: range from 0-%i\n", total_range);
	printf("Input1: range from 0-%i\n Even number only\n", total_range-2);
	printf("Input2: range from 1-%i\n Odd number only\n", total_range-1);

	for (int index = 0; index < total_range; index=index+2) {
		input1 = input1 + "{" + to_string(index) + "," + to_string(index) +"} ";
		input2 = input2 + "{" + to_string(index+1) + "," + to_string(index+1) +"} ";
	}
	expected_output = "{0," + to_string(total_range-1) + "}"; 
	// Remove the last space from the string
	input1.pop_back();
	input2.pop_back();
	printf("Rangemerge Test: input1 + input2 = ExpectedOutput\n");
	ms1 = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
	CheckRangeMerge(input1, input2, expected_output);
	ms2 = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
	printf("CheckRangeMerge Completed %ldms\n", ms2-ms1);

	/* Test 2: Reverse of Test 1 (expected_output - input = 2) */
 	printf("RangeSubstract Test: ExpectedOutput - input1 = input2\n");	
	ms1 = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
	CheckRangeSubtract(expected_output, input1, input2);
	ms2 = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
	printf("CheckRangeSubtract1 Completed %ldms\n", ms2-ms1);

 	printf("RangeSubstract Test: ExpectedOutput - input2 = input1\n");	
	ms1 = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
	CheckRangeSubtract(expected_output, input2, input1);
	ms2 = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
	printf("CheckRangeSubtract2 Completed %ldms\n", ms2-ms1);
}
BOOST_AUTO_TEST_CASE(generate_range_stress_merge1) 
{
	// Test: merge1 
	// range1: {0-10m} even only
	// range2: {1-9999999} odd only
	// output:  range1 + range2
	printf("Running generate_range_stress_merge1:...\n");
	int total_range = 10000000;
	vector<CRange> vecRange1_i, vecRange2_i, vecRange_o, vecRange_expected;
	int64_t ms1 = 0, ms2 = 0;

	for (int index = 0; index < total_range; index=index+2) {
		addToRangeVector(vecRange1_i, index, index);
		addToRangeVector(vecRange2_i, index+1, index+1);
	}
	
	// Set expected outcome
	addToRangeVector(vecRange_expected, 0, total_range-1);
	
	// combine the two vectors of ranges
	vecRange1_i.insert(std::end(vecRange1_i), std::begin(vecRange2_i), std::end(vecRange2_i));

	ms1 = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
	mergeRanges(vecRange1_i, vecRange_o);
	ms2 = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();

	printf("\noutput range 1+2: merge time: %ldms\n", ms2-ms1);

	BOOST_CHECK(vecRange_o.size() == vecRange_expected.size());
	BOOST_CHECK(vecRange_o.back() == vecRange_expected.back());
	printf("\n Stress Test done \n");

}
BOOST_AUTO_TEST_CASE(generate_range_stress_subtract1) 
{
	// Test: subtract1 
	// range1: {0-1m} 
	// range2: {1m, 4m ,7m}
	// output:  range1 - range2
	printf("Running generate_range_stress_subtract1...\n");
	vector<CRange> vecRange1_i, vecRange2_i, vecRange_o, vecRange_expected;
	vector<CRange> vecRange2_i_copy;
	int64_t ms1 = 0, ms2 = 0;

	
	// Set input range 1 {0,10m}
	addToRangeVector(vecRange1_i, 0, 10000000);

	printRangeVector(vecRange1_i, "vecRange 1 input");
	// Set input range 2 {1m,4m,7m}
	addToRangeVector(vecRange2_i, 1000000);
	addToRangeVector(vecRange2_i, 4000000);
	addToRangeVector(vecRange2_i, 7000000);
	printRangeVector(vecRange2_i, "vecRange 2 input");
	
	// Set expected output {(1-999999),(1000001-3999999)...}
	addToRangeVector(vecRange_expected, 0, 999999);
	addToRangeVector(vecRange_expected, 1000001, 3999999);
	addToRangeVector(vecRange_expected, 4000001, 6999999);
	addToRangeVector(vecRange_expected, 7000001, 10000000);
	printRangeVector(vecRange_expected, "vecRange_expected");
	
	// Deep copy for test #2 since the vector will get modified
	vecRange2_i_copy = vecRange2_i;
	

	ms1 = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
	subtractRanges(vecRange1_i, vecRange2_i, vecRange_o);
	ms2 = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();

	BOOST_CHECK(vecRange_o.size() == vecRange_expected.size());
	BOOST_CHECK(vecRange_o.back() == vecRange_expected.back());

	vecRange2_i = vecRange2_i_copy;
	vector<CRange> vecRange2_o;
	vecRange2_i.insert(std::end(vecRange2_i), std::begin(vecRange_expected), std::end(vecRange_expected));
	ms1 = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
	mergeRanges(vecRange2_i, vecRange2_o);
	ms2 = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
	printf("\noutput range expected+2: merge time: %ld\n", ms2-ms1);

	BOOST_CHECK_EQUAL(vecRange2_o.size(), vecRange1_i.size());
	BOOST_CHECK_EQUAL(vecRange2_o.back().start, 0);
	BOOST_CHECK_EQUAL(vecRange2_o.back().end, 10000000);
}
BOOST_AUTO_TEST_CASE(generate_range_stress_merge2) 
{
	// Test: merge2
	// range1: {0-1m} odd only
	// range2: {100000 200000, ..., 900000 }
	// output:  range1 + range2
	printf("Running generate_range_stress_merge2...\n");
	vector<CRange> vecRange1_i, vecRange2_i, vecRange_o;
	int64_t ms1 = 0, ms2 = 0;
	int total_range = 1000000;

	// Create vector range 1 that's 0-1mill odd only
	for (int index = 0; index < total_range; index=index+2) {
		addToRangeVector(vecRange1_i, index+1, index+1);
	}
	// Create vector range 2 that's 100k,200k,300k...,900k
	for (int index = 100000; index < total_range; index=index+100000) {
		addToRangeVector(vecRange2_i, index, index);
	}

	ms1 = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
	vecRange1_i.insert(std::end(vecRange1_i), std::begin(vecRange2_i), std::end(vecRange2_i));
	mergeRanges(vecRange1_i, vecRange_o);
	ms2 = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();

	//HARDCODED checks
	BOOST_CHECK_EQUAL(vecRange_o.size(), (total_range/2) - 9);
	BOOST_CHECK_EQUAL(vecRange_o[49999].start, 99999); 
	BOOST_CHECK_EQUAL(vecRange_o[49999].end, 100001); 
	BOOST_CHECK_EQUAL(vecRange_o[99998].start, 199999); 
	BOOST_CHECK_EQUAL(vecRange_o[99998].end, 200001); 
	BOOST_CHECK_EQUAL(vecRange_o[449991].start, 899999); 
	BOOST_CHECK_EQUAL(vecRange_o[449991].end, 900001); 
	printf("CheckRangeSubtract Completed %ldms\n", ms2-ms1);
}
BOOST_AUTO_TEST_CASE(generate_range_stress_subtract2) 
{
	// Test: subtract2
	// range1: {0-1m} odd only
	// range2: {100001 200001, ..., 900001 }
	// output:  range1 - range2
	printf("Running generate_range_stress_subtract3...\n");
	vector<CRange> vecRange1_i, vecRange2_i, vecRange_o;
	int64_t ms1 = 0, ms2 = 0;
	int total_range = 1000000;

	// Create vector range 1 that's 0-1mill odd only
	for (int index = 0; index < total_range; index=index+2) {
		addToRangeVector(vecRange1_i, index+1,index+1);
	}
	// Create vector range 2 that's 100k,200k,300k...,900k
	for (int index = 100000; index < total_range; index=index+100000) {
		addToRangeVector(vecRange2_i, index+1,index+1);
	}

	ms1 = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
	subtractRanges(vecRange1_i, vecRange2_i, vecRange_o);
	ms2 = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();

	//HARDCODED checks
	BOOST_CHECK_EQUAL(vecRange_o.size(), 499991);
	printf("CheckRangeSubtract Completed %ldms\n", ms2-ms1);
}

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
	string guid = AssetNew("node1", "chf", newaddress, gooddata);
	BOOST_CHECK_NO_THROW(r = CallRPC("node1", "listassets"));
	UniValue rArray = r.get_array();
	BOOST_CHECK(rArray.size() > 0);
	BOOST_CHECK_EQUAL(find_value(rArray[0].get_obj(), "_id").get_str(), guid);
	string guid1 = AssetNew("node1", "usd", newaddress, gooddata);
	BOOST_CHECK_NO_THROW(r = CallRPC("node1", "assetinfo " + guid + " false"));
	BOOST_CHECK(find_value(r.get_obj(), "_id").get_str() == guid);
	BOOST_CHECK(find_value(r.get_obj(), "symbol").get_str() == "CHF");
	BOOST_CHECK_NO_THROW(r = CallRPC("node1", "assetinfo " + guid1 + " false"));
	BOOST_CHECK(find_value(r.get_obj(), "_id").get_str() == guid1);
	BOOST_CHECK(find_value(r.get_obj(), "symbol").get_str() == "USD");
}
BOOST_AUTO_TEST_CASE(generate_burn_syscoin)
{
	printf("Running generate_burn_syscoin...\n");
	UniValue r;
	string newaddress = GetNewFundedAddress("node1");
	BOOST_CHECK_NO_THROW(r = CallRPC("node1", "syscoinburn " + newaddress + " 9.9 true"));
	UniValue varray = r.get_array();
	BOOST_CHECK_NO_THROW(r = CallRPC("node1", "signrawtransactionwithwallet " + varray[0].get_str()));
	string hexStr = find_value(r.get_obj(), "hex").get_str();
	BOOST_CHECK_NO_THROW(r = CallRPC("node1", "syscoinsendrawtransaction " + hexStr));
	GenerateBlocks(5, "node1");	
	CMutableTransaction txIn;
	if (!DecodeHexTx(txIn, hexStr))
		throw runtime_error("SYSCOIN_ASSET_RPC_ERROR: ERRCODE: 5513 - " + _("Could not send raw transaction: Cannot decode transaction from hex string"));
	CTransaction tx(txIn);
	BOOST_CHECK(tx.vout[0].scriptPubKey.IsUnspendable());	
	BOOST_CHECK_THROW(r = CallRPC("node1", "syscoinburn " + newaddress + " 0.1 true"), runtime_error);
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
	
	string assetguid = AssetNew("node1", "asset1", creatoraddress, "pubdata", "0xc47bD54a3Df2273426829a7928C3526BF8F7Acaa");
	
	AssetSend("node1", assetguid, "\"[{\\\"ownerto\\\":\\\"" + useraddress + "\\\",\\\"amount\\\":0.5}]\"", "memoassetburn");
	// try to burn more than we own
	BOOST_CHECK_THROW(r = CallRPC("node1", "assetallocationburn " + assetguid + " " + useraddress + " 0.6"), runtime_error);
	// this one is ok
	BurnAssetAllocation("node1", assetguid, useraddress, "0.5");

	BOOST_CHECK_NO_THROW(r = CallRPC("node1", "assetallocationinfo " + assetguid + " " + useraddress + " false"));
	UniValue balance2 = find_value(r.get_obj(), "balance");
	BOOST_CHECK_EQUAL(balance2.getValStr(), "0.00000000");
	// make sure you can't move coins from burn recipient
	BOOST_CHECK_THROW(r = CallRPC("node1", "assetallocationsend " + assetguid + " burn " + "\"[{\\\"ownerto\\\":\\\"" + useraddress + "\\\",\\\"amount\\\":0.5}]\"" + " memo ''"), runtime_error);


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
       
    string assetguid = AssetNew("node1", "asset1", creatoraddress, "pubdata", "0xc47bD54a3Df2273426829a7928C3526BF8F7Acaa");
    
    AssetSend("node1", assetguid, "\"[{\\\"ownerto\\\":\\\"" + useraddress + "\\\",\\\"amount\\\":1.0}]\"", "memoassetburn");
 
    // 2 options for burns, all good, 1 good 1 bad
    // all good, burn 0.4 + 0.5 + 0.05

    BurnAssetAllocation("node1", assetguid, useraddress, "0.4", false);

    BurnAssetAllocation("node1", assetguid, useraddress, "0.5", false);

    BurnAssetAllocation("node1", assetguid, useraddress, "0.05", false);
    GenerateBlocks(5, "node1");
     
    BOOST_CHECK_NO_THROW(r = CallRPC("node1", "assetallocationinfo " + assetguid + " " + useraddress + " false"));
    UniValue balance2 = find_value(r.get_obj(), "balance");
    BOOST_CHECK_EQUAL(balance2.getValStr(), "0.05000000");
    
    
    assetguid = AssetNew("node1", "asset1", creatoraddress, "pubdata", "0xc47bD54a3Df2273426829a7928C3526BF8F7Acaa");
   
    AssetSend("node1", assetguid, "\"[{\\\"ownerto\\\":\\\"" + useraddress + "\\\",\\\"amount\\\":1.0}]\"", "memoassetburn");  
    // 1 bad 1 good, burn 0.6+0.6 only 1 should go through

    BurnAssetAllocation("node1", assetguid, useraddress, "0.6", false);

    BurnAssetAllocation("node1", assetguid, useraddress, "0.6", false);
    
    // this will stop the chain if both burns were allowed in the chain, the miner must throw away one of the burns to avoid his block from being flagged as invalid
    GenerateBlocks(5, "node1");
     
    BOOST_CHECK_NO_THROW(r = CallRPC("node1", "assetallocationinfo " + assetguid + " " + useraddress + " false"));
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

	string assetguid = AssetNew("node1", "asset1", creatoraddress, "pubdata", "0xc47bD54a3Df2273426829a7928C3526BF8F7Acaa");

	AssetSend("node1", assetguid, "\"[{\\\"ownerto\\\":\\\"" + useraddress1 + "\\\",\\\"amount\\\":1.0}]\"", "memoassetburn");

	AssetAllocationTransfer(true, "node1", assetguid, useraddress1, "\"[{\\\"ownerto\\\":\\\"" + useraddress2 + "\\\",\\\"amount\\\":0.4}]\"", "zdagburn");
	MilliSleep(1000);

	AssetAllocationTransfer(true, "node1", assetguid, useraddress1, "\"[{\\\"ownerto\\\":\\\"" + useraddress3 + "\\\",\\\"amount\\\":0.2}]\"", "zdagburn");

	BurnAssetAllocation("node1", assetguid, useraddress1, "0.4", false);

	GenerateBlocks(5, "node1");

	BOOST_CHECK_NO_THROW(r = CallRPC("node1", "assetallocationinfo " + assetguid + " " + useraddress1 + " false"));
	UniValue balance = find_value(r.get_obj(), "balance");
	BOOST_CHECK_EQUAL(balance.getValStr(), "0.00000000");

	BOOST_CHECK_NO_THROW(r = CallRPC("node1", "assetallocationinfo " + assetguid + " " + useraddress2 + " false"));
	balance = find_value(r.get_obj(), "balance");
	BOOST_CHECK_EQUAL(balance.getValStr(), "0.40000000");

	BOOST_CHECK_NO_THROW(r = CallRPC("node1", "assetallocationinfo " + assetguid + " " + useraddress3 + " false"));
	balance = find_value(r.get_obj(), "balance");
	BOOST_CHECK_EQUAL(balance.getValStr(), "0.20000000");

	BOOST_CHECK_NO_THROW(r = CallRPC("node1", "assetallocationinfo " + assetguid + " burn false"));
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

	string assetguid = AssetNew("node1", "asset1", creatoraddress, "pubdata", "0xc47bD54a3Df2273426829a7928C3526BF8F7Acaa");
	
	AssetSend("node1", assetguid, "\"[{\\\"ownerto\\\":\\\"" + useraddress1 + "\\\",\\\"amount\\\":1.0}]\"", "memoassetburn");

	BurnAssetAllocation("node1", assetguid, useraddress1, "0.8", false);

	AssetAllocationTransfer(true, "node1", assetguid, useraddress1, "\"[{\\\"ownerto\\\":\\\"" + useraddress2 + "\\\",\\\"amount\\\":0.4}]\"", "zdagburn");
	MilliSleep(1000);

	AssetAllocationTransfer(true, "node1", assetguid, useraddress1, "\"[{\\\"ownerto\\\":\\\"" + useraddress3 + "\\\",\\\"amount\\\":0.2}]\"", "zdagburn");

	GenerateBlocks(5, "node1");

	BOOST_CHECK_NO_THROW(r = CallRPC("node1", "assetallocationinfo " + assetguid + " " + useraddress1 + " false"));
	UniValue balance = find_value(r.get_obj(), "balance");
	BOOST_CHECK_EQUAL(balance.getValStr(), "0.40000000");

	BOOST_CHECK_NO_THROW(r = CallRPC("node1", "assetallocationinfo " + assetguid + " " + useraddress2 + " false"));
	balance = find_value(r.get_obj(), "balance");
	BOOST_CHECK_EQUAL(balance.getValStr(), "0.40000000");

	BOOST_CHECK_NO_THROW(r = CallRPC("node1", "assetallocationinfo " + assetguid + " " + useraddress3 + " false"));
	balance = find_value(r.get_obj(), "balance");
	BOOST_CHECK_EQUAL(balance.getValStr(), "0.20000000");

	// no burn found        
	BOOST_CHECK_THROW(r = CallRPC("node1", "assetallocationinfo " + assetguid + " burn false"), runtime_error);
}
// a = 1, a->b (0.2), b->a(0.2),  a->c(0.2), c->a(0.2), burn a(0.5), burn a(0.5), burn a(0.2), a->c(0.2), burn c(0.2) (a=0.1, b=0, c=0 and burn=0.9)
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

	string assetguid = AssetNew("node1", "asset1", creatoraddress, "pubdata", "0xc47bD54a3Df2273426829a7928C3526BF8F7Acaa");

	AssetSend("node1", assetguid, "\"[{\\\"ownerto\\\":\\\"" + useraddress1 + "\\\",\\\"amount\\\":1.0}]\"", "memoassetburn"); 

	AssetAllocationTransfer(true, "node1", assetguid, useraddress1, "\"[{\\\"ownerto\\\":\\\"" + useraddress2 + "\\\",\\\"amount\\\":0.2}]\"", "zdagburn");
	MilliSleep(1000);

	AssetAllocationTransfer(true, "node1", assetguid, useraddress2, "\"[{\\\"ownerto\\\":\\\"" + useraddress1 + "\\\",\\\"amount\\\":0.2}]\"", "zdagburn");
	MilliSleep(1000);

	AssetAllocationTransfer(true, "node1", assetguid, useraddress1, "\"[{\\\"ownerto\\\":\\\"" + useraddress3 + "\\\",\\\"amount\\\":0.2}]\"", "zdagburn");
	MilliSleep(1000);

	AssetAllocationTransfer(true, "node1", assetguid, useraddress3, "\"[{\\\"ownerto\\\":\\\"" + useraddress1 + "\\\",\\\"amount\\\":0.2}]\"", "zdagburn");
	MilliSleep(1000);

	BurnAssetAllocation("node1", assetguid, useraddress1, "0.5", false);

	// this one should be thrown away for not enough balance on POW
	BurnAssetAllocation("node1", assetguid, useraddress1, "0.5", false);

	BurnAssetAllocation("node1", assetguid, useraddress1, "0.2", false);

	AssetAllocationTransfer(true, "node1", assetguid, useraddress1, "\"[{\\\"ownerto\\\":\\\"" + useraddress3 + "\\\",\\\"amount\\\":0.2}]\"", "zdagburn");

	BurnAssetAllocation("node1", assetguid, useraddress3, "0.2", false);

	GenerateBlocks(5, "node1");

	BOOST_CHECK_NO_THROW(r = CallRPC("node1", "assetallocationinfo " + assetguid + " " + useraddress1 + " false"));
	UniValue balance = find_value(r.get_obj(), "balance");
	BOOST_CHECK_EQUAL(balance.getValStr(), "0.10000000");

	BOOST_CHECK_NO_THROW(r = CallRPC("node1", "assetallocationinfo " + assetguid + " " + useraddress2 + " false"));
	balance = find_value(r.get_obj(), "balance");
	BOOST_CHECK_EQUAL(balance.getValStr(), "0.00000000");

	BOOST_CHECK_NO_THROW(r = CallRPC("node1", "assetallocationinfo " + assetguid + " " + useraddress3 + " false"));
	balance = find_value(r.get_obj(), "balance");
	BOOST_CHECK_EQUAL(balance.getValStr(), "0.00000000");

	BOOST_CHECK_NO_THROW(r = CallRPC("node1", "assetallocationinfo " + assetguid + " burn false"));
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
       
    string assetguid = AssetNew("node1", "asset1", creatoraddress, "pubdata", "0xc47bD54a3Df2273426829a7928C3526BF8F7Acaa");
    
    AssetSend("node1", assetguid, "\"[{\\\"ownerto\\\":\\\"" + useraddress1 + "\\\",\\\"amount\\\":1.0}]\"", "memoassetburn");
    AssetAllocationTransfer(false, "node1", assetguid, useraddress1, "\"[{\\\"ownerto\\\":\\\"" + useraddress2 + "\\\",\\\"amount\\\":0.1}]\"", "zdagburn");
    
    BurnAssetAllocation("node1", assetguid, useraddress1, "0.8", false);
    
    AssetAllocationTransfer(true, "node1", assetguid, useraddress1, "\"[{\\\"ownerto\\\":\\\"" + useraddress2 + "\\\",\\\"amount\\\":0.4}]\"", "zdagburn");
    // wait for 1 second as required by unit test
    MilliSleep(1000);
    AssetAllocationTransfer(true, "node1", assetguid, useraddress2, "\"[{\\\"ownerto\\\":\\\"" + useraddress3 + "\\\",\\\"amount\\\":0.1}]\"", "zdagburn");
    // wait for 1.5 second to clear minor warning status
    MilliSleep(1500);
    // check just sender, burn marks as major issue on zdag
    BOOST_CHECK_NO_THROW(r = CallRPC("node1", "assetallocationsenderstatus " + assetguid + " " + useraddress1 + " ''"));
    BOOST_CHECK_EQUAL(find_value(r.get_obj(), "status").get_int(), ZDAG_MAJOR_CONFLICT);
    // should affect downstream too
    BOOST_CHECK_NO_THROW(r = CallRPC("node1", "assetallocationsenderstatus " + assetguid + " " + useraddress2 + " ''"));
    BOOST_CHECK_EQUAL(find_value(r.get_obj(), "status").get_int(), ZDAG_MAJOR_CONFLICT);    
    
    GenerateBlocks(5, "node1");
     
    BOOST_CHECK_NO_THROW(r = CallRPC("node1", "assetallocationinfo " + assetguid + " " + useraddress1 + " false"));
    UniValue balance = find_value(r.get_obj(), "balance");
    BOOST_CHECK_EQUAL(balance.getValStr(), "0.50000000"); 
    
    BOOST_CHECK_NO_THROW(r = CallRPC("node1", "assetallocationinfo " + assetguid + " " + useraddress2 + " false"));
    balance = find_value(r.get_obj(), "balance");
    BOOST_CHECK_EQUAL(balance.getValStr(), "0.40000000"); 
    
    BOOST_CHECK_NO_THROW(r = CallRPC("node1", "assetallocationinfo " + assetguid + " " + useraddress3 + " false"));
    balance = find_value(r.get_obj(), "balance");
    BOOST_CHECK_EQUAL(balance.getValStr(), "0.10000000");     
            
    // no burn found        
    BOOST_CHECK_THROW(r = CallRPC("node1", "assetallocationinfo " + assetguid + " burn false"), runtime_error);
    
    // no zdag tx found after block
    BOOST_CHECK_NO_THROW(r = CallRPC("node1", "assetallocationsenderstatus " + assetguid + " " + useraddress1 + " ''"));
    BOOST_CHECK_EQUAL(find_value(r.get_obj(), "status").get_int(), ZDAG_NOT_FOUND);   
    BOOST_CHECK_NO_THROW(r = CallRPC("node1", "assetallocationsenderstatus " + assetguid + " " + useraddress2 + " ''"));
    BOOST_CHECK_EQUAL(find_value(r.get_obj(), "status").get_int(), ZDAG_NOT_FOUND);    

	// now do more zdag and check status are ok this time
	AssetAllocationTransfer(true, "node1", assetguid, useraddress1, "\"[{\\\"ownerto\\\":\\\"" + useraddress2 + "\\\",\\\"amount\\\":0.4}]\"", "zdagburn");
	MilliSleep(1000);
	AssetAllocationTransfer(true, "node1", assetguid, useraddress2, "\"[{\\\"ownerto\\\":\\\"" + useraddress3 + "\\\",\\\"amount\\\":0.1}]\"", "zdagburn");

	BOOST_CHECK_NO_THROW(r = CallRPC("node1", "assetallocationsenderstatus " + assetguid + " " + useraddress2 + " ''"));
	BOOST_CHECK_EQUAL(find_value(r.get_obj(), "status").get_int(), ZDAG_MINOR_CONFLICT);

	MilliSleep(1000);
	BOOST_CHECK_NO_THROW(r = CallRPC("node1", "assetallocationsenderstatus " + assetguid + " " + useraddress1 + " ''"));
	BOOST_CHECK_EQUAL(find_value(r.get_obj(), "status").get_int(), ZDAG_STATUS_OK);
	
	BOOST_CHECK_NO_THROW(r = CallRPC("node1", "assetallocationsenderstatus " + assetguid + " " + useraddress2 + " ''"));
	BOOST_CHECK_EQUAL(find_value(r.get_obj(), "status").get_int(), ZDAG_STATUS_OK);
}
BOOST_AUTO_TEST_CASE(generate_asset_throughput)
{
	UniValue r;
	printf("Running generate_asset_throughput...\n");
	GenerateBlocks(5, "node1");
	GenerateBlocks(5, "node3");
	map<string, string> assetMap;
	map<string, string> assetAddressMap;
	// setup senders and receiver node addresses
	vector<string> senders;
	vector<string> receivers;
	senders.push_back("node1");
	senders.push_back("node2");
	receivers.push_back("node3");
	BOOST_CHECK(receivers.size() == 1);

	int numberOfTransactionToSend = 100;
	// create 1000 addresses and assets for each asset	
	printf("creating sender addresses/assets...\n");
	for (int i = 0; i < numberOfTransactionToSend; i++) {
		string address1 = GetNewFundedAddress("node1");
		string address2 = GetNewFundedAddress("node1");

		BOOST_CHECK_NO_THROW(r = CallRPC("node1", "assetnew tpstest " + address1 + " '' '' 8 false 1 10 0 63 ''"));
		UniValue arr = r.get_array();
		BOOST_CHECK_NO_THROW(r = CallRPC("node1", "signrawtransactionwithwallet " + arr[0].get_str()));
		string hex_str = find_value(r.get_obj(), "hex").get_str();
		BOOST_CHECK_NO_THROW(r = CallRPC("node1", "syscoinsendrawtransaction " + hex_str));

		BOOST_CHECK_NO_THROW(r = CallRPC("node1", "generate 1"));
		string guid = arr[1].get_str();

		BOOST_CHECK_NO_THROW(r = CallRPC("node1", "assetsend " + guid + " \"[{\\\"ownerto\\\":\\\"" + address2 + "\\\",\\\"amount\\\":1}]\" '' ''"));
		arr = r.get_array();
		BOOST_CHECK_NO_THROW(r = CallRPC("node1", "signrawtransactionwithwallet " + arr[0].get_str()));
		hex_str = find_value(r.get_obj(), "hex").get_str();
		BOOST_CHECK_NO_THROW(r = CallRPC("node1", "syscoinsendrawtransaction " + hex_str));
		BOOST_CHECK_NO_THROW(r = CallRPC("node1", "generate 1"));

		assetMap[guid] = address2;
		assetAddressMap[guid] = address1;
		if (i % 100 == 0)
			printf("%.2f percentage done\n", 100.0f * ((float)(i + 1)/(float)numberOfTransactionToSend));

	}

	GenerateBlocks(10);
	printf("Creating assetsend transactions...\n");
	for (auto &sender : senders)
		BOOST_CHECK_NO_THROW(CallExtRPC(sender, "tpstestsetenabled", "true"));
	for (auto &receiver : receivers)
		BOOST_CHECK_NO_THROW(CallExtRPC(receiver, "tpstestsetenabled", "true"));
	int count = 0;
	// setup total senders, and amount that we can add to tpstestadd at once (I noticed that if you push more than 100 or so to tpstestadd at once it will crap out)
	int totalSenderNodes = senders.size();
	int senderNodeCount = 0;
	int totalPerSenderNode = assetMap.size() / totalSenderNodes;
	if (totalPerSenderNode > 100)
		totalPerSenderNode = 100;
	
	// create vector of signed transactions and push them to tpstestadd on every sender node distributed evenly
	string vecTX = "[";
	for (auto& assetTuple : assetMap) {
		count++;
		BOOST_CHECK_NO_THROW(r = CallRPC("node1", "assetallocationsend " + assetTuple.first + " " + assetTuple.second + " \"[{\\\"ownerto\\\":\\\"" + assetAddressMap[assetTuple.first] + "\\\",\\\"amount\\\":1}]\" '' ''"));
		UniValue arr = r.get_array();
		BOOST_CHECK_NO_THROW(r = CallRPC("node1", "signrawtransactionwithwallet " + arr[0].get_str() ));
		string hex_str = find_value(r.get_obj(), "hex").get_str();
		vecTX += "{\"tx\":\"" + hex_str + "\"}";
		if ((count % totalPerSenderNode) == 0) {
			vecTX += "]";
			if (senderNodeCount >= totalSenderNodes)
				senderNodeCount = 0;
			BOOST_CHECK_NO_THROW(CallExtRPC(senders[senderNodeCount], "tpstestadd", "0," + vecTX));
			vecTX = "[";
			senderNodeCount++;
		}
		else
			vecTX += ",";

		if (count % 100 == 0)
			printf("%.2f percentage done\n", 100.0f * ((float)count/(float)numberOfTransactionToSend));


	}
	// set the start time to 1 second from now (this needs to be profiled, if the tpstestadd setting time to every node exceeds say 500ms then this time should be extended to account for the latency).
	// rule of thumb if sender count is high (> 25) then profile how long it takes and multiple by 10 and get ceiling of next second needed to send this rpc to every node to have them sync up

	// this will set a start time to every node which will send the vector of signed txs to the network
	int64_t tpstarttime = GetTimeMicros();
	int microsInSecond = 1000 * 1000;
	tpstarttime = tpstarttime + 1 * microsInSecond;
	printf("Adding assetsend transactions to queue on sender nodes...\n");
	for (auto &sender : senders)
		BOOST_CHECK_NO_THROW(CallExtRPC(sender, "tpstestadd",  boost::lexical_cast<string>(tpstarttime)));
	for (auto &receiver : receivers)
		BOOST_CHECK_NO_THROW(CallExtRPC(receiver, "tpstestadd", boost::lexical_cast<string>(tpstarttime)));

	
	printf("Waiting 11 seconds as per protocol...\n");
	// start 11 second wait
	MilliSleep(11000);

	// get the elapsed time of each node on how long it took to push the vector of signed txs to the network
	int64_t avgteststarttime = 0;
	for (auto &sender : senders) {
		BOOST_CHECK_NO_THROW(r = CallExtRPC(sender, "tpstestinfo"));
		avgteststarttime += find_value(r.get_obj(), "teststarttime").get_int64();
	}
	avgteststarttime /= senders.size();

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

	// avg time per tx it took to hit the mempool
	UniValue tpsresponsereceiversmempool = find_value(tpsresponse, "receivers_mempool").get_array();
	float totalTimeMempool = 0;
	for (size_t i = 0; i < tpsresponsereceiversmempool.size(); i++) {
		const UniValue &responseObj = tpsresponsereceiversmempool[i].get_obj();
		totalTimeMempool += find_value(responseObj, "time").get_int64() - avgteststarttime;
	}
	// average the start time - received time by the number of responses received (usually number of responses should match number of transactions sent beginning of test)
	totalTimeMempool /= tpsresponsereceiversmempool.size();

	printf("tpstarttime %ld avgteststarttime %ld totaltime %.2f, totaltime mempool %.2f num responses %zu\n", tpstarttime, avgteststarttime, totalTime, totalTimeMempool, tpsresponsereceivers.size());
	for (auto &sender : senders)
		BOOST_CHECK_NO_THROW(CallExtRPC(sender, "tpstestsetenabled", "false"));
	for (auto &receiver : receivers)
		BOOST_CHECK_NO_THROW(CallExtRPC(receiver, "tpstestsetenabled", "false"));
}
BOOST_AUTO_TEST_CASE(generate_big_assetname_address)
{
	GenerateBlocks(5);
	printf("Running generate_big_assetname_address...\n");
	GenerateBlocks(5);
	string newaddress = GetNewFundedAddress("node1");
	// 256 bytes long
	string gooddata = "SfsddfdfsdsfSfsdfdfsdsfDsdsdsdsfsfsdsfsdsfdsfsdsfdsfsdsfsdSfsdfdfsdsfSfsdfdfsdsfDsdsdsdsfsfsdsfsdsfdsfsdsfdsfsdsfsdSfsdfdfsdsfSfsdfdfsdsfDsdsdsdsfsfsdsfsdsfdsfsdsfdsfsdsfsdSfsdfdfsdsfSfsdfdfsdsfDsdsdsdsfsfsdsfsdsfdsfsdsfdsfsdsfsdSfsdfdfsdsfSfsdfdfsdsDfdfdd";
	// cannot create this asset because its more than 8 chars
	BOOST_CHECK_THROW(CallRPC("node1", "assetnew 123456789 " + newaddress + " " + gooddata + " '' 8 false 1 1 0 63 ''"), runtime_error);
	// its 3 chars now so its ok
	BOOST_CHECK_NO_THROW(CallRPC("node1", "assetnew abc " + newaddress + " " + gooddata + " '' 8 false 1 1 0 63 ''"));
}
BOOST_AUTO_TEST_CASE(generate_bad_assetmaxsupply_address)
{
	GenerateBlocks(5);
	printf("Running generate_bad_assetmaxsupply_address...\n");
	GenerateBlocks(5);
	string newaddress = GetNewFundedAddress("node1");
	// 256 bytes long
	string gooddata = "SfsddfdfsdsfSfsdfdfsdsfDsdsdsdsfsfsdsfsdsfdsfsdsfdsfsdsfsdSfsdfdfsdsfSfsdfdfsdsfDsdsdsdsfsfsdsfsdsfdsfsdsfdsfsdsfsdSfsdfdfsdsfSfsdfdfsdsfDsdsdsdsfsfsdsfsdsfdsfsdsfdsfsdsfsdSfsdfdfsdsfSfsdfdfsdsfDsdsdsdsfsfsdsfsdsfdsfsdsfdsfsdsfsdSfsdfdfsdsfSfsdfdfsdsDfdfdd";
	// 0 max supply bad
	BOOST_CHECK_THROW(CallRPC("node1", "assetnew abc " + newaddress + " " + gooddata + " '' 8 false 1 0 0 63 ''"), runtime_error);
	// 1 max supply good
	BOOST_CHECK_NO_THROW(CallRPC("node1", "assetnew abc " + newaddress + " " + gooddata + " '' 8 false 1 1 0 63 ''"));
	// balance > max supply
	BOOST_CHECK_THROW(CallRPC("node1", "assetnew abc " + newaddress + " " + gooddata + " '' 3 false 2000 1000 0 63 ''"), runtime_error);
}
BOOST_AUTO_TEST_CASE(generate_assetuppercase)
{
	GenerateBlocks(5);
	printf("Running generate_assetuppercase...\n");
	UniValue r;
	string newaddress = GetNewFundedAddress("node1");
	string guid = AssetNew("node1", "upper", newaddress, "data","''", "8", "false", "1", "1", "0");

	GenerateBlocks(5);
	// assetinfo is case incensitive
	BOOST_CHECK_NO_THROW(r = CallRPC("node1", "assetinfo " + guid + " false"));
	BOOST_CHECK_EQUAL(find_value(r.get_obj(), "symbol").get_str(), "UPPER");
}
BOOST_AUTO_TEST_CASE(generate_assetuppercase_address)
{
	UniValue r;
	GenerateBlocks(5);
	printf("Running generate_assetuppercase_address...\n");
	string newaddress = GetNewFundedAddress("node1");
	string guid = AssetNew("node1", "upper", newaddress, "data","''","8", "false", "1", "1", "0");

	GenerateBlocks(5);
	// assetinfo is case incensitive
	BOOST_CHECK_NO_THROW(r = CallRPC("node1", "assetinfo " + guid + " false"));
	BOOST_CHECK_EQUAL(find_value(r.get_obj(), "symbol").get_str(), "UPPER");
}
BOOST_AUTO_TEST_CASE(generate_asset_collect_interest)
{
	UniValue r;
	printf("Running generate_asset_collect_interest...\n");
	GenerateBlocks(5);
	string newaddress = GetNewFundedAddress("node1");
	string newaddressreceiver = GetNewFundedAddress("node1");
	// setup asset with 5% interest hourly (unit test mode calculates interest hourly not annually)
	string guid = AssetNew("node1", "cad", newaddress, "data","''","8", "false", "10000", "-1", "0.05");

	AssetSend("node1", guid, "\"[{\\\"ownerto\\\":\\\"" + newaddressreceiver + "\\\",\\\"amount\\\":5000}]\"", "memoassetinterest");
	BOOST_CHECK_NO_THROW(r = CallRPC("node1", "assetallocationinfo " + guid + " " + newaddressreceiver + " false"));
	UniValue balance = find_value(r.get_obj(), "balance");
	BOOST_CHECK_EQUAL(AssetAmountFromValue(balance, 8, false), 5000 * COIN);
	BOOST_CHECK_EQUAL(find_value(r.get_obj(), "interest_claim_height").get_int(), find_value(r.get_obj(), "height").get_int());
	// 10 hours later
	GenerateBlocks(60 * 10);
	// calc interest expect 5000 (1 + 0.05 / 60) ^ (60(10)) = ~8248
	AssetClaimInterest("node1", guid, newaddressreceiver);
	BOOST_CHECK_NO_THROW(r = CallRPC("node1", "assetallocationinfo " + guid + " " + newaddressreceiver + " false"));
	balance = find_value(r.get_obj(), "balance");
	BOOST_CHECK_EQUAL(AssetAmountFromValue(balance, 8, false), 824875846664);
	BOOST_CHECK_NO_THROW(r = CallRPC("node2", "assetallocationinfo " + guid + "  " + newaddressreceiver + " false"));
	balance = find_value(r.get_obj(), "balance");
	BOOST_CHECK_EQUAL(AssetAmountFromValue(balance, 8, false), 824875846664);
	BOOST_CHECK_NO_THROW(r = CallRPC("node3", "assetallocationinfo " + guid + "  " + newaddressreceiver + " false"));
	balance = find_value(r.get_obj(), "balance");
	BOOST_CHECK_EQUAL(AssetAmountFromValue(balance, 8, false), 824875846664);

}
/*BOOST_AUTO_TEST_CASE(generate_asset_allocation_interest_overflow)
{
	GenerateBlocks(5);
	printf("Running generate_asset_allocation_interest_overflow...\n");
	GenerateBlocks(5);
	string newaddress = GetNewFundedAddress("node1");
	string newaddressreceiver2 = GetNewFundedAddress("node1");
	string guid = AssetNew("node1", "cad", newaddress, "data", "''","8", "false", "5999999998", "9999999999", "0.00001");
	AssetSend("node1", guid, "\"[{\\\"ownerto\\\":\\\"" + newaddress + "\\\",\\\"amount\\\":5000000000}]\"", "memoassetinterest");
	AssetSend("node1", guid, "\"[{\\\"ownerto\\\":\\\"" + newaddressreceiver2 + "\\\",\\\"amount\\\":10000000}]\"", "memoassetinterest");

	printf("first set of sends...\n");
	for (int i = 0; i < 1100; i++) {
		AssetAllocationTransfer(false, "node1", guid, newaddress, "\"[{\\\"ownerto\\\":\\\"" + newaddressreceiver2 + "\\\",\\\"amount\\\":1}]\"", "allocationsendmemo");
		if ((i % 100)==0)
			printf("%d out of %d completed...\n", i, 1100);
	}
	printf("second set of sends...\n");
	for (int i = 0; i < 1100; i++) {
		AssetAllocationTransfer(false, "node1", guid, newaddressreceiver2, "\"[{\\\"ownerto\\\":\\\"" + newaddress + "\\\",\\\"amount\\\":1}]\"", "allocationsendmemo");
		if ((i % 100)==0)
			printf("%d out of %d completed...\n", i, 1100);
	}
	printf("done now claim interest...\n");
	AssetClaimInterest("node1", guid, newaddress);
}*/
/*
BOOST_AUTO_TEST_CASE(generate_asset_maxsenders)
{
	UniValue r;
	printf("Running generate_asset_maxsenders...\n");
	AliasNew("node1", "fundingmaxsender", "data");
	string guid = AssetNew("node1", "max", "fundingmaxsender", "data","''", "8", "false", "10");
	BOOST_CHECK_THROW(CallRPC("node1", "sendtoaddress fundingmaxsender 200000"), runtime_error);
	GenerateBlocks(5, "node1");
	BOOST_CHECK_NO_THROW(r = CallRPC("node1", "aliasinfo fundingmaxsender"));
	string strAddress = find_value(r.get_obj(), "address").get_str();
	// create 250 aliases
	printf("creating sender 250 aliases...\n");
	BOOST_CHECK_NO_THROW(r = CallRPC("node1", "getblockchaininfo"));
	int64_t mediantime = find_value(r.get_obj(), "mediantime").get_int64();
	mediantime += ONE_YEAR_IN_SECONDS;
	string mediantimestr = boost::lexical_cast<string>(mediantime);
	string senderstring = "\"[";
	for (int i = 0; i < 250; i++) {
		string aliasname = "jagmaxsenders" + boost::lexical_cast<string>(i);
		senderstring += "{\\\"ownerto\\\":\\\"";
		senderstring += aliasname;
		if(i==0)
			senderstring += "\\\",\\\"amount\\\":5.0}";
		else
			senderstring += "\\\",\\\"amount\\\":0.001}";
		if (i < 249)
			senderstring += ",";
		// registration	
		BOOST_CHECK_NO_THROW(r = CallRPC("node1", "aliasnew " + aliasname + " '' 3 " + mediantimestr + " '' ''"));
		UniValue varray = r.get_array();
		BOOST_CHECK_NO_THROW(r = CallRPC("node1", "syscointxfund " + varray[0].get_str() + " " + "\"{\\\"addresses\\\":[\\\"" + strAddress + "\\\"]}\""));
		varray = r.get_array();
		BOOST_CHECK_NO_THROW(r = CallRPC("node1", "signrawtransactionwithwallet " + varray[0].get_str()));
		BOOST_CHECK_NO_THROW(r = CallRPC("node1", "syscoinsendrawtransaction " + find_value(r.get_obj(), "hex").get_str()));
		BOOST_CHECK_NO_THROW(r = CallRPC("node1", "generate 1"));
		// activation	
		BOOST_CHECK_NO_THROW(r = CallRPC("node1", "aliasnew " + aliasname + " '' 3 " + mediantimestr + " '' ''"));
		UniValue varray1 = r.get_array();
		BOOST_CHECK_NO_THROW(r = CallRPC("node1", "syscointxfund " + varray1[0].get_str() + " " + "\"{\\\"addresses\\\":[\\\"" + strAddress + "\\\"]}\""));
		varray1 = r.get_array();
		BOOST_CHECK_NO_THROW(r = CallRPC("node1", "signrawtransactionwithwallet " + varray1[0].get_str()));
		BOOST_CHECK_NO_THROW(r = CallRPC("node1", "syscoinsendrawtransaction " + find_value(r.get_obj(), "hex").get_str()));
		BOOST_CHECK_NO_THROW(r = CallRPC("node1", "generate 1"));
	}
	senderstring += "]\"";
	printf("done now trying to send asset...\n");
	AssetSend("node1", guid, senderstring, "memomaxsend");
	// test asset allocation transfers aswell
	senderstring = "\"[";
	for (int i = 1; i < 250; i++) {
		string aliasname = "jagmaxsenders" + boost::lexical_cast<string>(i);
		senderstring += "{\\\"ownerto\\\":\\\"";
		senderstring += aliasname;
		senderstring += "\\\",\\\"amount\\\":0.001}";
		if (i < 249)
			senderstring += ",";
	}
	senderstring += "]\"";
	AssetAllocationTransfer(false, "node1", guid, "jagmaxsenders0", senderstring, "memomaxsendallocation");

}*/
BOOST_AUTO_TEST_CASE(generate_asset_collect_interest_checktotalsupply_address)
{
	UniValue r;
	printf("Running generate_asset_collect_interest_checktotalsupply_address...\n");
	GenerateBlocks(5);
	string newaddress = GetNewFundedAddress("node1");
	string newaddress1 = GetNewFundedAddress("node1");
	string newaddress2 = GetNewFundedAddress("node1");
	// setup asset with 5% interest hourly (unit test mode calculates interest hourly not annually)
	string guid = AssetNew("node1", "cad", newaddress, "data", "''","8", "false", "50", "100", "0.1");
	AssetSend("node1", guid, "\"[{\\\"ownerto\\\":\\\"" + newaddress1 + "\\\",\\\"amount\\\":20},{\\\"ownerto\\\":\\\"" + newaddress2 + "\\\",\\\"amount\\\":30}]\"", "memoassetinterest");
	BOOST_CHECK_NO_THROW(r = CallRPC("node1", "assetallocationinfo " + guid + " " + newaddress1 + " false"));
	UniValue balance = find_value(r.get_obj(), "balance");
	BOOST_CHECK_EQUAL(AssetAmountFromValue(balance, 8, false), 20 * COIN);
	BOOST_CHECK_EQUAL(find_value(r.get_obj(), "interest_claim_height").get_int(), find_value(r.get_obj(), "height").get_int());

	BOOST_CHECK_NO_THROW(r = CallRPC("node1", "assetallocationinfo " + guid + " " + newaddress2 + " false"));
	balance = find_value(r.get_obj(), "balance");
	BOOST_CHECK_EQUAL(AssetAmountFromValue(balance, 8, false), 30 * COIN);
	BOOST_CHECK_EQUAL(find_value(r.get_obj(), "interest_claim_height").get_int(), find_value(r.get_obj(), "height").get_int());

	BOOST_CHECK_NO_THROW(r = CallRPC("node1", "assetinfo " + guid + " false"));
	UniValue totalsupply = find_value(r.get_obj(), "total_supply");
	UniValue maxsupply = find_value(r.get_obj(), "max_supply");
	BOOST_CHECK_EQUAL(AssetAmountFromValue(totalsupply, 8, false), 50 * COIN);
	BOOST_CHECK_EQUAL(AssetAmountFromValue(maxsupply, 8, false), 100 * COIN);

	// 1 hour later
	GenerateBlocks(60);
	// calc interest expect 20 (1 + 0.1 / 60) ^ (60(1)) = ~22.13 and 30 (1 + 0.1 / 60) ^ (60(1)) = ~33.26
	AssetClaimInterest("node1", guid, newaddress1);
	AssetClaimInterest("node1", guid, newaddress2);
	// ensure total supply and individual supplies are correct after interest claims
	BOOST_CHECK_NO_THROW(r = CallRPC("node1", "assetallocationinfo " + guid + " " + newaddress1 + " false"));
	balance = find_value(r.get_obj(), "balance");
	CAmount nBalance1 = AssetAmountFromValue(balance, 8, false);
	BOOST_CHECK_EQUAL(nBalance1, 2213841445);
	BOOST_CHECK_EQUAL(find_value(r.get_obj(), "interest_claim_height").get_int(), find_value(r.get_obj(), "height").get_int());

	BOOST_CHECK_NO_THROW(r = CallRPC("node1", "assetallocationinfo " + guid + " " + newaddress2 + " false"));
	balance = find_value(r.get_obj(), "balance");
	CAmount nBalance2 = AssetAmountFromValue(balance, 8, false);
	BOOST_CHECK_EQUAL(nBalance2, 3326296793);
	BOOST_CHECK_EQUAL(find_value(r.get_obj(), "interest_claim_height").get_int(), find_value(r.get_obj(), "height").get_int());

	BOOST_CHECK_NO_THROW(r = CallRPC("node1", "assetinfo " + guid + " false"));
	totalsupply = find_value(r.get_obj(), "total_supply");
	maxsupply = find_value(r.get_obj(), "max_supply");
	BOOST_CHECK_EQUAL(AssetAmountFromValue(totalsupply, 8, false), (nBalance1 + nBalance2));
	BOOST_CHECK_EQUAL(AssetAmountFromValue(maxsupply, 8, false), 100 * COIN);
	CAmount supplyRemaining = 100 * COIN - (nBalance1 + nBalance2);
	// mint up to the max supply
	BOOST_CHECK_NO_THROW(r = CallRPC("node1", "assetupdate " + guid + " pub '' " + ValueFromAssetAmount(supplyRemaining, 8, false).write() + " 0.1 [] 63 ''"));
	UniValue arr = r.get_array();
	BOOST_CHECK_NO_THROW(r = CallRPC("node1", "signrawtransactionwithwallet " + arr[0].get_str()));
	string hex_str = find_value(r.get_obj(), "hex").get_str();
	BOOST_CHECK_NO_THROW(r = CallRPC("node1", "syscoinsendrawtransaction " + hex_str));
	GenerateBlocks(5);

	BOOST_CHECK_NO_THROW(r = CallRPC("node1", "assetinfo " + guid + " false"));
	totalsupply = find_value(r.get_obj(), "total_supply");
	maxsupply = find_value(r.get_obj(), "max_supply");
	BOOST_CHECK_EQUAL(AssetAmountFromValue(totalsupply, 8, false), 100 * COIN);
	BOOST_CHECK_EQUAL(AssetAmountFromValue(maxsupply, 8, false), 100 * COIN);

	// totalsupply cannot go > maxsupply
	BOOST_CHECK_THROW(r = CallRPC("node1", "assetupdate " + guid + " jagassetupdate '' 0.001 0.1 [] 63 ''"), runtime_error);
}
BOOST_AUTO_TEST_CASE(generate_asset_collect_interest_average_balance_address)
{
	UniValue r;
	printf("Running generate_asset_collect_interest_average_balance_address...\n");
	GenerateBlocks(5);
	string newaddress = GetNewFundedAddress("node1");
	string newaddress1 = GetNewFundedAddress("node1");
	// setup asset with 5% interest hourly (unit test mode calculates interest hourly not annually)
	string guid = AssetNew("node1", "token", newaddress , "data", "''","8", "false", "10000", "-1", "0.05");
	AssetSend("node1", guid, "\"[{\\\"ownerto\\\":\\\"" + newaddress1 + "\\\",\\\"amount\\\":1000}]\"", "memoassetinterest");
	BOOST_CHECK_NO_THROW(r = CallRPC("node1", "assetallocationinfo " + guid + " " + newaddress1 + " false"));
	UniValue balance = find_value(r.get_obj(), "balance");
	BOOST_CHECK_EQUAL(AssetAmountFromValue(balance, 8, false), 1000 * COIN);
	int claimheight = find_value(r.get_obj(), "height").get_int();
	// 3 hours later send 1k more
	GenerateBlocks((60 * 3) - 1);

	BOOST_CHECK_EQUAL(find_value(r.get_obj(), "interest_claim_height").get_int(), claimheight);
	AssetSend("node1", guid, "\"[{\\\"ownerto\\\":\\\"" + newaddress1 + "\\\",\\\"amount\\\":3000}]\"", "memoassetinterest");
	BOOST_CHECK_NO_THROW(r = CallRPC("node1", "assetallocationinfo " + guid + " " + newaddress1 + " false"));
	balance = find_value(r.get_obj(), "balance");
	BOOST_CHECK_EQUAL(AssetAmountFromValue(balance, 8, false), 4000 * COIN);
	BOOST_CHECK_EQUAL(find_value(r.get_obj(), "interest_claim_height").get_int(), claimheight);
	// 2 hours later send 3k more
	GenerateBlocks((60 * 2) - 1);

	BOOST_CHECK_EQUAL(find_value(r.get_obj(), "interest_claim_height").get_int(), claimheight);
	AssetSend("node1", guid, "\"[{\\\"ownerto\\\":\\\"" + newaddress1 + "\\\",\\\"amount\\\":1000}]\"", "memoassetinterest");
	BOOST_CHECK_NO_THROW(r = CallRPC("node1", "assetallocationinfo " + guid + " " + newaddress1 + " false"));
	balance = find_value(r.get_obj(), "balance");
	BOOST_CHECK_EQUAL(AssetAmountFromValue(balance, 8, false), 5000 * COIN);
	BOOST_CHECK_EQUAL(find_value(r.get_obj(), "interest_claim_height").get_int(), claimheight);

	// 1 hour later send 1k more
	GenerateBlocks((60 * 1) - 1);

	// total interest (1000*180 + 4000*120 + 5000*60) / 360 = 2666.67 - average balance over 6hrs, calculate interest on that balance and apply it to 5k
	// formula is  ((averagebalance*pow((1 + ((double)asset.fInterestRate / 60)), (60*6)))) - averagebalance;
	//  ((2666.67*pow((1 + (0.05 / 60)), (60*6)))) - 2666.67 = 932.5 interest (total 5932.5 balance after interest)
	AssetClaimInterest("node1", guid, newaddress1);
	BOOST_CHECK_NO_THROW(r = CallRPC("node1", "assetallocationinfo " + guid + " " + newaddress1 + " false"));
	balance = find_value(r.get_obj(), "balance");
	BOOST_CHECK_EQUAL(AssetAmountFromValue(balance, 8, false), 593250714517);
	BOOST_CHECK_NO_THROW(r = CallRPC("node2", "assetallocationinfo " + guid + " " + newaddress1 + " false"));
	balance = find_value(r.get_obj(), "balance");
	BOOST_CHECK_EQUAL(AssetAmountFromValue(balance, 8, false), 593250714517);
	BOOST_CHECK_NO_THROW(r = CallRPC("node3", "assetallocationinfo " + guid + " " + newaddress1 + " false"));
	balance = find_value(r.get_obj(), "balance");
	BOOST_CHECK_EQUAL(AssetAmountFromValue(balance, 8, false), 593250714517);
}
BOOST_AUTO_TEST_CASE(generate_asset_collect_interest_update_with_average_balance_address)
{
	UniValue r;
	printf("Running generate_asset_collect_interest_update_with_average_balance_address...\n");
	GenerateBlocks(5);
	string newaddress = GetNewFundedAddress("node1");
	string newaddress1 = GetNewFundedAddress("node1");
	// setup asset with 5% interest hourly (unit test mode calculates interest hourly not annually), can adjust the rate
	string guid = AssetNew("node1", "mytoken", newaddress, "data", "''","8", "false", "10000", "-1", "0.05", "63");
	AssetSend("node1", guid, "\"[{\\\"ownerto\\\":\\\"" + newaddress1 + "\\\",\\\"amount\\\":1000}]\"", "memoassetinterest");
	BOOST_CHECK_NO_THROW(r = CallRPC("node1", "assetallocationinfo " + guid + " " + newaddress1 + " false"));
	UniValue balance = find_value(r.get_obj(), "balance");
	BOOST_CHECK_EQUAL(AssetAmountFromValue(balance, 8, false), 1000 * COIN);
	int claimheight = find_value(r.get_obj(), "height").get_int();
	// 3 hours later send 1k more
	GenerateBlocks((60 * 3) - 11);
	// update interest rate to 10%
	AssetUpdate("node1", guid, "pub", "''", "0.1");
	BOOST_CHECK_EQUAL(find_value(r.get_obj(), "interest_claim_height").get_int(), claimheight);
	AssetSend("node1", guid, "\"[{\\\"ownerto\\\":\\\"" + newaddress1 + "\\\",\\\"amount\\\":3000}]\"", "memoassetinterest");
	BOOST_CHECK_NO_THROW(r = CallRPC("node1", "assetallocationinfo " + guid + " " + newaddress1 + " false"));
	balance = find_value(r.get_obj(), "balance");
	BOOST_CHECK_EQUAL(AssetAmountFromValue(balance, 8, false), 4000 * COIN);
	BOOST_CHECK_EQUAL(find_value(r.get_obj(), "interest_claim_height").get_int(), claimheight);
	// 2 hours later send 3k more
	GenerateBlocks((60 * 2) - 11);

	// interest rate to back to 5%
	AssetUpdate("node1", guid, "pub", "''", "0.05");
	BOOST_CHECK_EQUAL(find_value(r.get_obj(), "interest_claim_height").get_int(), claimheight);
	AssetSend("node1", guid, "\"[{\\\"ownerto\\\":\\\"" + newaddress1 + "\\\",\\\"amount\\\":1000}]\"", "memoassetinterest");
	BOOST_CHECK_NO_THROW(r = CallRPC("node1", "assetallocationinfo " + guid + " " + newaddress1 + " false"));
	balance = find_value(r.get_obj(), "balance");
	BOOST_CHECK_EQUAL(AssetAmountFromValue(balance, 8, false), 5000 * COIN);
	BOOST_CHECK_EQUAL(find_value(r.get_obj(), "interest_claim_height").get_int(), claimheight);

	// 1 hour later send 1k more
	GenerateBlocks((60 * 1) - 11);

	// at the end set rate to 50% but this shouldn't affect the result since we set this rate recently
	AssetUpdate("node1", guid, "pub", "''", "0.5");
	// total interest (1000*180 + 4000*120 + 5000*60) / 360 = 2666.67 - average balance over 6hrs, calculate interest on that balance and apply it to 5k
	// total interest rate (0.05*180 + 0.1*120 + 0.05*60) / 360 = 0.0667% - average interest over 6hrs
	// formula is  ((averagebalance*pow((1 + ((double)asset.fInterestRate / 60)), (60*6)))) - averagebalance;
	//  ((2666.67*pow((1 + (0.0667 / 60)), (60*6)))) - 2666.67 = 1310.65 interest (total about 6310.65 balance after interest)
	AssetClaimInterest("node1", guid, newaddress1);
	BOOST_CHECK_NO_THROW(r = CallRPC("node1", "assetallocationinfo " + guid + " " + newaddress1 + " false"));
	balance = find_value(r.get_obj(), "balance");
	BOOST_CHECK_EQUAL(AssetAmountFromValue(balance, 8, false), 631064923515);
	BOOST_CHECK_NO_THROW(r = CallRPC("node2", "assetallocationinfo " + guid + " " + newaddress1 + " false"));
	balance = find_value(r.get_obj(), "balance");
	BOOST_CHECK_EQUAL(AssetAmountFromValue(balance, 8, false), 631064923515);
	BOOST_CHECK_NO_THROW(r = CallRPC("node3", "assetallocationinfo " + guid + " " + newaddress1 + " false"));
	balance = find_value(r.get_obj(), "balance");
	BOOST_CHECK_EQUAL(AssetAmountFromValue(balance, 8, false), 631064923515);
}
BOOST_AUTO_TEST_CASE(generate_asset_collect_interest_every_block_address)
{
	UniValue r;
	printf("Running generate_asset_collect_interest_every_block_address...\n");
	GenerateBlocks(5);
	string newaddress = GetNewFundedAddress("node1");
	string newaddress1 = GetNewFundedAddress("node1");
	// setup asset with 10% interest hourly (unit test mode calculates interest hourly not annually)
	string guid = AssetNew("node1", "a", newaddress, "data", "''","8", "false", "10000", "-1", "0.05");
	AssetSend("node1", guid, "\"[{\\\"ownerto\\\":\\\"" + newaddress1 + "\\\",\\\"amount\\\":5000}]\"", "memoassetinterest1");
	BOOST_CHECK_NO_THROW(r = CallRPC("node1", "assetallocationinfo " + guid + " " + newaddress1 + " false"));
	UniValue balance = find_value(r.get_obj(), "balance");
	BOOST_CHECK_EQUAL(AssetAmountFromValue(balance, 8, false), 5000 * COIN);
	BOOST_CHECK_EQUAL(find_value(r.get_obj(), "interest_claim_height").get_int(), find_value(r.get_obj(), "height").get_int());
	// 10 hours later
	// calc interest expect 5000 (1 + 0.05 / 60) ^ (60(10)) = ~8248
	for (int i = 0; i <= 60 * 10; i += 25) {
		AssetClaimInterest("node1", guid, newaddress1);
		GenerateBlocks(24);
		printf("Claiming interest %d of out %d...\n", i, 60 * 10);
	}
	BOOST_CHECK_NO_THROW(r = CallRPC("node1", "assetallocationinfo " + guid + " " + newaddress1 + " false"));
	balance = find_value(r.get_obj(), "balance");
	BOOST_CHECK_EQUAL(AssetAmountFromValue(balance, 8, false), 824875830937);
	BOOST_CHECK_NO_THROW(r = CallRPC("node2", "assetallocationinfo " + guid + " " + newaddress1 + " false"));
	balance = find_value(r.get_obj(), "balance");
	BOOST_CHECK_EQUAL(AssetAmountFromValue(balance, 8, false), 824875830937);
	BOOST_CHECK_NO_THROW(r = CallRPC("node3", "assetallocationinfo " + guid + " " + newaddress1 + " false"));
	balance = find_value(r.get_obj(), "balance");
	BOOST_CHECK_EQUAL(AssetAmountFromValue(balance, 8, false), 824875830937);
}
BOOST_AUTO_TEST_CASE(generate_assetupdate_address)
{
	printf("Running generate_assetupdate_address...\n");
	string newaddress = GetNewFundedAddress("node1");
	string guid = AssetNew("node1", "b", newaddress, "data");
	// update an asset that isn't yours
	UniValue r;
	//"assetupdate [asset] [public] [supply] [interest_rate] [witness]\n"
	BOOST_CHECK_NO_THROW(r = CallRPC("node2", "assetupdate " + guid + " " + newaddress + " '' 1 0 [] 63 ''"));
	UniValue arr = r.get_array();
	BOOST_CHECK_NO_THROW(r = CallRPC("node2", "signrawtransactionwithwallet " + arr[0].get_str()));
	BOOST_CHECK(!find_value(r.get_obj(), "complete").get_bool());

	AssetUpdate("node1", guid, "pub1");
	// shouldnt update data, just uses prev data because it hasnt changed
	AssetUpdate("node1", guid);
	// update supply, ensure balance gets updated properly, 5+1, 1 comes from the initial assetnew, 1 above doesn't actually get set because asset wasn't yours so total should be 6
	AssetUpdate("node1", guid, "pub12", "5");
	BOOST_CHECK_NO_THROW(r = CallRPC("node1", "assetinfo " + guid + " false"));
	UniValue balance = find_value(r.get_obj(), "balance");
	BOOST_CHECK_EQUAL(AssetAmountFromValue(balance, 8, false), 6 * COIN);
	// update interest rate
    int updateflags = 63 & ~ASSET_UPDATE_SUPPLY;
	string guid1 = AssetNew("node1", "c", newaddress, "data", "''", "8","false", "1", "10", "0.1", "63");
    // can't change supply > max supply (current balance already 6, max is 10)
    BOOST_CHECK_THROW(r = CallRPC("node1", "assetupdate " + guid + " " + newaddress + " '' 5 0 [] " + boost::lexical_cast<string>(updateflags) + " ''"), runtime_error);
	AssetUpdate("node1", guid1, "pub12", "1", "0.25", "[]", boost::lexical_cast<string>(updateflags));
	// ensure can't update interest rate (update flags is set to not allow interest rate/supply update)
	BOOST_CHECK_THROW(r = CallRPC("node1", "assetupdate " + guid1 + " " + newaddress + " '' 1 0.11 [] " + boost::lexical_cast<string>(updateflags) + " ''"), runtime_error);
    // can't update supply or interest rate
    BOOST_CHECK_THROW(r = CallRPC("node1", "assetupdate " + guid1 + " " + newaddress + " '' 1 0 [] " + boost::lexical_cast<string>(updateflags) + " ''"), runtime_error);

}
BOOST_AUTO_TEST_CASE(generate_assetupdate_precision_address)
{
	printf("Running generate_assetupdate_precision_address...\n");
	UniValue r;
	for (int i = 0; i <= 8; i++) {
		string istr = boost::lexical_cast<string>(i);
		string assetName = "asset" + istr;
		string addressName = GetNewFundedAddress("node1");
		// test max supply for every possible precision
		string guid = AssetNew("node1", assetName, addressName, "data","''", istr, "false", "1", "-1");
		UniValue negonevalue(UniValue::VSTR);
		negonevalue.setStr("-1");
		CAmount precisionCoin = powf(10, i);
		// get max value - 1 (1 is already the supply, and this value is cumulative)
		CAmount negonesupply = AssetAmountFromValue(negonevalue, i, false) - precisionCoin;
		string maxstr = ValueFromAssetAmount(negonesupply, i, false).get_str();
		AssetUpdate("node1", guid, "pub12", maxstr);
		// can't go above max balance (10^18) / (10^i) for i decimal places
		BOOST_CHECK_THROW(r = CallRPC("node1", "assetupdate " + guid + " pub '' 1 0 [] 63 ''"), runtime_error);
		// can't create asset with more than max+1 balance or max+1 supply
		string maxstrplusone = ValueFromAssetAmount(negonesupply + (precisionCoin * 2), i, false).get_str();
		maxstr = ValueFromAssetAmount(negonesupply + precisionCoin, i, false).get_str();
		BOOST_CHECK_NO_THROW(CallRPC("node1", "assetnew  " + assetName + "2 " + addressName + " pub '' " + istr + " false " + maxstr + " -1 0 63 ''"));
		BOOST_CHECK_NO_THROW(CallRPC("node1", "assetnew  " + assetName + "2 " + addressName + " pub '' " + istr + " false 1 " + maxstr + " 0 63 ''"));
		BOOST_CHECK_THROW(CallRPC("node1", "assetnew  " + assetName + "2 " + addressName + " pub '' " + istr + " false " + maxstrplusone + " -1 0 63 ''"), runtime_error);
		BOOST_CHECK_THROW(CallRPC("node1", "assetnew  " + assetName + "2 " + addressName + " pub '' " + istr + " false 1 " + maxstrplusone + " 0 63 ''"), runtime_error);
	}
	string newaddress = GetNewFundedAddress("node1");
	// invalid precisions
	BOOST_CHECK_THROW(CallRPC("node1", "assetnew high " + newaddress + " pub '' 9 false 1 2 0 63 ''"), runtime_error);
	BOOST_CHECK_THROW(CallRPC("node1", "assetnew low " + newaddress + " pub '' -1 false 1 2 0 63 ''"), runtime_error);

	// try an input range asset for 10m max with precision 0
	// for fun try to use precision 4 for input range it should default to 0
	string istr = boost::lexical_cast<string>(4);
	int i = 0;
	string assetName = "usd" + istr;
	string addressName = GetNewFundedAddress("node1");
	
	// test max supply
	string guid1 = AssetNew("node1", assetName, addressName, "data", "''",istr, "true", "1", "-1");
	UniValue negonevalue(UniValue::VSTR);
	negonevalue.setStr("-1");
	CAmount precisionCoin = powf(10, i);
	// get max value - 1 (1 is already the supply, and this value is cumulative)
	CAmount negonesupply = AssetAmountFromValue(negonevalue, i, true) - precisionCoin;
	string maxstr = ValueFromAssetAmount(negonesupply, i, true).get_str();
	AssetUpdate("node1", guid1, "pub12", maxstr);
	// can't go above max balance (10^18) / (10^i) for i decimal places
	BOOST_CHECK_THROW(r = CallRPC("node1", "assetupdate " + guid1 + " pub '' 1 0 [] 63 ''"), runtime_error);
	// can't create asset with more than max+1 balance or max+1 supply
	string maxstrplusone = ValueFromAssetAmount(negonesupply + (precisionCoin * 2), i, true).get_str();
	maxstr = ValueFromAssetAmount(negonesupply + precisionCoin, i, true).get_str();
	BOOST_CHECK_NO_THROW(CallRPC("node1", "assetnew  " + assetName + "2 " + addressName + " pub '' " + istr + " true " + maxstr + " -1 0 63 ''"));
	BOOST_CHECK_NO_THROW(CallRPC("node1", "assetnew  " + assetName + "2 " + addressName + " pub '' " + istr + " true 1 " + maxstr + " 0 63 ''"));
	BOOST_CHECK_THROW(CallRPC("node1", "assetnew  " + assetName + "2 " + addressName + " pub '' " + istr + " true " + maxstrplusone + " -1 0 63 ''"), runtime_error);
	BOOST_CHECK_THROW(CallRPC("node1", "assetnew  " + assetName + "2 " + addressName + " pub '' " + istr + " true 1 " + maxstrplusone + " 0 63 ''"), runtime_error);

}
BOOST_AUTO_TEST_CASE(generate_assetsend_address)
{
	UniValue r;
	printf("Running generate_assetsend_address...\n");
	string newaddress = GetNewFundedAddress("node1");
	string newaddress1 = GetNewFundedAddress("node1");
	string guid = AssetNew("node1", "elf", newaddress, "data", "''", "8","false", "10", "20");
	// [{\"ownerto\":\"address\",\"amount\":amount},...]
	AssetSend("node1", guid, "\"[{\\\"ownerto\\\":\\\"" + newaddress1 + "\\\",\\\"amount\\\":7}]\"", "memoassetsend");
	// ensure amounts are correct
	BOOST_CHECK_NO_THROW(r = CallRPC("node1", "assetinfo " + guid + " true"));
	UniValue balance = find_value(r.get_obj(), "balance");
	UniValue totalsupply = find_value(r.get_obj(), "total_supply");
	UniValue maxsupply = find_value(r.get_obj(), "max_supply");
	BOOST_CHECK_EQUAL(AssetAmountFromValue(balance, 8, false), 3 * COIN);
	BOOST_CHECK_EQUAL(AssetAmountFromValue(totalsupply, 8, false), 10 * COIN);
	BOOST_CHECK_EQUAL(AssetAmountFromValue(maxsupply, 8, false), 20 * COIN);
	UniValue inputs = find_value(r.get_obj(), "inputs");
	BOOST_CHECK(inputs.isArray());
	UniValue inputsArray = inputs.get_array();
	BOOST_CHECK(inputsArray.size() == 0);
	// ensure receiver get's it
	BOOST_CHECK_NO_THROW(r = CallRPC("node1", "assetallocationinfo " + guid + " " + newaddress1 + " true"));
	inputs = find_value(r.get_obj(), "inputs");
	BOOST_CHECK(inputs.isArray());
	inputsArray = inputs.get_array();
	BOOST_CHECK(inputsArray.size() == 0);
	balance = find_value(r.get_obj(), "balance");
	BOOST_CHECK_EQUAL(AssetAmountFromValue(balance, 8, false), 7 * COIN);
	BOOST_CHECK_EQUAL(find_value(r.get_obj(), "memo").get_str(), "memoassetsend");
	BOOST_CHECK_EQUAL(find_value(r.get_obj(), "interest_claim_height").get_int(), find_value(r.get_obj(), "height").get_int());

	// add balances
	AssetUpdate("node1", guid, "pub12", "1");
	// check balance is added to end
	BOOST_CHECK_NO_THROW(r = CallRPC("node1", "assetinfo " + guid + " true"));
	balance = find_value(r.get_obj(), "balance");
	totalsupply = find_value(r.get_obj(), "total_supply");
	maxsupply = find_value(r.get_obj(), "max_supply");
	BOOST_CHECK_EQUAL(AssetAmountFromValue(balance, 8, false), 4 * COIN);
	BOOST_CHECK_EQUAL(AssetAmountFromValue(totalsupply, 8, false), 11 * COIN);
	BOOST_CHECK_EQUAL(AssetAmountFromValue(maxsupply, 8, false), 20 * COIN);
	inputs = find_value(r.get_obj(), "inputs");
	BOOST_CHECK(inputs.isArray());
	inputsArray = inputs.get_array();
	BOOST_CHECK(inputsArray.size() == 0);
	AssetUpdate("node1", guid, "pub12", "9");
	// check balance is added to end
	BOOST_CHECK_NO_THROW(r = CallRPC("node1", "assetinfo " + guid + " true"));
	balance = find_value(r.get_obj(), "balance");
	totalsupply = find_value(r.get_obj(), "total_supply");
	maxsupply = find_value(r.get_obj(), "max_supply");
	BOOST_CHECK_EQUAL(AssetAmountFromValue(balance, 8, false), 13 * COIN);
	BOOST_CHECK_EQUAL(AssetAmountFromValue(totalsupply, 8, false), 20 * COIN);
	BOOST_CHECK_EQUAL(AssetAmountFromValue(maxsupply, 8, false), 20 * COIN);
	inputs = find_value(r.get_obj(), "inputs");
	BOOST_CHECK(inputs.isArray());
	inputsArray = inputs.get_array();
	BOOST_CHECK(inputsArray.size() == 0);
	// can't go over 20 supply
	BOOST_CHECK_THROW(r = CallRPC("node1", "assetupdate " + guid + " " + newaddress + " '' 1 0 [] 63 ''"), runtime_error);
}
BOOST_AUTO_TEST_CASE(generate_assetsend_ranges_address)
{
	UniValue r;
	printf("Running generate_assetsend_ranges_address...\n");
	string newaddress = GetNewFundedAddress("node1");
	string newaddress1 = GetNewFundedAddress("node1");
	// if use input ranges update supply and ensure adds to end of allocation, ensure balance gets updated properly
	string guid = AssetNew("node1", "msft", newaddress, "data", "''","8", "true", "10", "20");
	// send range 1-2, 4-6, 8-9 and then add 1 balance and expect it to add to 10, add 9 more and expect it to add to 11, try to add one more and won't let you due to max 20 supply
	// [{\"ownerto\":\"aliasname\",\"ranges\":[{\"start\":index,\"end\":index},...]},...]
	// break ranges into 0, 3, 7
	AssetSend("node1", guid, "\"[{\\\"ownerto\\\":\\\"" + newaddress1 + "\\\",\\\"ranges\\\":[{\\\"start\\\":1,\\\"end\\\":2},{\\\"start\\\":4,\\\"end\\\":6},{\\\"start\\\":8,\\\"end\\\":9}]}]\"", "memoassetsendranges");
	// ensure receiver get's it
	BOOST_CHECK_NO_THROW(r = CallRPC("node1", "assetallocationinfo " + guid + " " + newaddress1 + " true"));
	UniValue inputs = find_value(r.get_obj(), "inputs");
	BOOST_CHECK(inputs.isArray());
	UniValue inputsArray = inputs.get_array();
	BOOST_CHECK_EQUAL(inputsArray.size(), 3);
	BOOST_CHECK_EQUAL(find_value(inputsArray[0].get_obj(), "start").get_int(), 1);
	BOOST_CHECK_EQUAL(find_value(inputsArray[0].get_obj(), "end").get_int(), 2);
	BOOST_CHECK_EQUAL(find_value(inputsArray[1].get_obj(), "start").get_int(), 4);
	BOOST_CHECK_EQUAL(find_value(inputsArray[1].get_obj(), "end").get_int(), 6);
	BOOST_CHECK_EQUAL(find_value(inputsArray[2].get_obj(), "start").get_int(), 8);
	BOOST_CHECK_EQUAL(find_value(inputsArray[2].get_obj(), "end").get_int(), 9);
	UniValue balance = find_value(r.get_obj(), "balance");
	BOOST_CHECK_EQUAL(AssetAmountFromValue(balance, 8, true), 7);
	BOOST_CHECK_EQUAL(find_value(r.get_obj(), "memo").get_str(), "memoassetsendranges");
	BOOST_CHECK_EQUAL(find_value(r.get_obj(), "interest_claim_height").get_int(), find_value(r.get_obj(), "height").get_int());

	// ensure ranges are correct
	BOOST_CHECK_NO_THROW(r = CallRPC("node1", "assetinfo " + guid + " true"));
	balance = find_value(r.get_obj(), "balance");
	UniValue totalsupply = find_value(r.get_obj(), "total_supply");
	UniValue maxsupply = find_value(r.get_obj(), "max_supply");
	BOOST_CHECK_EQUAL(AssetAmountFromValue(balance, 8, true), 3);
	BOOST_CHECK_EQUAL(AssetAmountFromValue(totalsupply, 8, true), 10);
	BOOST_CHECK_EQUAL(AssetAmountFromValue(maxsupply, 8, true), 20);
	inputs = find_value(r.get_obj(), "inputs");
	BOOST_CHECK(inputs.isArray());
	inputsArray = inputs.get_array();
	BOOST_CHECK_EQUAL(inputsArray.size(), 3);
	BOOST_CHECK_EQUAL(find_value(inputsArray[0].get_obj(), "start").get_int(), 0);
	BOOST_CHECK_EQUAL(find_value(inputsArray[0].get_obj(), "end").get_int(), 0);
	BOOST_CHECK_EQUAL(find_value(inputsArray[1].get_obj(), "start").get_int(), 3);
	BOOST_CHECK_EQUAL(find_value(inputsArray[1].get_obj(), "end").get_int(), 3);
	BOOST_CHECK_EQUAL(find_value(inputsArray[2].get_obj(), "start").get_int(), 7);
	BOOST_CHECK_EQUAL(find_value(inputsArray[2].get_obj(), "end").get_int(), 7);
	// add balances, expect to add to range 10 because total supply is 10 (0-9 range)
	AssetUpdate("node1", guid, "pub12", "1");
	// check balance is added to end
	BOOST_CHECK_NO_THROW(r = CallRPC("node1", "assetinfo " + guid + " true"));
	balance = find_value(r.get_obj(), "balance");
	totalsupply = find_value(r.get_obj(), "total_supply");
	maxsupply = find_value(r.get_obj(), "max_supply");
	BOOST_CHECK_EQUAL(AssetAmountFromValue(balance, 8, true), 4);
	BOOST_CHECK_EQUAL(AssetAmountFromValue(totalsupply, 8, true), 11);
	BOOST_CHECK_EQUAL(AssetAmountFromValue(maxsupply, 8, true), 20);
	inputs = find_value(r.get_obj(), "inputs");
	BOOST_CHECK(inputs.isArray());
	inputsArray = inputs.get_array();
	BOOST_CHECK_EQUAL(inputsArray.size(), 4);
	BOOST_CHECK_EQUAL(find_value(inputsArray[0].get_obj(), "start").get_int(), 0);
	BOOST_CHECK_EQUAL(find_value(inputsArray[0].get_obj(), "end").get_int(), 0);
	BOOST_CHECK_EQUAL(find_value(inputsArray[1].get_obj(), "start").get_int(), 3);
	BOOST_CHECK_EQUAL(find_value(inputsArray[1].get_obj(), "end").get_int(), 3);
	BOOST_CHECK_EQUAL(find_value(inputsArray[2].get_obj(), "start").get_int(), 7);
	BOOST_CHECK_EQUAL(find_value(inputsArray[2].get_obj(), "end").get_int(), 7);
	BOOST_CHECK_EQUAL(find_value(inputsArray[3].get_obj(), "start").get_int(), 10);
	BOOST_CHECK_EQUAL(find_value(inputsArray[3].get_obj(), "end").get_int(), 10);
	AssetUpdate("node1", guid, "pub12", "9");
	// check balance is added to end, last range expected 10-19
	BOOST_CHECK_NO_THROW(r = CallRPC("node1", "assetinfo " + guid + " true"));
	balance = find_value(r.get_obj(), "balance");
	totalsupply = find_value(r.get_obj(), "total_supply");
	maxsupply = find_value(r.get_obj(), "max_supply");
	BOOST_CHECK_EQUAL(AssetAmountFromValue(balance, 8, true), 13);
	BOOST_CHECK_EQUAL(AssetAmountFromValue(totalsupply, 8, true), 20);
	BOOST_CHECK_EQUAL(AssetAmountFromValue(maxsupply, 8, true), 20);
	inputs = find_value(r.get_obj(), "inputs");
	BOOST_CHECK(inputs.isArray());
	inputsArray = inputs.get_array();
	BOOST_CHECK_EQUAL(inputsArray.size(), 4);
	BOOST_CHECK_EQUAL(find_value(inputsArray[0].get_obj(), "start").get_int(), 0);
	BOOST_CHECK_EQUAL(find_value(inputsArray[0].get_obj(), "end").get_int(), 0);
	BOOST_CHECK_EQUAL(find_value(inputsArray[1].get_obj(), "start").get_int(), 3);
	BOOST_CHECK_EQUAL(find_value(inputsArray[1].get_obj(), "end").get_int(), 3);
	BOOST_CHECK_EQUAL(find_value(inputsArray[2].get_obj(), "start").get_int(), 7);
	BOOST_CHECK_EQUAL(find_value(inputsArray[2].get_obj(), "end").get_int(), 7);
	BOOST_CHECK_EQUAL(find_value(inputsArray[3].get_obj(), "start").get_int(), 10);
	BOOST_CHECK_EQUAL(find_value(inputsArray[3].get_obj(), "end").get_int(), 19);
	// can't go over 20 supply
	BOOST_CHECK_THROW(r = CallRPC("node1", "assetupdate " + guid + " " + newaddress + " '' 1 0 [] 63 ''"), runtime_error);
}
BOOST_AUTO_TEST_CASE(generate_assetsend_ranges2_address)
{
	// create an asset, assetsend the initial allocation to myself, assetallocationsend the entire allocation to 5 other aliases, 5 other aliases send all those allocations back to me, then mint 1000 new tokens
	UniValue r;
	printf("Running generate_assetsend_ranges2_address...\n");
	string newaddressowner = GetNewFundedAddress("node1");
	string newaddressownerallocation = GetNewFundedAddress("node1");
	string newaddressrangesa = GetNewFundedAddress("node1");
	string newaddressrangesb = GetNewFundedAddress("node1");
	string newaddressrangesc = GetNewFundedAddress("node1");
	string newaddressrangesd = GetNewFundedAddress("node1");
	string newaddressrangese = GetNewFundedAddress("node1");
	
	// if use input ranges update supply and ensure adds to end of allocation, ensure balance gets updated properly
	string guid = AssetNew("node1", "asset", newaddressowner, "asset", "''","8", "true", "1000", "1000000");

	AssetSend("node1", guid, "\"[{\\\"ownerto\\\":\\\"" + newaddressownerallocation + "\\\",\\\"ranges\\\":[{\\\"start\\\":0,\\\"end\\\":999}]}]\"", "memo1");

	BOOST_CHECK_NO_THROW(r = CallRPC("node1", "assetinfo " + guid + " true"));
	UniValue inputs = find_value(r.get_obj(), "inputs");
	BOOST_CHECK(inputs.isArray());
	UniValue inputsArray = inputs.get_array();

	AssetAllocationTransfer(true, "node1", guid, "" + newaddressownerallocation + "", "\"[{\\\"ownerto\\\":\\\"" + newaddressrangesa + "\\\",\\\"ranges\\\":[{\\\"start\\\":0,\\\"end\\\":199}]},{\\\"ownerto\\\":\\\"" + newaddressrangesb + "\\\",\\\"ranges\\\":[{\\\"start\\\":200,\\\"end\\\":399}]},{\\\"ownerto\\\":\\\"" + newaddressrangesc + "\\\",\\\"ranges\\\":[{\\\"start\\\":400,\\\"end\\\":599}]},{\\\"ownerto\\\":\\\"" + newaddressrangesd + "\\\",\\\"ranges\\\":[{\\\"start\\\":600,\\\"end\\\":799}]},{\\\"ownerto\\\":\\\"" + newaddressrangese + "\\\",\\\"ranges\\\":[{\\\"start\\\":800,\\\"end\\\":999}]}]\"", "memo");

	// ensure receiver get's it
	BOOST_CHECK_NO_THROW(r = CallRPC("node1", "assetallocationinfo " + guid + " " + newaddressrangesa + " true"));
	inputs = find_value(r.get_obj(), "inputs");
	BOOST_CHECK(inputs.isArray());
	inputsArray = inputs.get_array();
	BOOST_CHECK_EQUAL(inputsArray.size(), 1);
	BOOST_CHECK_EQUAL(find_value(inputsArray[0].get_obj(), "start").get_int(), 0);
	BOOST_CHECK_EQUAL(find_value(inputsArray[0].get_obj(), "end").get_int(), 199);
	UniValue balance = find_value(r.get_obj(), "balance");
	BOOST_CHECK_EQUAL(AssetAmountFromValue(balance, 8, true), 200);
	BOOST_CHECK_EQUAL(find_value(r.get_obj(), "memo").get_str(), "memo");

	BOOST_CHECK_NO_THROW(r = CallRPC("node1", "assetallocationinfo " + guid + " " + newaddressrangesb + " true"));
	inputs = find_value(r.get_obj(), "inputs");
	BOOST_CHECK(inputs.isArray());
	inputsArray = inputs.get_array();
	BOOST_CHECK_EQUAL(inputsArray.size(), 1);
	BOOST_CHECK_EQUAL(find_value(inputsArray[0].get_obj(), "start").get_int(), 200);
	BOOST_CHECK_EQUAL(find_value(inputsArray[0].get_obj(), "end").get_int(), 399);
	balance = find_value(r.get_obj(), "balance");
	BOOST_CHECK_EQUAL(AssetAmountFromValue(balance, 8, true), 200);
	BOOST_CHECK_EQUAL(find_value(r.get_obj(), "memo").get_str(), "memo");

	BOOST_CHECK_NO_THROW(r = CallRPC("node1", "assetallocationinfo " + guid + " " + newaddressrangesc + " true"));
	inputs = find_value(r.get_obj(), "inputs");
	BOOST_CHECK(inputs.isArray());
	inputsArray = inputs.get_array();
	BOOST_CHECK_EQUAL(inputsArray.size(), 1);
	BOOST_CHECK_EQUAL(find_value(inputsArray[0].get_obj(), "start").get_int(), 400);
	BOOST_CHECK_EQUAL(find_value(inputsArray[0].get_obj(), "end").get_int(), 599);
	balance = find_value(r.get_obj(), "balance");
	BOOST_CHECK_EQUAL(AssetAmountFromValue(balance, 8, true), 200);
	BOOST_CHECK_EQUAL(find_value(r.get_obj(), "memo").get_str(), "memo");

	BOOST_CHECK_NO_THROW(r = CallRPC("node1", "assetallocationinfo " + guid + " " + newaddressrangesd + " true"));
	inputs = find_value(r.get_obj(), "inputs");
	BOOST_CHECK(inputs.isArray());
	inputsArray = inputs.get_array();
	BOOST_CHECK_EQUAL(inputsArray.size(), 1);
	BOOST_CHECK_EQUAL(find_value(inputsArray[0].get_obj(), "start").get_int(), 600);
	BOOST_CHECK_EQUAL(find_value(inputsArray[0].get_obj(), "end").get_int(), 799);
	balance = find_value(r.get_obj(), "balance");
	BOOST_CHECK_EQUAL(AssetAmountFromValue(balance, 8, true), 200);
	BOOST_CHECK_EQUAL(find_value(r.get_obj(), "memo").get_str(), "memo");

	BOOST_CHECK_NO_THROW(r = CallRPC("node1", "assetallocationinfo " + guid + " " + newaddressrangese + " true"));
	inputs = find_value(r.get_obj(), "inputs");
	BOOST_CHECK(inputs.isArray());
	inputsArray = inputs.get_array();
	BOOST_CHECK_EQUAL(inputsArray.size(), 1);
	BOOST_CHECK_EQUAL(find_value(inputsArray[0].get_obj(), "start").get_int(), 800);
	BOOST_CHECK_EQUAL(find_value(inputsArray[0].get_obj(), "end").get_int(), 999);
	balance = find_value(r.get_obj(), "balance");
	BOOST_CHECK_EQUAL(AssetAmountFromValue(balance, 8, true), 200);
	BOOST_CHECK_EQUAL(find_value(r.get_obj(), "memo").get_str(), "memo");


	AssetUpdate("node1", guid, "ASSET", "1000");
	// check balance is added to end
	BOOST_CHECK_NO_THROW(r = CallRPC("node1", "assetinfo " + guid + " true"));
	balance = find_value(r.get_obj(), "balance");
	UniValue totalsupply = find_value(r.get_obj(), "total_supply");
	UniValue maxsupply = find_value(r.get_obj(), "max_supply");
	BOOST_CHECK_EQUAL(AssetAmountFromValue(balance, 8, true), 1000);
	BOOST_CHECK_EQUAL(AssetAmountFromValue(totalsupply, 8, true), 2000);
	BOOST_CHECK_EQUAL(AssetAmountFromValue(maxsupply, 8, true), 1000000);
	inputs = find_value(r.get_obj(), "inputs");
	BOOST_CHECK(inputs.isArray());
	inputsArray = inputs.get_array();
	BOOST_CHECK_EQUAL(inputsArray.size(), 1);
	BOOST_CHECK_EQUAL(find_value(inputsArray[0].get_obj(), "start").get_int(), 1000);
	BOOST_CHECK_EQUAL(find_value(inputsArray[0].get_obj(), "end").get_int(), 1999);

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

	string guid1 = AssetNew("node1", "dow", newaddres1, "pubdata");
	string guid2 = AssetNew("node1", "cat", newaddres1, "pubdata");
	AssetUpdate("node1", guid1, "pub3");
	UniValue r;
	AssetTransfer("node1", "node2", guid1, newaddres2);
	AssetTransfer("node1", "node3", guid2, newaddres3);

	// xfer an asset that isn't yours
	BOOST_CHECK_NO_THROW(r = CallRPC("node1", "assettransfer " + guid1 + " " + newaddres2 + " ''"));
	UniValue arr = r.get_array();
	BOOST_CHECK_NO_THROW(r = CallRPC("node1", "signrawtransactionwithwallet " + arr[0].get_str()));
	BOOST_CHECK(!find_value(r.get_obj(), "complete").get_bool());
	// update xferred asset
	AssetUpdate("node2", guid1, "public");

	// retransfer asset
	AssetTransfer("node2", "node3", guid1, newaddres3);
}
BOOST_AUTO_TEST_SUITE_END ()
