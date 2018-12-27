// Copyright (c) 2016-2018 The Syscoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "test_syscoin_services.h"
#include "utiltime.h"
#include "util.h"
#include "amount.h"
#include "rpc/server.h"
#include "services/asset.h"
#include "services/assetallocation.h"
#include "wallet/crypter.h"
#include "random.h"
#include "base58.h"
#include "chainparams.h"
#include "core_io.h"
#include <key_io.h>
#include <memory>
#include <string>
#include <boost/algorithm/string.hpp>
#include <boost/filesystem.hpp>
#include <boost/test/unit_test.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/thread.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/algorithm/string/case_conv.hpp> // for to_upper()
#include "services/ranges.h"
#include <bech32.h>
static int node1LastBlock = 0;
static int node2LastBlock = 0;
static int node3LastBlock = 0;
static bool node1Online = false;
static bool node2Online = false;
static bool node3Online = false;
std::map<string, string> mapNodes;
// create a map between node alias names and URLs to be used in testing for example CallRPC("mynode", "getblockchaininfo") would call getblockchaininfo on the node alias mynode which would be pushed as a URL here.
// it is assumed RPC ports are open and u:p is the authentication
void InitNodeURLMap() {
	mapNodes.clear();
	mapNodes["node1"] = "http://127.0.0.1:28379";
	mapNodes["node2"] = "http://127.0.0.1:38379";
	mapNodes["node3"] = "http://127.0.0.1:48379";

}
// lookup the URL based on node passed in
string LookupURL(const string& node) {
	if (mapNodes.find(node) != mapNodes.end())
		return mapNodes[node];
	return "";
}
// SYSCOIN testing setup
void StartNodes()
{
	printf("Stopping any test nodes that are running...\n");
	InitNodeURLMap();
	StopNodes();
	node1LastBlock = 0;
	node2LastBlock = 0;
	node3LastBlock = 0;
	if (boost::filesystem::exists(boost::filesystem::system_complete("node1/wallet.dat")))
		boost::filesystem::remove(boost::filesystem::system_complete("node1//wallet.dat"));
	if (boost::filesystem::exists(boost::filesystem::system_complete("node2/wallet.dat")))
		boost::filesystem::remove(boost::filesystem::system_complete("node2//wallet.dat"));
	if (boost::filesystem::exists(boost::filesystem::system_complete("node3/wallet.dat")))
		boost::filesystem::remove(boost::filesystem::system_complete("node3//wallet.dat"));
	//StopMainNetNodes();
	printf("Starting 3 nodes in a regtest setup...\n");
	StartNode("node1");
    BOOST_CHECK_NO_THROW(CallExtRPC("node1", "generate", "5"));
	StartNode("node2");
	StartNode("node3");
	SelectParams(CBaseChainParams::REGTEST);

}
void StartMainNetNodes()
{
	StopMainNetNodes();
	printf("Starting 2 nodes in mainnet setup...\n");
	StartNode("mainnet1", false);
	StartNode("mainnet2", false);
}
void StopMainNetNodes()
{
	printf("Stopping mainnet1..\n");
	try {
		CallRPC("mainnet1", "stop");
	}
	catch (const runtime_error& error)
	{
	}
	printf("Stopping mainnet2..\n");
	try {
		CallRPC("mainnet2", "stop");
	}
	catch (const runtime_error& error)
	{
	}
	printf("Done!\n");
}
void StopNodes()
{
	StopNode("node1");
	StopNode("node2");
	StopNode("node3");
	printf("Done!\n");
}
void StartNode(const string &dataDir, bool regTest, const string& extraArgs)
{
	if (boost::filesystem::exists(boost::filesystem::system_complete(dataDir + "/wallet.dat")))
	{
		if (!boost::filesystem::exists(boost::filesystem::system_complete(dataDir + "/regtest")))
			boost::filesystem::create_directory(boost::filesystem::system_complete(dataDir + "/regtest"));
		boost::filesystem::copy_file(boost::filesystem::system_complete(dataDir + "/wallet.dat"), boost::filesystem::system_complete(dataDir + "/regtest/wallet.dat"), boost::filesystem::copy_option::overwrite_if_exists);
		boost::filesystem::remove(boost::filesystem::system_complete(dataDir + "/wallet.dat"));
	}
	boost::filesystem::path fpath = boost::filesystem::system_complete("../syscoind");
	string nodePath = fpath.string() + string(" -unittest -assetallocationindex -tpstest -daemon -server  -datadir=") + dataDir;
	if (regTest)
		nodePath += string(" -regtest");
	if (!extraArgs.empty())
		nodePath += string(" ") + extraArgs;

	boost::thread t(runCommand, nodePath);
	printf("Launching %s, waiting 1 second before trying to ping...\n", nodePath.c_str());
	MilliSleep(1000);
	UniValue r;
	while (1)
	{
		try {
			printf("Calling getblockchaininfo!\n");
			r = CallRPC(dataDir, "getblockchaininfo", regTest);
			if (dataDir == "node1")
			{
				if (node1LastBlock > find_value(r.get_obj(), "blocks").get_int())
				{
					printf("Waiting for %s to catch up, current block number %d vs total blocks %d...\n", dataDir.c_str(), find_value(r.get_obj(), "blocks").get_int(), node1LastBlock);
					MilliSleep(500);
					continue;
				}
				node1Online = true;
				node1LastBlock = 0;
			}
			else if (dataDir == "node2")
			{
				if (node2LastBlock > find_value(r.get_obj(), "blocks").get_int())
				{
					printf("Waiting for %s to catch up, current block number %d vs total blocks %d...\n", dataDir.c_str(), find_value(r.get_obj(), "blocks").get_int(), node2LastBlock);
					MilliSleep(500);
					continue;
				}
				node2Online = true;
				node2LastBlock = 0;
			}
			else if (dataDir == "node3")
			{
				if (node3LastBlock > find_value(r.get_obj(), "blocks").get_int())
				{
					printf("Waiting for %s to catch up, current block number %d vs total blocks %d...\n", dataDir.c_str(), find_value(r.get_obj(), "blocks").get_int(), node3LastBlock);
					MilliSleep(500);
					continue;
				}
				node3Online = true;
				node3LastBlock = 0;
			}
			MilliSleep(500);
		}
		catch (const runtime_error& error)
		{
			printf("Waiting for %s to come online, trying again in 1 second...\n", dataDir.c_str());
			MilliSleep(1000);
			continue;
		}
		break;
	}
	printf("Done!\n");
}

