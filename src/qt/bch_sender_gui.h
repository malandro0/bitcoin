// Copyright (c) 2011-2017 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QT_BCH_SENDER_GUI_H
#define BITCOIN_QT_BCH_SENDER_GUI_H

#if defined(HAVE_CONFIG_H)
#include "config/bitcoin-config.h"
#endif

#include <QTextEdit>
#include <QWizard>

class BCHSender : public QWizard {
    Q_OBJECT

private:
    QTextEdit *wallet_info_label;

    QWizardPage *makeIntroPage();
    QWizardPage *makeWalletInfoPage();
    QWizardPage *makeCompletePage();

    void loadWalletInfo();

private Q_SLOTS:
    void onIdChanged(int);

public:
    BCHSender();
};

#endif  // BITCOIN_QT_BCH_SENDER_GUI_H
