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
  int partialPathInt = boost::lexical_cast<int>(encodedPartialPath[0]);
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
    const int len = parentNodes.size();
    dev::RLP nodeKey = root;       
    int pathPtr = 0;

	const std::string pathString = toHex(path);
    int nibbles;
    for (int i = 0 ; i < len ; i++) {
      currentNode = parentNodes[i];
      if(nodeKey.data() != sha3(currentNode.data()).ref()){
        // console.log("nodeKey != sha3(rlp.encode(currentNode)): ", nodeKey, Buffer.from(sha3(rlp.encode(currentNode)),'hex'))
        return false;
      }
      if(pathPtr > pathString.size()){
        // console.log("pathPtr >= path.length ", pathPtr,  path.length)

        return false;
      }

      switch(currentNode.size()){
        case 17://branch node
          if(pathPtr == pathString.size()){
            if(currentNode[16].data() == value.data()) {
              return true;
            }else{
              // console.log('currentNode[16],rlp.encode(value): ', currentNode[16], rlp.encode(value))
              return false;
            }
          }
          nodeKey = currentNode[boost::lexical_cast<int>(pathString[pathPtr])]; //must == sha3(rlp.encode(currentNode[path[pathptr]]))
          pathPtr += 1;         
          // console.log(nodeKey, pathPtr, path[pathPtr])
          break;
        case 2:
          nibbles = nibblesToTraverse(toHex(currentNode[0].data()), pathString, pathPtr);
          if(nibbles <= -1)
            return false;
          pathPtr += nibbles;
          if(pathPtr == pathString.size()) { //leaf node
            if(currentNode[1].data() == value.data()){
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