void StopNode(const string &dataDir) {
	printf("Stopping %s..\n", dataDir.c_str());
	UniValue r;
	try {
		r = CallRPC(dataDir, "getblockchaininfo");
		if (r.isObject())
		{
			if (dataDir == "node1")
				node1LastBlock = find_value(r.get_obj(), "blocks").get_int();
			else if (dataDir == "node2")
				node2LastBlock = find_value(r.get_obj(), "blocks").get_int();
			else if (dataDir == "node3")
				node3LastBlock = find_value(r.get_obj(), "blocks").get_int();
		}
	}
	catch (const runtime_error& error)
	{
	}
	try {
		CallRPC(dataDir, "stop");
	}
	catch (const runtime_error& error)
	{
	}
	while (1)
	{
		try {
			MilliSleep(1000);
			CallRPC(dataDir, "getblockchaininfo");
		}
		catch (const runtime_error& error)
		{
			break;
		}
	}
	try {
		if (dataDir == "node1")
			node1Online = false;
		else if (dataDir == "node2")
			node2Online = false;
		else if (dataDir == "node3")
			node3Online = false;
	}
	catch (const runtime_error& error)
	{

	}
	MilliSleep(1000);
	if (boost::filesystem::exists(boost::filesystem::system_complete(dataDir + "/regtest/wallet.dat")))
		boost::filesystem::copy_file(boost::filesystem::system_complete(dataDir + "/regtest/wallet.dat"), boost::filesystem::system_complete(dataDir + "/wallet.dat"), boost::filesystem::copy_option::overwrite_if_exists);
	if (boost::filesystem::exists(boost::filesystem::system_complete(dataDir + "/regtest")))
		boost::filesystem::remove_all(boost::filesystem::system_complete(dataDir + "/regtest"));
}
UniValue CallExtRPC(const string &node, const string& command, const string& args, bool readJson)
{
	string url = LookupURL(node);
	BOOST_CHECK(!url.empty());
	UniValue val;
	string curlcmd = "curl -s --user u:p --data-binary '{\"jsonrpc\":\"1.0\",\"id\":\"unittest\",\"method\":\"" + command + "\",\"params\":[" + args + "]}' -H 'content-type:text/plain;' " + url;

    string rawJson = CallExternal(curlcmd);
	val.read(rawJson);
  
    if(!readJson)
        return val;
	if (val.isNull())
		throw runtime_error("Could not parse rpc results");
 
	// try to get error message if exist
	UniValue errorValue = find_value(val.get_obj(), "error");
	if (errorValue.isObject()) {
		throw runtime_error(find_value(errorValue.get_obj(), "message").get_str());
	}

	return find_value(val.get_obj(), "result");
}
UniValue CallRPC(const string &dataDir, const string& commandWithArgs, bool regTest, bool readJson)
{
	UniValue val;
	boost::filesystem::path fpath = boost::filesystem::system_complete("../syscoin-cli");
	string path = fpath.string() + string(" -datadir=") + dataDir;
	if (regTest)
		path += string(" -regtest ");
	else
		path += " ";
	path += commandWithArgs;
	string rawJson = CallExternal(path);
	if (readJson)
	{
		val.read(rawJson);
		if (val.isNull())
			throw runtime_error("Could not parse rpc results");
	}
	else
		val.setStr(rawJson);
	return val;
}
int fsize(FILE *fp) {
	int prev = ftell(fp);
	fseek(fp, 0L, SEEK_END);
	int sz = ftell(fp);
	fseek(fp, prev, SEEK_SET); //go back to where we were
	return sz;
}
void safe_fclose(FILE* file)
{
	if (file)
		BOOST_VERIFY(0 == fclose(file));
	if (boost::filesystem::exists("cmdoutput.log"))
		boost::filesystem::remove("cmdoutput.log");
}
int runSysCommand(const std::string& strCommand)
{
	int nErr = ::system(strCommand.c_str());
	return nErr;
}
std::string CallExternal(std::string &cmd)
{
	cmd += " > cmdoutput.log || true";
	if (runSysCommand(cmd))
		return string("ERROR");
	boost::shared_ptr<FILE> pipe(fopen("cmdoutput.log", "r"), safe_fclose);
	if (!pipe) return "ERROR";
	char buffer[128];
	std::string result = "";
	if (fsize(pipe.get()) > 0)
	{
		while (!feof(pipe.get())) {
			if (fgets(buffer, 128, pipe.get()) != NULL)
				result += buffer;
		}
	}
	return result;
}
void GenerateMainNetBlocks(int nBlocks, const string& node)
{
	int height, targetHeight, newHeight, timeoutCounter;
	UniValue r;
	BOOST_CHECK_NO_THROW(r = CallRPC(node, "getblockchaininfo"));
	targetHeight = find_value(r.get_obj(), "blocks").get_int() + nBlocks;
	newHeight = 0;
	const string &sBlocks = strprintf("%d", nBlocks);
	string otherNode1, otherNode2;
	GetOtherNodes(node, otherNode1, otherNode2);
	while (newHeight < targetHeight)
	{
		BOOST_CHECK_NO_THROW(r = CallRPC(node, "generate " + sBlocks));
		BOOST_CHECK_NO_THROW(r = CallRPC(node, "getblockchaininfo"));
		newHeight = find_value(r.get_obj(), "blocks").get_int();
		BOOST_CHECK_NO_THROW(r = CallRPC(node, "getbalance", true, false));
		CAmount balance = AmountFromValue(r.get_str());
		printf("Current block height %d, Target block height %d, balance %f\n", newHeight, targetHeight, ValueFromAmount(balance).get_real());
	}
	BOOST_CHECK(newHeight >= targetHeight);
	height = 0;
	timeoutCounter = 0;
	MilliSleep(10);
	while (!otherNode1.empty() && height < newHeight)
	{
		try
		{
			r = CallRPC(otherNode1, "getblockchaininfo");
		}
		catch (const runtime_error &e)
		{
			r = NullUniValue;
		}
		if (!r.isObject())
		{
			height = newHeight;
			break;
		}
		height = find_value(r.get_obj(), "blocks").get_int();
		timeoutCounter++;
		if (timeoutCounter > 100) {
			printf("Error: Timeout on getblockchaininfo for %s, height %d vs newHeight %d!\n", otherNode1.c_str(), height, newHeight);
			break;
		}
		MilliSleep(10);
	}
	if (!otherNode1.empty())
		BOOST_CHECK(height >= targetHeight);
}
// generate n Blocks, with up to 10 seconds relay time buffer for other nodes to get the blocks.
// may fail if your network is slow or you try to generate too many blocks such that can't relay within 10 seconds
void GenerateBlocks(int nBlocks, const string& node)
{
	int height, newHeight, timeoutCounter;
	UniValue r;
	string otherNode1, otherNode2;
	GetOtherNodes(node, otherNode1, otherNode2);
	try
	{
		r = CallExtRPC(node, "getblockchaininfo");
	}
	catch (const runtime_error &e)
	{
		return;
	}
	newHeight = find_value(r.get_obj(), "blocks").get_int() + nBlocks;
	const string &sBlocks = strprintf("%d", nBlocks);
	BOOST_CHECK_NO_THROW(r = CallExtRPC(node, "generate" , sBlocks));
	BOOST_CHECK_NO_THROW(r = CallExtRPC(node, "getblockchaininfo"));
	height = find_value(r.get_obj(), "blocks").get_int();
	BOOST_CHECK(height >= newHeight);
	height = 0;
	timeoutCounter = 0;
	MilliSleep(10);
	while (!otherNode1.empty() && height < newHeight)
	{
		try
		{
			r = CallExtRPC(otherNode1, "getblockchaininfo");
		}
		catch (const runtime_error &e)
		{
			r = NullUniValue;
		}
		if (!r.isObject())
		{
			height = newHeight;
			break;
		}
		height = find_value(r.get_obj(), "blocks").get_int();
		timeoutCounter++;
		if (timeoutCounter > 100) {
			printf("Error: Timeout on getblockchaininfo for %s, height %d vs newHeight %d!\n", otherNode1.c_str(), height, newHeight);
			break;
		}
		MilliSleep(10);
	}
	if (!otherNode1.empty())
		BOOST_CHECK(height >= newHeight);
	height = 0;
	timeoutCounter = 0;
	while (!otherNode2.empty() && height < newHeight)
	{
		try
		{
			r = CallExtRPC(otherNode2, "getblockchaininfo");
		}
		catch (const runtime_error &e)
		{
			r = NullUniValue;
		}
		if (!r.isObject())
		{
			height = newHeight;
			break;
		}
		height = find_value(r.get_obj(), "blocks").get_int();
		timeoutCounter++;
		if (timeoutCounter > 100) {
			printf("Error: Timeout on getblockchaininfo for %s, height %d vs newHeight %d!\n", otherNode2.c_str(), height, newHeight);
			break;
		}
		MilliSleep(10);
	}
	if (!otherNode2.empty())
		BOOST_CHECK(height >= newHeight);
	height = 0;
	timeoutCounter = 0;
}
void GenerateSpendableCoins() {
	UniValue r;

	const string &sBlocks = strprintf("%d", 101);
	BOOST_CHECK_NO_THROW(r = CallExtRPC("node1", "generate", sBlocks));
	/*MilliSleep(1000);
	BOOST_CHECK_NO_THROW(r = CallExtRPC("node2", "generate",sBlocks));
	MilliSleep(1000);
	BOOST_CHECK_NO_THROW(r = CallExtRPC("node3", "generate",sBlocks));
	MilliSleep(1000);*/
	BOOST_CHECK_NO_THROW(r = CallExtRPC("node1", "getnewaddress"));

	string newaddress = r.get_str();
	CallExtRPC("node1", "sendtoaddress", "\"" + newaddress + "\",\"100000\"", false);
	GenerateBlocks(10, "node1");
	BOOST_CHECK_NO_THROW(r = CallExtRPC("node2", "getnewaddress"));
	newaddress = r.get_str();
	CallExtRPC("node1", "sendtoaddress", "\"" + newaddress + "\",\"100000\"", false);
	GenerateBlocks(10, "node1");
	BOOST_CHECK_NO_THROW(r = CallExtRPC("node3", "getnewaddress"));
	newaddress = r.get_str();
	CallExtRPC("node1", "sendtoaddress", "\"" + newaddress + "\",\"100000\"", false);
	GenerateBlocks(10, "node1");
}
string GetNewFundedAddress(const string &node) {
	UniValue r;
	BOOST_CHECK_NO_THROW(r = CallExtRPC(node, "getnewaddress"));
	string newaddress = r.get_str();
	string sendnode = "";
	if (node == "node1")
		sendnode = "node2";
	else if (node == "node2")
		sendnode = "node1";
	else if (node == "node3")
		sendnode = "node1";
    CallExtRPC(sendnode, "sendtoaddress", "\"" + newaddress + "\",\"10\"", false);
	GenerateBlocks(10, sendnode);
	GenerateBlocks(10, node);
	return newaddress;
}
void SleepFor(const int& milliseconds, bool actualSleep) {
	if (actualSleep)
		MilliSleep(milliseconds);
	float seconds = milliseconds / 1000;
	BOOST_CHECK(seconds > 0);
	UniValue r;
	try
	{
		r = CallExtRPC("node1", "getblockchaininfo");
		int64_t currentTime = find_value(r.get_obj(), "mediantime").get_int64();
		currentTime += seconds;
		SetSysMocktime(currentTime);
	}
	catch (const runtime_error &e)
	{
	}
	try
	{
		r = CallExtRPC("node2", "getblockchaininfo");
		int64_t currentTime = find_value(r.get_obj(), "mediantime").get_int64();
		currentTime += seconds;
		SetSysMocktime(currentTime);
	}
	catch (const runtime_error &e)
	{
	}
	try
	{
		r = CallExtRPC("node3", "getblockchaininfo");
		int64_t currentTime = find_value(r.get_obj(), "mediantime").get_int64();
		currentTime += seconds;
		SetSysMocktime(currentTime);
	}
	catch (const runtime_error &e)
	{
	}
}
void SetSysMocktime(const int64_t& expiryTime) {
	BOOST_CHECK(expiryTime > 0);
	string expiryStr = strprintf("%lld", expiryTime);
	try
	{
		CallExtRPC("node1", "getblockchaininfo");
		BOOST_CHECK_NO_THROW(CallExtRPC("node1", "setmocktime", "\"" + expiryStr + "\""));
		GenerateBlocks(5, "node1");
		GenerateBlocks(5, "node1");
		GenerateBlocks(5, "node1");
		UniValue r;
		BOOST_CHECK_NO_THROW(r = CallExtRPC("node1", "getblockchaininfo"));
		BOOST_CHECK(expiryTime <= find_value(r.get_obj(), "mediantime").get_int64());
	}
	catch (const runtime_error &e)
	{
	}
	try
	{
		CallExtRPC("node2", "getblockchaininfo");
		BOOST_CHECK_NO_THROW(CallExtRPC("node2", "setmocktime", "\"" + expiryStr + "\""));
		GenerateBlocks(5, "node2");
		GenerateBlocks(5, "node2");
		GenerateBlocks(5, "node2");
		UniValue r;
		BOOST_CHECK_NO_THROW(r = CallExtRPC("node2", "getblockchaininfo"));
		BOOST_CHECK(expiryTime <= find_value(r.get_obj(), "mediantime").get_int64());
	}
	catch (const runtime_error &e)
	{
	}
	try
	{
		CallExtRPC("node3", "getblockchaininfo");
		BOOST_CHECK_NO_THROW(CallExtRPC("node3", "setmocktime", "\"" + expiryStr + "\""));
		GenerateBlocks(5, "node3");
		GenerateBlocks(5, "node3");
		GenerateBlocks(5, "node3");
		UniValue r;
		BOOST_CHECK_NO_THROW(r = CallExtRPC("node3", "getblockchaininfo"));
		BOOST_CHECK(expiryTime <= find_value(r.get_obj(), "mediantime").get_int64());
	}
	catch (const runtime_error &e)
	{
	}
}
void GetOtherNodes(const string& node, string& otherNode1, string& otherNode2)
{
	otherNode1 = "";
	otherNode2 = "";
	if (node == "node1")
	{
		if (node2Online)
			otherNode1 = "node2";
		if (node3Online)
			otherNode2 = "node3";
	}
	else if (node == "node2")
	{
		if (node1Online)
			otherNode1 = "node1";
		if (node3Online)
			otherNode2 = "node3";
	}
	else if (node == "node3")
	{
		if (node1Online)
			otherNode1 = "node1";
		if (node2Online)
			otherNode2 = "node2";
	}

}
void CheckRangeMerge(const string& originalRanges, const string& newRanges, const string& expectedOutputRanges)
{
	vector<string> originalRangeTokens;
	boost::split(originalRangeTokens, originalRanges, boost::is_any_of(" "));
	if (originalRangeTokens.empty())
		originalRangeTokens.push_back(originalRanges);

	vector<CRange> vecRanges;
	for (auto &token : originalRangeTokens) {
		BOOST_CHECK(token.size() > 2);
		token = token.substr(1, token.size() - 2);
		vector<string> ranges;
		boost::split(ranges, token, boost::is_any_of(","));
		BOOST_CHECK_EQUAL(ranges.size(), 2);
		CRange range(boost::lexical_cast<int>(ranges[0]), boost::lexical_cast<int>(ranges[1]));
		vecRanges.push_back(range);
	}


	vector<string> newRangeTokens;
	boost::split(newRangeTokens, newRanges, boost::is_any_of(" "));
	if (newRangeTokens.empty())
		newRangeTokens.push_back(newRanges);
	vector<CRange> vecNewRanges;
	for (auto &token : newRangeTokens) {
		BOOST_CHECK(token.size() > 2);
		token = token.substr(1, token.size() - 2);
		vector<string> ranges;
		boost::split(ranges, token, boost::is_any_of(","));
		BOOST_CHECK_EQUAL(ranges.size(), 2);
		CRange range(boost::lexical_cast<int>(ranges[0]), boost::lexical_cast<int>(ranges[1]));
		vecNewRanges.push_back(range);
	}

	vecRanges.insert(std::end(vecRanges), std::begin(vecNewRanges), std::end(vecNewRanges));

	vector<CRange> mergedRanges;
	mergeRanges(vecRanges, mergedRanges);

	vector<string> expectedOutputRangeTokens;
	boost::split(expectedOutputRangeTokens, expectedOutputRanges, boost::is_any_of(" "));
	if (expectedOutputRangeTokens.empty())
		expectedOutputRangeTokens.push_back(expectedOutputRanges);

	vector<CRange> vecExpectedOutputRanges;
	for (auto &token : expectedOutputRangeTokens) {
		BOOST_CHECK(token.size() > 2);
		token = token.substr(1, token.size() - 2);
		vector<string> ranges;
		boost::split(ranges, token, boost::is_any_of(","));
		BOOST_CHECK_EQUAL(ranges.size(), 2);
		CRange range(boost::lexical_cast<int>(ranges[0]), boost::lexical_cast<int>(ranges[1]));
		vecExpectedOutputRanges.push_back(range);
	}

	BOOST_CHECK_EQUAL(mergedRanges.size(), vecExpectedOutputRanges.size());
	for (size_t i = 0; i < mergedRanges.size(); i++) {
		BOOST_CHECK(mergedRanges[i] == vecExpectedOutputRanges[i]);
	}
}
void CheckRangeSubtract(const string& originalRanges, const string& subtractRange, const string& expectedOutputRanges)
{
	vector<string> originalRangeTokens;
	boost::split(originalRangeTokens, originalRanges, boost::is_any_of(" "));
	if (originalRangeTokens.empty())
		originalRangeTokens.push_back(originalRanges);

	vector<CRange> vecRanges;
	for (auto &token : originalRangeTokens) {
		BOOST_CHECK(token.size() > 2);
		token = token.substr(1, token.size() - 2);
		vector<string> ranges;
		boost::split(ranges, token, boost::is_any_of(","));
		BOOST_CHECK_EQUAL(ranges.size(), 2);
		CRange range(boost::lexical_cast<int>(ranges[0]), boost::lexical_cast<int>(ranges[1]));
		vecRanges.push_back(range);
	}


	vector<string> subtractRangeTokens;
	boost::split(subtractRangeTokens, subtractRange, boost::is_any_of(" "));
	if (subtractRangeTokens.empty())
		subtractRangeTokens.push_back(subtractRange);

	vector<CRange> vecSubtractRanges;
	for (auto &token : subtractRangeTokens) {
		BOOST_CHECK(token.size() > 2);
		token = token.substr(1, token.size() - 2);
		vector<string> ranges;
		boost::split(ranges, token, boost::is_any_of(","));
		BOOST_CHECK_EQUAL(ranges.size(), 2);
		CRange range(boost::lexical_cast<int>(ranges[0]), boost::lexical_cast<int>(ranges[1]));
		vecSubtractRanges.push_back(range);
	}

	vector<CRange> mergedRanges;
	subtractRanges(vecRanges, vecSubtractRanges, mergedRanges);

	vector<string> expectedOutputRangeTokens;
	boost::split(expectedOutputRangeTokens, expectedOutputRanges, boost::is_any_of(" "));
	if (expectedOutputRangeTokens.empty())
		expectedOutputRangeTokens.push_back(expectedOutputRanges);

	vector<CRange> vecExpectedOutputRanges;
	for (auto &token : expectedOutputRangeTokens) {
		BOOST_CHECK(token.size() > 2);
		token = token.substr(1, token.size() - 2);
		vector<string> ranges;
		boost::split(ranges, token, boost::is_any_of(","));
		BOOST_CHECK_EQUAL(ranges.size(), 2);
		CRange range(boost::lexical_cast<int>(ranges[0]), boost::lexical_cast<int>(ranges[1]));
		vecExpectedOutputRanges.push_back(range);
	}

	BOOST_CHECK_EQUAL(mergedRanges.size(), vecExpectedOutputRanges.size());
	for (size_t i = 0; i < mergedRanges.size(); i++) {
		BOOST_CHECK(mergedRanges[i] == vecExpectedOutputRanges[i]);
	}
}
bool DoesRangeContain(const string& parentRange, const string& childRange) {
	vector<string> parentRangeTokens;
	boost::split(parentRangeTokens, parentRange, boost::is_any_of(" "));
	if (parentRangeTokens.empty())
		parentRangeTokens.push_back(parentRange);

	vector<CRange> vecParentRanges;
	for (auto &token : parentRangeTokens) {
		BOOST_CHECK(token.size() > 2);
		token = token.substr(1, token.size() - 2);
		vector<string> ranges;
		boost::split(ranges, token, boost::is_any_of(","));
		BOOST_CHECK_EQUAL(ranges.size(), 2);
		CRange range(boost::lexical_cast<int>(ranges[0]), boost::lexical_cast<int>(ranges[1]));
		vecParentRanges.push_back(range);
	}

	vector<string> childRangeTokens;
	boost::split(childRangeTokens, childRange, boost::is_any_of(" "));
	if (childRangeTokens.empty())
		childRangeTokens.push_back(childRange);

	vector<CRange> vecChildRanges;
	for (auto &token : childRangeTokens) {
		BOOST_CHECK(token.size() > 2);
		token = token.substr(1, token.size() - 2);
		vector<string> ranges;
		boost::split(ranges, token, boost::is_any_of(","));
		BOOST_CHECK_EQUAL(ranges.size(), 2);
		CRange range(boost::lexical_cast<int>(ranges[0]), boost::lexical_cast<int>(ranges[1]));
		vecChildRanges.push_back(range);
	}

	return doesRangeContain(vecParentRanges, vecChildRanges);
}
string AssetNew(const string& node, const string& name, const string& address, const string& pubdata, const string& contract, const string& precision, const string& useinputranges, const string& supply, const string& maxsupply, const string& interestrate, const string& updateflags, const string& witness)
{
	string otherNode1, otherNode2;
	GetOtherNodes(node, otherNode1, otherNode2);
	UniValue r;
    
	// "assetnew [name] [address] [public] [contract] [precision=8] [use_inputranges] [supply] [max_supply] [interest_rate] [update_flags] [witness]\n"
	BOOST_CHECK_NO_THROW(r = CallRPC(node, "assetnew " + name + " " + address + " " + pubdata + " " + contract + " " + precision + " " + useinputranges + " " + supply + " " + maxsupply + " " + interestrate + " " + updateflags + " " + witness));
	UniValue arr = r.get_array();
    string guid = arr[1].get_str();
    BOOST_CHECK_NO_THROW(r = CallRPC("node1", "syscointxfund " + arr[0].get_str() + " " + address));
    arr = r.get_array();
	BOOST_CHECK_NO_THROW(r = CallRPC(node, "signrawtransactionwithwallet " + arr[0].get_str()));
	string hex_str = find_value(r.get_obj(), "hex").get_str();
	BOOST_CHECK_NO_THROW(r = CallRPC(node, "sendrawtransaction " + hex_str, true, false));
	BOOST_CHECK_NO_THROW(r = CallRPC(node, "decoderawtransaction " + hex_str + " true"));
	
	GenerateBlocks(5, node);
	BOOST_CHECK_NO_THROW(r = CallRPC(node, "assetinfo " + guid + " false"));
	int nprecision = atoi(precision);
	bool binputrange = useinputranges == "true" ? true : false;
	string nameupper = name;
	boost::algorithm::to_upper(nameupper);
	BOOST_CHECK(find_value(r.get_obj(), "_id").get_str() == guid);
	BOOST_CHECK(find_value(r.get_obj(), "symbol").get_str() == nameupper);
	BOOST_CHECK(find_value(r.get_obj(), "owner").get_str() == address);
	BOOST_CHECK(find_value(r.get_obj(), "publicvalue").get_str() == pubdata);
	UniValue balance = find_value(r.get_obj(), "balance");
	UniValue totalsupply = find_value(r.get_obj(), "total_supply");
	UniValue maxsupplyu = find_value(r.get_obj(), "max_supply");
	UniValue supplytmp(UniValue::VSTR);
	supplytmp.setStr(supply);
	UniValue maxsupplytmp(UniValue::VSTR);
	maxsupplytmp.setStr(maxsupply);
	BOOST_CHECK(AssetAmountFromValue(balance, nprecision, binputrange) == AssetAmountFromValue(supplytmp, nprecision, binputrange));
	BOOST_CHECK(AssetAmountFromValue(totalsupply, nprecision, binputrange) == AssetAmountFromValue(supplytmp, nprecision, binputrange));
	BOOST_CHECK_EQUAL(AssetAmountFromValue(maxsupplyu, nprecision, binputrange), AssetAmountFromValue(maxsupplytmp, nprecision, binputrange));
	BOOST_CHECK_EQUAL(((int)(find_value(r.get_obj(), "interest_rate").get_real() * 1000 + 0.5)), ((int)(boost::lexical_cast<float>(interestrate) * 1000)));
	int update_flags = find_value(r.get_obj(), "update_flags").get_int();
	int paramUpdateFlags = atoi(updateflags);
	BOOST_CHECK(update_flags == paramUpdateFlags);


	GenerateBlocks(5, node);
	if (!otherNode1.empty())
	{
		BOOST_CHECK_NO_THROW(r = CallRPC(otherNode1, "assetinfo " + guid + " false"));
		BOOST_CHECK(find_value(r.get_obj(), "_id").get_str() == guid);
		BOOST_CHECK(find_value(r.get_obj(), "symbol").get_str() == nameupper);
		BOOST_CHECK(find_value(r.get_obj(), "publicvalue").get_str() == pubdata);
		UniValue balance = find_value(r.get_obj(), "balance");
		UniValue totalsupply = find_value(r.get_obj(), "total_supply");
		UniValue maxsupplyu = find_value(r.get_obj(), "max_supply");
		BOOST_CHECK(AssetAmountFromValue(balance, nprecision, binputrange) == AssetAmountFromValue(supplytmp, nprecision, binputrange));
		BOOST_CHECK(AssetAmountFromValue(totalsupply, nprecision, binputrange) == AssetAmountFromValue(supplytmp, nprecision, binputrange));
		BOOST_CHECK_EQUAL(AssetAmountFromValue(maxsupplyu, nprecision, binputrange), AssetAmountFromValue(maxsupplytmp, nprecision, binputrange));
		BOOST_CHECK_EQUAL(((int)(find_value(r.get_obj(), "interest_rate").get_real() * 1000 + 0.5)), ((int)(boost::lexical_cast<float>(interestrate) * 1000)));
        int update_flags = find_value(r.get_obj(), "update_flags").get_int();
        int paramUpdateFlags = atoi(updateflags);
        BOOST_CHECK(update_flags == paramUpdateFlags);
	}
	if (!otherNode2.empty())
	{
		BOOST_CHECK_NO_THROW(r = CallRPC(otherNode2, "assetinfo " + guid + " false"));
		BOOST_CHECK(find_value(r.get_obj(), "_id").get_str() == guid);
		BOOST_CHECK(find_value(r.get_obj(), "symbol").get_str() == nameupper);
		BOOST_CHECK(find_value(r.get_obj(), "publicvalue").get_str() == pubdata);
		UniValue balance = find_value(r.get_obj(), "balance");
		UniValue totalsupply = find_value(r.get_obj(), "total_supply");
		UniValue maxsupplyu = find_value(r.get_obj(), "max_supply");
		BOOST_CHECK(AssetAmountFromValue(balance, nprecision, binputrange) == AssetAmountFromValue(supplytmp, nprecision, binputrange));
		BOOST_CHECK(AssetAmountFromValue(totalsupply, nprecision, binputrange) == AssetAmountFromValue(supplytmp, nprecision, binputrange));
		BOOST_CHECK_EQUAL(AssetAmountFromValue(maxsupplyu, nprecision, binputrange), AssetAmountFromValue(maxsupplytmp, nprecision, binputrange));
		BOOST_CHECK_EQUAL(((int)(find_value(r.get_obj(), "interest_rate").get_real() * 1000 + 0.5)), ((int)(boost::lexical_cast<float>(interestrate) * 1000)));
        int update_flags = find_value(r.get_obj(), "update_flags").get_int();
        int paramUpdateFlags = atoi(updateflags);
        BOOST_CHECK(update_flags == paramUpdateFlags);
	}
	return guid;
}
void AssetUpdate(const string& node, const string& name, const string& pubdata, const string& supply, const string& interest, const string& blacklist, const string& updateflags, const string& witness)
{
	string otherNode1, otherNode2;
	GetOtherNodes(node, otherNode1, otherNode2);
	UniValue r;
	BOOST_CHECK_NO_THROW(r = CallRPC(node, "assetinfo " + name + " false"));
	string oldaddress = find_value(r.get_obj(), "owner").get_str();
	string oldpubdata = find_value(r.get_obj(), "publicvalue").get_str();
	string oldsupply = find_value(r.get_obj(), "total_supply").get_str();
	string oldsymbol = find_value(r.get_obj(), "symbol").get_str();
	bool binputranges = find_value(r.get_obj(), "use_input_ranges").get_bool();
	int nprecision = find_value(r.get_obj(), "precision").get_int();
	UniValue totalsupply = find_value(r.get_obj(), "total_supply");

	CAmount oldsupplyamount = AssetAmountFromValue(totalsupply, nprecision, binputranges);
	CAmount supplyamount = 0;
	if (supply != "''") {
		UniValue supplytmp(UniValue::VSTR);
		supplytmp.setStr(supply);
		supplyamount = AssetAmountFromValue(supplytmp, nprecision, binputranges);
	}
	CAmount newamount = oldsupplyamount + supplyamount;

	string oldinterest = boost::lexical_cast<string>(find_value(r.get_obj(), "interest_rate").get_real());

	string newpubdata = pubdata == "''" ? oldpubdata : pubdata;
	string newsupply = supply == "''" ? "0" : supply;
	string newinterest = interest == "''" ? oldinterest : interest;

	// "assetupdate [asset] [public] [contract] [category=assets] [supply] [interest_rate] [blacklist] [update_flags] [witness]\n"
	BOOST_CHECK_NO_THROW(r = CallRPC(node, "assetupdate " + name + " " + newpubdata + " '' " +  newsupply + " " + newinterest + " " + blacklist + " " + updateflags + " " +witness));
	// increase supply to new amount if we passed in a supply value
	newsupply = supply == "''" ? oldsupply : ValueFromAssetAmount(newamount, nprecision, binputranges).write();
    UniValue arr = r.get_array();
    BOOST_CHECK_NO_THROW(r = CallRPC("node1", "syscointxfund " + arr[0].get_str() + " " + oldaddress));
    arr = r.get_array();
	BOOST_CHECK_NO_THROW(r = CallRPC(node, "signrawtransactionwithwallet " + arr[0].get_str()));
	string hex_str = find_value(r.get_obj(), "hex").get_str();
	BOOST_CHECK_NO_THROW(r = CallRPC(node, "sendrawtransaction " + hex_str, true, false));
	BOOST_CHECK_NO_THROW(r = CallRPC(node, "decoderawtransaction " + hex_str + " true"));

	GenerateBlocks(5, node);
	BOOST_CHECK_NO_THROW(r = CallRPC(node, "assetinfo " + name + " false"));

	BOOST_CHECK(find_value(r.get_obj(), "_id").get_str() == name);
	BOOST_CHECK_EQUAL(find_value(r.get_obj(), "symbol").get_str(), oldsymbol);
	BOOST_CHECK(find_value(r.get_obj(), "owner").get_str() == oldaddress);
	BOOST_CHECK_EQUAL(((int)(find_value(r.get_obj(), "interest_rate").get_real() * 1000 + 0.5)), ((int)(boost::lexical_cast<float>(newinterest) * 1000)));
	totalsupply = find_value(r.get_obj(), "total_supply");
	BOOST_CHECK(AssetAmountFromValue(totalsupply, nprecision, binputranges) == newamount);
	GenerateBlocks(5, node);
	if (!otherNode1.empty())
	{
		BOOST_CHECK_NO_THROW(r = CallRPC(otherNode1, "assetinfo " + name + " false"));
		BOOST_CHECK(find_value(r.get_obj(), "_id").get_str() == name);
		BOOST_CHECK_EQUAL(find_value(r.get_obj(), "symbol").get_str(), oldsymbol);
		BOOST_CHECK(find_value(r.get_obj(), "owner").get_str() == oldaddress);
		BOOST_CHECK_EQUAL(find_value(r.get_obj(), "publicvalue").get_str(), newpubdata);
		BOOST_CHECK_EQUAL(((int)(find_value(r.get_obj(), "interest_rate").get_real() * 1000 + 0.5)), ((int)(boost::lexical_cast<float>(newinterest) * 1000)));
		totalsupply = find_value(r.get_obj(), "total_supply");
		BOOST_CHECK(AssetAmountFromValue(totalsupply, nprecision, binputranges) == newamount);

	}
	if (!otherNode2.empty())
	{
		BOOST_CHECK_NO_THROW(r = CallRPC(otherNode2, "assetinfo " + name + " false"));
		BOOST_CHECK(find_value(r.get_obj(), "_id").get_str() == name);
		BOOST_CHECK_EQUAL(find_value(r.get_obj(), "symbol").get_str(), oldsymbol);
		BOOST_CHECK(find_value(r.get_obj(), "owner").get_str() == oldaddress);
		BOOST_CHECK_EQUAL(find_value(r.get_obj(), "publicvalue").get_str(), newpubdata);
		BOOST_CHECK_EQUAL(((int)(find_value(r.get_obj(), "interest_rate").get_real() * 1000 + 0.5)), ((int)(boost::lexical_cast<float>(newinterest) * 1000)));
		totalsupply = find_value(r.get_obj(), "total_supply");
		BOOST_CHECK(AssetAmountFromValue(totalsupply, nprecision, binputranges) == newamount);

	}

}
void AssetTransfer(const string& node, const string &tonode, const string& name, const string& toaddress, const string& witness)
{
	UniValue r;

	BOOST_CHECK_NO_THROW(r = CallRPC(node, "assetinfo " + name + " false"));
	string oldaddress = find_value(r.get_obj(), "owner").get_str();
	string oldsymbol = find_value(r.get_obj(), "symbol").get_str();


	// "assettransfer [asset] [address] [witness]\n"
	BOOST_CHECK_NO_THROW(r = CallRPC(node, "assettransfer " + name + " " + toaddress + " " + witness));
    UniValue arr = r.get_array();
    BOOST_CHECK_NO_THROW(r = CallRPC("node1", "syscointxfund " + arr[0].get_str() + " " + oldaddress));
    arr = r.get_array();
	BOOST_CHECK_NO_THROW(r = CallRPC(node, "signrawtransactionwithwallet " + arr[0].get_str()));
	string hex_str = find_value(r.get_obj(), "hex").get_str();
	BOOST_CHECK_NO_THROW(r = CallRPC(node, "sendrawtransaction " + hex_str, true, false));
	BOOST_CHECK_NO_THROW(r = CallRPC(node, "decoderawtransaction " + hex_str + " true"));

	GenerateBlocks(5, node);
	BOOST_CHECK_NO_THROW(r = CallRPC(node, "assetinfo " + name + " false"));


	BOOST_CHECK(find_value(r.get_obj(), "_id").get_str() == name);
	BOOST_CHECK(find_value(r.get_obj(), "symbol").get_str() == oldsymbol);
	GenerateBlocks(5, node);

	BOOST_CHECK_NO_THROW(r = CallRPC(tonode, "assetinfo " + name + " false"));
	BOOST_CHECK(find_value(r.get_obj(), "owner").get_str() == toaddress);


}
void AssetClaimInterest(const string& node, const string& name, const string& address, const string& witness) {
	string otherNode1, otherNode2;
	GetOtherNodes(node, otherNode1, otherNode2);
	UniValue r;
	BOOST_CHECK_NO_THROW(r = CallRPC(node, "assetallocationcollectinterest " + name + " " + address + " " + witness));
    UniValue arr = r.get_array();
    BOOST_CHECK_NO_THROW(r = CallRPC("node1", "syscointxfund " + arr[0].get_str() + " " + address));
    arr = r.get_array();
	BOOST_CHECK_NO_THROW(r = CallRPC(node, "signrawtransactionwithwallet " + arr[0].get_str()));
	string hex_str = find_value(r.get_obj(), "hex").get_str();
	BOOST_CHECK_NO_THROW(r = CallRPC(node, "sendrawtransaction " + hex_str, true, false));
	BOOST_CHECK_NO_THROW(r = CallRPC(node, "decoderawtransaction " + hex_str + " true"));
	GenerateBlocks(1);

}
string AssetAllocationTransfer(const bool usezdag, const string& node, const string& name, const string& fromaddress, const string& inputs, const string& memo, const string& witness) {

	UniValue r;
	BOOST_CHECK_NO_THROW(r = CallRPC(node, "assetinfo " + name + " false"));
	bool binputranges = find_value(r.get_obj(), "use_input_ranges").get_bool();
	int nprecision = find_value(r.get_obj(), "precision").get_int();

	CAssetAllocation theAssetAllocation;
	UniValue valueTo;
	string inputsTmp = inputs;
	boost::replace_all(inputsTmp, "\\\"", "\"");
	boost::replace_all(inputsTmp, "\"[", "[");
	boost::replace_all(inputsTmp, "]\"", "]");
	BOOST_CHECK(valueTo.read(inputsTmp));
	BOOST_CHECK(valueTo.isArray());
	UniValue receivers = valueTo.get_array();
	CAmount inputamount = 0;
	BOOST_CHECK(receivers.size() > 0);
	for (unsigned int idx = 0; idx < receivers.size(); idx++) {
		const UniValue& receiver = receivers[idx];
		BOOST_CHECK(receiver.isObject());


		UniValue receiverObj = receiver.get_obj();
		vector<uint8_t> vchAddressTo = bech32::Decode(find_value(receiverObj, "ownerto").get_str()).second;

		UniValue inputRangeObj = find_value(receiverObj, "ranges");
		UniValue amountObj = find_value(receiverObj, "amount");
		if (inputRangeObj.isArray()) {
			UniValue inputRanges = inputRangeObj.get_array();
			vector<CRange> vectorOfRanges;
			for (unsigned int rangeIndex = 0; rangeIndex < inputRanges.size(); rangeIndex++) {
				const UniValue& inputRangeObj = inputRanges[rangeIndex];
				BOOST_CHECK(inputRangeObj.isObject());
				UniValue startRangeObj = find_value(inputRangeObj, "start");
				UniValue endRangeObj = find_value(inputRangeObj, "end");
				BOOST_CHECK(startRangeObj.isNum());
				BOOST_CHECK(endRangeObj.isNum());
				vectorOfRanges.push_back(CRange(startRangeObj.get_int(), endRangeObj.get_int()));
			}
			const unsigned int rangeTotal = validateRangesAndGetCount(vectorOfRanges);
			BOOST_CHECK(rangeTotal > 0);
			inputamount += rangeTotal;
			theAssetAllocation.listSendingAllocationInputs.push_back(make_pair(vchAddressTo, vectorOfRanges));
		}
		else if (amountObj.isNum()) {
			const CAmount &amount = AssetAmountFromValue(amountObj, nprecision, binputranges);
			inputamount += amount;
			BOOST_CHECK(amount > 0);
			theAssetAllocation.listSendingAllocationAmounts.push_back(make_pair(vchAddressTo, amount));
		}


	}

	string otherNode1, otherNode2;
	GetOtherNodes(node, otherNode1, otherNode2);

	BOOST_CHECK_NO_THROW(r = CallRPC(node, "assetallocationinfo " + name + " " + fromaddress + " false"));
	UniValue balance = find_value(r.get_obj(), "balance");
	CAmount newfromamount = AssetAmountFromValue(balance, nprecision, binputranges) - inputamount;

	// "assetallocationsend [asset] [ownerfrom] ( [{\"ownerto\":\"address\",\"amount\":amount},...] or [{\"ownerto\":\"address\",\"ranges\":[{\"start\":index,\"end\":index},...]},...] ) [memo] [witness]\n"
	BOOST_CHECK_NO_THROW(r = CallRPC(node, "assetallocationsend " + name + " " + fromaddress + " " + inputs + " " + memo + " " + witness));
    UniValue arr = r.get_array();
    BOOST_CHECK_NO_THROW(r = CallRPC("node1", "syscointxfund " + arr[0].get_str() + " " + fromaddress));
    arr = r.get_array();
	BOOST_CHECK_NO_THROW(r = CallRPC(node, "signrawtransactionwithwallet " + arr[0].get_str()));
	string hex_str = find_value(r.get_obj(), "hex").get_str();


	BOOST_CHECK_NO_THROW(r = CallRPC(node, "sendrawtransaction " + hex_str, true, false));
	BOOST_CHECK_NO_THROW(r = CallRPC(node, "syscoindecoderawtransaction " + hex_str));
	BOOST_CHECK_EQUAL(find_value(r.get_obj(), "txtype").get_str(), "assetallocationsend");
	if (!theAssetAllocation.listSendingAllocationAmounts.empty())
		BOOST_CHECK_EQUAL(find_value(r.get_obj(), "allocations").get_array().size(), theAssetAllocation.listSendingAllocationAmounts.size());
	else if (!theAssetAllocation.listSendingAllocationInputs.empty())
		BOOST_CHECK_EQUAL(find_value(r.get_obj(), "allocations").get_array().size(), theAssetAllocation.listSendingAllocationInputs.size());

	BOOST_CHECK_NO_THROW(r = CallRPC(node, "decoderawtransaction " + hex_str + " true"));
	string txid = find_value(r.get_obj(), "txid").get_str();
	if (usezdag) {
		MilliSleep(100);
	}
	else {
		GenerateBlocks(1, node);
		BOOST_CHECK_NO_THROW(r = CallRPC(node, "listassetallocationtransactions"));
		BOOST_CHECK(r.isArray());
		UniValue assetTxArray = r.get_array();
		UniValue firstAssetTx = assetTxArray[0].get_obj();
		BOOST_CHECK_EQUAL(find_value(firstAssetTx, "txid").get_str(), txid);
		BOOST_CHECK_EQUAL(find_value(firstAssetTx, "confirmed").get_bool(), true);
	}
	BOOST_CHECK_NO_THROW(r = CallRPC(node, "assetallocationinfo " + name + " " + fromaddress + " false"));
	balance = find_value(r.get_obj(), "balance");
	if (newfromamount > 0)
		BOOST_CHECK_EQUAL(AssetAmountFromValue(balance, nprecision, binputranges), newfromamount);
	return txid;
}
void BurnAssetAllocation(const string& node, const string &guid, const string &address,const string &amount, bool confirm){
    UniValue r;
    try{
        r = CallRPC(node, "assetallocationinfo " + guid + " burn false");
    }
    catch(runtime_error&err){
    }
    CAmount beforeBalance = 0;
    if(r.isObject()){
        UniValue balanceB = find_value(r.get_obj(), "balance");
        beforeBalance = AssetAmountFromValue(balanceB, 8, false);
    }
    
	BOOST_CHECK_NO_THROW(r = CallRPC(node, "assetallocationburn " + guid + " " + address + " " + amount));
    UniValue arr = r.get_array();
    BOOST_CHECK_NO_THROW(r = CallRPC("node1", "syscointxfund " + arr[0].get_str() + " " + address));
    arr = r.get_array();
	BOOST_CHECK_NO_THROW(r = CallRPC(node, "signrawtransactionwithwallet " + arr[0].get_str()));
	string hexStr = find_value(r.get_obj(), "hex").get_str();
	BOOST_CHECK_NO_THROW(r = CallRPC(node, "sendrawtransaction " + hexStr, true, false));
    if(confirm){
    	GenerateBlocks(5, "node1");
    	BOOST_CHECK_NO_THROW(r = CallRPC(node, "assetallocationinfo " + guid + " burn false"));
    	UniValue balance = find_value(r.get_obj(), "balance");
    	BOOST_CHECK_EQUAL(AssetAmountFromValue(balance, 8, false), beforeBalance+(0.5 * COIN));
    }
}
string AssetSend(const string& node, const string& name, const string& inputs, const string& memo, const string& witness, bool completetx)
{
	UniValue r;
	BOOST_CHECK_NO_THROW(r = CallRPC(node, "assetinfo " + name + " false"));
	bool binputranges = find_value(r.get_obj(), "use_input_ranges").get_bool();
	int nprecision = find_value(r.get_obj(), "precision").get_int();
    string fromAddress = find_value(r.get_obj(), "owner").get_str();
    
	CAssetAllocation theAssetAllocation;
	UniValue valueTo;
	string inputsTmp = inputs;
	boost::replace_all(inputsTmp, "\\\"", "\"");
	boost::replace_all(inputsTmp, "\"[", "[");
	boost::replace_all(inputsTmp, "]\"", "]");
	BOOST_CHECK(valueTo.read(inputsTmp));
	BOOST_CHECK(valueTo.isArray());
	UniValue receivers = valueTo.get_array();
	CAmount inputamount = 0;
	for (unsigned int idx = 0; idx < receivers.size(); idx++) {
		const UniValue& receiver = receivers[idx];
		BOOST_CHECK(receiver.isObject());


		UniValue receiverObj = receiver.get_obj();
		vector<uint8_t> vchAddressTo = bech32::Decode(find_value(receiverObj, "ownerto").get_str()).second;

		UniValue inputRangeObj = find_value(receiverObj, "ranges");
		UniValue amountObj = find_value(receiverObj, "amount");
		if (inputRangeObj.isArray()) {
			UniValue inputRanges = inputRangeObj.get_array();
			vector<CRange> vectorOfRanges;
			for (unsigned int rangeIndex = 0; rangeIndex < inputRanges.size(); rangeIndex++) {
				const UniValue& inputRangeObj = inputRanges[rangeIndex];
				BOOST_CHECK(inputRangeObj.isObject());
				UniValue startRangeObj = find_value(inputRangeObj, "start");
				UniValue endRangeObj = find_value(inputRangeObj, "end");
				BOOST_CHECK(startRangeObj.isNum());
				BOOST_CHECK(endRangeObj.isNum());
				vectorOfRanges.push_back(CRange(startRangeObj.get_int(), endRangeObj.get_int()));
			}
			const unsigned int rangeTotal = validateRangesAndGetCount(vectorOfRanges);
			BOOST_CHECK(rangeTotal > 0);
			inputamount += rangeTotal;
			theAssetAllocation.listSendingAllocationInputs.push_back(make_pair(vchAddressTo, vectorOfRanges));
		}
		else if (amountObj.isNum()) {
			const CAmount &amount = AssetAmountFromValue(amountObj, nprecision, binputranges);
			inputamount += amount;
			BOOST_CHECK(amount > 0);
			theAssetAllocation.listSendingAllocationAmounts.push_back(make_pair(vchAddressTo, amount));
		}


	}

	string otherNode1, otherNode2;
	GetOtherNodes(node, otherNode1, otherNode2);
	BOOST_CHECK_NO_THROW(r = CallRPC(node, "assetinfo " + name + " false"));
	string fromsupply = find_value(r.get_obj(), "total_supply").get_str();
	UniValue balance = find_value(r.get_obj(), "balance");
	CAmount newfromamount = AssetAmountFromValue(balance, nprecision, binputranges) - inputamount;

	// "assetsend [asset] ( [{\"ownerto\":\"address\",\"amount\":amount},...] or [{\"ownerto\":\"address\",\"ranges\":[{\"start\":index,\"end\":index},...]},...] ) [memo] [witness]\n"
	BOOST_CHECK_NO_THROW(r = CallRPC(node, "assetsend " + name + " " + inputs + " " + memo + " " + witness));
    UniValue arr = r.get_array();
    BOOST_CHECK_NO_THROW(r = CallRPC("node1", "syscointxfund " + arr[0].get_str() + " " + fromAddress));
    arr = r.get_array();
	BOOST_CHECK_NO_THROW(r = CallRPC(node, "signrawtransactionwithwallet " + arr[0].get_str()));
	string hex_str = find_value(r.get_obj(), "hex").get_str();
	if (completetx) {
		BOOST_CHECK_NO_THROW(r = CallRPC(node, "sendrawtransaction " + hex_str, true, false));
		BOOST_CHECK_NO_THROW(r = CallRPC(node, "decoderawtransaction " + hex_str + " true"));

		GenerateBlocks(1, node);
		BOOST_CHECK_NO_THROW(r = CallRPC(node, "assetinfo " + name + " false"));

		BOOST_CHECK_EQUAL(find_value(r.get_obj(), "total_supply").get_str(), fromsupply);
		balance = find_value(r.get_obj(), "balance");
		if (newfromamount > 0)
			BOOST_CHECK_EQUAL(AssetAmountFromValue(balance, nprecision, binputranges), newfromamount);

		if (!otherNode1.empty())
		{
			BOOST_CHECK_NO_THROW(r = CallRPC(otherNode1, "assetinfo " + name + " false"));
			BOOST_CHECK_EQUAL(find_value(r.get_obj(), "total_supply").get_str(), fromsupply);
			balance = find_value(r.get_obj(), "balance");
			if (newfromamount > 0)
				BOOST_CHECK_EQUAL(AssetAmountFromValue(balance, nprecision, binputranges), newfromamount);

		}
		if (!otherNode2.empty())
		{
			BOOST_CHECK_NO_THROW(r = CallRPC(otherNode2, "assetinfo " + name + " false"));
			BOOST_CHECK_EQUAL(find_value(r.get_obj(), "total_supply").get_str(), fromsupply);
			balance = find_value(r.get_obj(), "balance");
			if (newfromamount > 0)
				BOOST_CHECK_EQUAL(AssetAmountFromValue(balance, nprecision, binputranges), newfromamount);

		}
	}
	BOOST_CHECK_NO_THROW(r = CallRPC(node, "syscoindecoderawtransaction " + hex_str));
	BOOST_CHECK_EQUAL(find_value(r.get_obj(), "txtype").get_str(), "assetsend");
	if (!theAssetAllocation.listSendingAllocationAmounts.empty())
		BOOST_CHECK_EQUAL(find_value(r.get_obj(), "allocations").get_array().size(), theAssetAllocation.listSendingAllocationAmounts.size());
	else if (!theAssetAllocation.listSendingAllocationInputs.empty())
		BOOST_CHECK_EQUAL(find_value(r.get_obj(), "allocations").get_array().size(), theAssetAllocation.listSendingAllocationInputs.size());
	return hex_str;

}

BasicSyscoinTestingSetup::BasicSyscoinTestingSetup()
{
}
BasicSyscoinTestingSetup::~BasicSyscoinTestingSetup()
{
}
SyscoinTestingSetup::SyscoinTestingSetup()
{
}
SyscoinTestingSetup::~SyscoinTestingSetup()
{
}
SyscoinMainNetSetup::SyscoinMainNetSetup()
{
	//StartMainNetNodes();
}
SyscoinMainNetSetup::~SyscoinMainNetSetup()
{
	//StopMainNetNodes();
}
