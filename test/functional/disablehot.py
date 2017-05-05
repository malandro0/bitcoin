#!/usr/bin/env python3
# Copyright (c) 2017 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import *

class DisableHotKeysTest(BitcoinTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 2
        self.extra_args = [[], ['-disablehot']]

    def run_test(self):
        assert_equal(self.nodes[1].getwalletinfo()['keypoolsize'], 0)

        self.log.info("Mining blocks...")
        self.sync_all()
        self.nodes[0].generate(101)
        self.sync_all()

        n0addrR = self.nodes[0].getnewaddress()
        n0pubkR = self.nodes[0].validateaddress(n0addrR)['pubkey']
        n0addrC = self.nodes[0].getnewaddress()
        n0pubkC = self.nodes[0].validateaddress(n0addrC)['pubkey']
        n0addr  = self.nodes[0].getnewaddress()
        self.nodes[0].sendtoaddress(n0addrR, 10);
        self.nodes[0].generate(6)
        self.sync_all()

        assert_raises_rpc_error(-4,"Error: Hot keys are disabled (-disablehot)", self.nodes[1].getnewaddress)
        assert_raises_rpc_error(-4,"Error: Hot keys are disabled (-disablehot)", self.nodes[1].getrawchangeaddress)

        self.nodes[1].importpubkey(n0pubkR) #TODO switch to importmulti
        self.nodes[1].importpubkey(n0pubkC)
        assert_equal(self.nodes[1].getbalance("*", 1, True), 10)
        rawtx = self.nodes[1].createrawtransaction([], {n0addr: 1.0})
        frawtx= self.nodes[1].fundrawtransaction(rawtx, {"changeAddress": n0addrC, "includeWatching": True})

        # check if we can re-enable private keys
        assert_equal(self.nodes[1].signrawtransaction(frawtx['hex'])['complete'], False)
        self.log.info("restarting node with disablehot=0 (try to re-enable private keys which must not be possible)")
        self.stop_nodes()
        self.assert_start_raises_init_error(1, ['-disablehot=0'], 'hot keys on an already-existing wallet')

        #remove wallet
        os.remove(self.options.tmpdir + "/node1/regtest/wallet.dat")
        self.log.info("start node, create normal private key wallet")
        self.start_nodes(extra_args=[[], ['-disablehot=0']])
        self.stop_nodes()
        self.assert_start_raises_init_error(1, ['-disablehot=1'], 'hot keys on an already-existing wallet')
        self.start_nodes(extra_args=[[], ['-disablehot=0']])

if __name__ == '__main__':
    DisableHotKeysTest().main()
