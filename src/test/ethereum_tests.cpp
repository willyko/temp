#include <boost/test/unit_test.hpp>

#include "uint256.h"
#include "util.h"
#include "utilstrencodings.h"
#include "ethereum/ethereum.h"
#include "script/interpreter.h"
#include "script/standard.h"
#include "policy/policy.h"

char block_header_data[] = "" \
    "f90214a09c43161bc5c218f02f3df81543af39066cd9172619d0fcd055e2dcb0" \
    "054ef000a01dcc4de8dec75d7aab85b567b6ccd41ad312451b948a7413f0a142" \
    "fd40d49347940032a342d95c5e4433b072009d33caac3e7008cda00ba974ec31" \
    "5127e146844543306520c9be681090a7aca67f1cc7598c3ad4cdd4a006f2f457" \
    "5635d71d8e668167383e56a90e29dea7e0ed8c77d857da7a616ec661a047b7b7" \
    "2c8b2ccf24e0237ea684602d2b1675e81aca7e454c90f8354c39a1f77bb90100" \
    "0800400000000000000010000000000000000000000000020000000000000010" \
    "1000000000000000000082000000000020000000000080004000080080202000" \
    "0000000000004000001001080002080000040000000000000000000000000000" \
    "0000000000000004000003800000000000000000000000000000001000000000" \
    "0002000000000008000000001000000000000006000000000040000080000000" \
    "0200000000000000000000000000000000000000000000000000004000000000" \
    "0000000200000000040000000000000000000000400000000041008080040000" \
    "0014000000004000000800100000000000000000000000a00000080040000000" \
    "845adaf1c5832198c68347b7848319106f845a25bf0896d58301080286506172" \
    "69747986312e32312e30826c69a00cb99379ba234309daff5081ffe62f9e7cec" \
    "b72fbb11b19924e493b972fb0cd8881247ec247b1fadf5";


class EthereumTestChecker : public BaseSignatureChecker
{
public:
    virtual bool CheckSig(const std::vector<unsigned char>& scriptSig, const std::vector<unsigned char>& vchPubKey, const CScript& scriptCode, SigVersion sigversion) const
    {
        return true;
    }

    virtual bool CheckEthHeader(const std::vector<unsigned char>& header) const {
        return VerifyHeader(header);
    }
};


BOOST_AUTO_TEST_SUITE(ethereum_tests)

BOOST_AUTO_TEST_CASE(ethereum_blockheader)
{
    std::vector<unsigned char> header = ParseHex(block_header_data);
    bool verified = VerifyHeader(header);
    BOOST_CHECK(verified);
}

BOOST_AUTO_TEST_CASE(ethereum_evalscript)
{
    std::vector<std::vector<unsigned char> > stack;
    ScriptError err;
    EthereumTestChecker checker;
    CScript scriptPubKey = CScript()
            << ParseHex(block_header_data)
            << OP_SYSCOIN_UNLOCK;

    BOOST_CHECK_EQUAL(EvalScript(stack, scriptPubKey, STANDARD_SCRIPT_VERIFY_FLAGS, checker, SigVersion::BASE, &err), true);

    BOOST_CHECK_EQUAL(err, SCRIPT_ERR_OK);
}

BOOST_AUTO_TEST_CASE(ethereum_verifyscript)
{
    ScriptError err;
    EthereumTestChecker checker;
    CScript scriptSig = CScript()
            << ParseHex(block_header_data);
    CScript scriptPubKey = CScript()
            << OP_SYSCOIN_UNLOCK;

    BOOST_CHECK_EQUAL(VerifyScript(scriptSig, scriptPubKey, nullptr, STANDARD_SCRIPT_VERIFY_FLAGS, checker, &err), true);

    BOOST_CHECK_EQUAL(err, SCRIPT_ERR_OK);
}

BOOST_AUTO_TEST_SUITE_END()

