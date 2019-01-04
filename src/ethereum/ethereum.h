// Copyright (c) 2017 The Dogecoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef SYSCOIN_ETHEREUM_ETHEREUM_H
#define SYSCOIN_ETHEREUM_ETHEREUM_H

#include <vector>
#include "CommonData.h"
#include "RLP.h"
bool VerifyHeader(const std::vector<unsigned char>& header);

bool VerifyProof(dev::bytesConstRef path, const dev::RLP& value, const dev::RLP& parentNodes, const dev::RLP& root); 

#endif // SYSCOIN_ETHEREUM_ETHEREUM_H
