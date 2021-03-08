#!/usr/bin/env python3
# Copyright (c) 2021 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""
Tests taproot activation mechanism
"""

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import (
    assert_equal,
)

class TaprootActivationTest(BitcoinTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1

    def check_taproot_state(self, state):
        info = self.nodes[0].getblockchaininfo()
        taproot_info = info["softforks"]["taproot"]
        assert_equal(taproot_info["bip8"]["status"], state)
        assert_equal(taproot_info["bip8"]["startheight"], 576)
        assert_equal(taproot_info["bip8"]["timeoutheight"], 1584)

    def run_test(self):
        self.log.info("Make sure taproot is defined first")
        self.check_taproot_state("defined")

        self.log.info("Mine up to right before the start")
        self.nodes[0].generate(574)
        self.check_taproot_state("defined")

        self.log.info("Mine a block, should now be started")
        self.nodes[0].generate(1)
        self.check_taproot_state("started")


        self.log.info("Lock in taproot")
        self.nodes[0].generate(108)
        self.check_taproot_state("started")

        self.nodes[0].generate(36)
        self.check_taproot_state("locked_in")

        self.log.info("Make sure taproot does not activate until the minimum activation height")
        self.nodes[0].generate(1871)
        self.check_taproot_state("locked_in")
        self.nodes[0].generate(1)
        self.check_taproot_state("active")

if __name__ == '__main__':
    TaprootActivationTest().main()
