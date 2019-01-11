// Copyright (c) 2017 The Dogecoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "ethereum.h"
#include "SHA3.h"

using namespace dev;


int nibblesToTraverse(const std::string &encodedPartialPath, const std::string &path, int pathPtr) {
  std::string partialPath;
  char pathPtrInt[2] = {encodedPartialPath[0], '\0'};
  int partialPathInt = strtol(pathPtrInt, NULL, 10);
  if(partialPathInt == 0 || partialPathInt == 2){
    partialPath = encodedPartialPath.substr(2);
  }else{
    partialPath = encodedPartialPath.substr(1);
  }
  if(partialPath == path.substr(pathPtr, partialPath.size())){
    return partialPath.size();
  }else{
    return -1;
  }
}
bool VerifyProof(bytesConstRef path, const RLP& value, const RLP& parentNodes, const RLP& root) {
    try{
        dev::RLP currentNode;
        const int len = parentNodes.itemCount();
        dev::RLP nodeKey = root;       
        int pathPtr = 0;
        printf("length %d\n", len);
    	const std::string pathString = toHex(path);
        printf("pathString %s\n", pathString.c_str());
        int nibbles;
        char pathPtrInt[2];
        for (int i = 0 ; i < len ; i++) {
          currentNode = parentNodes[i];
          if(!nodeKey.payload().contentsEqual(sha3(currentNode.data()).ref().toVector())){
            return false;
          } 
          printf("pathPtr %d pathString.size() %d\n", pathPtr, pathString.size());
          if(pathPtr > (int)pathString.size()){
            return false;
          }
          printf("currentNode.itemCount() %d\n", currentNode.itemCount());
          switch(currentNode.itemCount()){
            case 17://branch node
              if(pathPtr == (int)pathString.size()){
                if(currentNode[16].payload().contentsEqual(value.data().toVector())){
                    printf("17 equals ret true\n");
                  return true;
                }else{
                  return false;
                }
              }
              printf("pathPtr %d pathString[pathPtr] %c\n", pathPtr, pathString[pathPtr]);
              pathPtrInt[0] = pathString[pathPtr];
              pathPtrInt[1] = '\0';
              printf("strtol(pathPtrInt, NULL, 16) %d \n", strtol(pathPtrInt, NULL, 16));
              nodeKey = currentNode[strtol(pathPtrInt, NULL, 16)]; //must == sha3(rlp.encode(currentNode[path[pathptr]]))
              pathPtr += 1;
              break;
            case 2:
              nibbles = nibblesToTraverse(toHex(currentNode[0].payload()), pathString, pathPtr);
              printf("nibbles %d\n", nibbles);
              if(nibbles <= -1)
                return false;
              pathPtr += nibbles;
              printf("pathPtr %d pathString.size() %d\n", pathPtr, pathString.size());
              if(pathPtr == (int)pathString.size()) { //leaf node
                if(currentNode[1].payload().contentsEqual(value.data().toVector())){
                    printf("2 equals ret true\n");
                  return true;
                } else {
                  return false;
                }
              } else {//extension node
                nodeKey = currentNode[1];
              }
              break;
            default:
              return false;
          }
        }
    }
    catch(...){
        return false;
    }
  return false;
}

/**
 * Parse eth input string expected to contain smart contract method call data. If the method call is not what we
 * expected, or the length of the expected string is not what we expect then return false.
 *
 * @param vchInputExpectedMethodHash The expected method hash
 * @param vchInputData The input to parse
 * @param outputAmount The amount burned
 * @param nAsset The asset burned or 0 for SYS 
 * @return true if everything is valid
 */
bool parseEthMethodInputData(const std::vector<unsigned char>& vchInputExpectedMethodHash, const std::vector<unsigned char>& vchInputData, CAmount& outputAmount, uint32_t& nAsset) {
    // input not 40 bytes is bad
    if(vchInputData.size() != 40) 
        return false;

    // method hash is 4 bytes
    std::vector<unsigned char>::const_iterator first = vchInputData.begin();
    std::vector<unsigned char>::const_iterator last = first + 4;
    const std::vector<unsigned char> vchMethodHash(first,last);
    // if the method hash doesn't match the expected method hash then return false
    if(vchMethodHash != vchInputExpectedMethodHash) 
        return false;

    // get the first parameter and convert to CAmount and assign to output var
    // convert the vch into a int64_t (CAmount)
    // should be in position 36 walking backwards
    uint64_t result = static_cast<uint64_t>(vchInputData[35]);
    result |= static_cast<uint64_t>(vchInputData[34]) << 8;
    result |= static_cast<uint64_t>(vchInputData[33]) << 16;
    result |= static_cast<uint64_t>(vchInputData[32]) << 24;
    result |= static_cast<uint64_t>(vchInputData[31]) << 32;
    result |= static_cast<uint64_t>(vchInputData[30]) << 40;
    result |= static_cast<uint64_t>(vchInputData[29]) << 48;
    result |= static_cast<uint64_t>(vchInputData[28]) << 56;
    outputAmount = (CAmount)result;

    // get the second parameter and convert to uin32_t and assign to output var
    // commented out for now since it's unused but I wanted it  here for clarity
    // and potential afuture enhancements
    // convert the vch into a uin32_t (nAsset)
    // should be in position 40 walking backwards
    nAsset = static_cast<uint32_t>(vchInputData[39]);
    nAsset |= static_cast<uint32_t>(vchInputData[38]) << 8;
    nAsset |= static_cast<uint32_t>(vchInputData[37]) << 16;
    nAsset |= static_cast<uint32_t>(vchInputData[36]) << 24;
    return true;
}