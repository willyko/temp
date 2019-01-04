// Copyright (c) 2017 The Dogecoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "ethereum.h"
#include "BlockHeader.h"
#include "CommonData.h"

using namespace dev;
using namespace eth;


bool VerifyHeader(const std::vector<unsigned char>& data) {
        BlockHeader header(data, BlockDataType::HeaderData);
        return true;
}

bool VerifyProof(bytesConstRef path, bytesConstRef value, std::vector<bytesConstRef> parentNodes, bytesConstRef root) {
    bytesConstRef currentNode;
    int len = parentNodes.size();
    bytesRef nodeKey = root;
    int pathPtr = 0;

	string pathString = toHex(path);

    for (int i = 0 ; i < len ; i++) {
      currentNode = parentNodes[i];
      if(nodeKey != sha3(RLP(currentNode).data()).ref()){
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
            if(currentNode[16] == RLP(value).data()) {
              return true;
            }else{
              // console.log('currentNode[16],rlp.encode(value): ', currentNode[16], rlp.encode(value))
              return false;
            }
          }
          nodeKey = currentNode[atoi(pathString[pathPtr])]; //must == sha3(rlp.encode(currentNode[path[pathptr]]))
          pathPtr += 1;         
          // console.log(nodeKey, pathPtr, path[pathPtr])
          break;
        case 2:
          pathPtr += nibblesToTraverse(toHex(currentNode[0]), pathString, pathPtr);
          if(pathPtr == pathString.size()) { //leaf node
            if(currentNode[1] == RLP(value).data()){
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
