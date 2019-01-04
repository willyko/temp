// Copyright (c) 2017 The Dogecoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "ethereum.h"
#include "BlockHeader.h"
#include <boost/lexical_cast.hpp>

using namespace dev;
using namespace eth;


bool VerifyHeader(const std::vector<unsigned char>& data) {
        BlockHeader header(data, BlockDataType::HeaderData);
        return true;
}

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
    dev::RLP currentNode;
    const int len = parentNodes.itemCount();
    dev::RLP nodeKey = root;       
    int pathPtr = 0;
    printf("length %d\n", len);
	const std::string pathString = toHex(path);
    printf("pathString %s\n", pathString.c_str());
    int nibbles;
    bytesConstRef vec1,vec2;
    char pathPtrInt[2];
    for (int i = 0 ; i < len ; i++) {
      currentNode = parentNodes[i];
      vec1 = nodeKey.payload();
      vec2 = sha3(currentNode.data()).ref();
             
      if(vec1.size() != vec2.size() || !std::equal(vec1.begin(), vec1.end(), vec2.begin())){
        // console.log("nodeKey != sha3(rlp.encode(currentNode)): ", nodeKey, Buffer.from(sha3(rlp.encode(currentNode)),'hex'))
        return false;
      } 
      printf("pathPtr %d pathString.size() %d\n", pathPtr, pathString.size());
      if(pathPtr > pathString.size()){
        // console.log("pathPtr >= path.length ", pathPtr,  path.length)

        return false;
      }
      printf("currentNode.itemCount() %d\n", currentNode.itemCount());
      switch(currentNode.itemCount()){
        case 17://branch node
          if(pathPtr == pathString.size()){
            vec1 = currentNode[1].payload();
            vec2 = value.data();
            if(vec1.size() == vec2.size() && std::equal(vec1.begin(), vec1.end(), vec2.begin())){
                printf("17 equals ret true\n");
              return true;
            }else{
              // console.log('currentNode[16],rlp.encode(value): ', currentNode[16], rlp.encode(value))
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
          if(pathPtr == pathString.size()) { //leaf node
            vec1 = currentNode[1].payload();
            vec2 = value.data();
            if(vec1.size() == vec2.size() && std::equal(vec1.begin(), vec1.end(), vec2.begin())){
                printf("2 equals ret true\n");
              return true;
            } else {
              // console.log("currentNode[1] == rlp.encode(value) ", currentNode[1], rlp.encode(value))
              return false;
            }
          } else {//extension node
            nodeKey = currentNode[1];
          }
          break;
        default:
          // console.log("all nodes must be length 17 or 2");
          return false;
      }
    }
  return false;
}
