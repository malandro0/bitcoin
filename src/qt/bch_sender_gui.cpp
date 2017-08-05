// Copyright (c) 2011-2017 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#if defined(HAVE_CONFIG_H)
#include "config/bitcoin-config.h"
#endif

#include "bch_sender_gui.h"

#include "guiconstants.h"
#include "guiutil.h"
#include "networkstyle.h"

#include "amount.h"
#include "base58.h"
#include "chainparams.h"
#include "core_io.h"
#include "init.h"
#include "key.h"
#include "rpc/client.h"
#include "rpc/server.h"
#include "script/standard.h"
#include "tinyformat.h"
#include "util.h"

#include <cstdlib>

#include <boost/algorithm/string.hpp>
#include <boost/filesystem/operations.hpp>

#include <QAbstractButton>
#include <QApplication>
#include <QDebug>
#include <QMessageBox>
#include <QScopedPointer>
#include <QSettings>
#include <QVBoxLayout>
#include <QWizard>

#if QT_VERSION < 0x050000
#include <QTextCodec>
#endif

#define main bitcoin_cli_main
#include "bitcoin-cli.cpp"
#undef main

static const int64_t last_common_block_time = 1501593374;
static const int64_t last_common_block_height = 478558;

static UniValue MyCallRPC(const std::string& method, const UniValue& params)
{
    UniValue reply;
    try {
        reply = CallRPC(method, params);
    } catch (const std::exception& e) {
        QMessageBox::critical(0, QObject::tr(PACKAGE_NAME), QObject::tr("Error: %1").arg(QString::fromStdString(e.what())));
        exit(EXIT_FAILURE);
    }
    const UniValue& result = find_value(reply, "result");
    const UniValue& error  = find_value(reply, "error");
    if (!error.isNull()) {
        QMessageBox::critical(0, QObject::tr(PACKAGE_NAME), QObject::tr("Error: %1").arg(QString::fromStdString(error.write())));
        exit(EXIT_FAILURE);
    }
    return result;
}

QWizardPage *BCHSender::makeIntroPage()
{
    QWizardPage * const page = new QWizardPage;
    page->setTitle("Welcome");
    QLabel * const lbl = new QLabel(QString("You are about to export BCash from your %1 wallet.").arg(QApplication::applicationName()));
    lbl->setWordWrap(true);
    QVBoxLayout * const layout = new QVBoxLayout;
    layout->addWidget(lbl);
    page->setLayout(layout);
    return page;
}

QWizardPage *BCHSender::makeWalletInfoPage()
{
    QWizardPage * const page = new QWizardPage;
    page->setTitle("Wallet information");
    wallet_info_label = new QTextEdit;
    wallet_info_label->setReadOnly(true);
    QVBoxLayout * const layout = new QVBoxLayout;
    layout->addWidget(wallet_info_label);
    page->setLayout(layout);
    return page;
}

QWizardPage *BCHSender::makeCompletePage()
{
    QWizardPage * const page = new QWizardPage;
    page->setTitle("Finished");
    QLabel * const lbl = new QLabel(QString("..."));
    lbl->setWordWrap(true);
    QVBoxLayout * const layout = new QVBoxLayout;
    layout->addWidget(lbl);
    page->setLayout(layout);
    return page;
}

void BCHSender::closeEvent(QCloseEvent *ev)
{
    if (button(QWizard::CancelButton)->isEnabled()) {
        ev->accept();
    } else {
        ev->ignore();
    }
}

void BCHSender::loadWalletInfo()
{
    const bool backEnabled = button(QWizard::BackButton)->isEnabled();
    const bool nextEnabled = button(QWizard::NextButton)->isEnabled();
    const bool cancelEnabled = button(QWizard::CancelButton)->isEnabled();
    button(QWizard::BackButton)->setEnabled(false);
    button(QWizard::NextButton)->setEnabled(false);
    button(QWizard::CancelButton)->setEnabled(false);
    QString progress_prefix = "Scanning through wallet... ";
    wallet_info_label->setHtml(progress_prefix);
    QCoreApplication::processEvents();

    UniValue params, result, result2;

    params = UniValue(UniValue::VARR);
    result = MyCallRPC("getblockcount", params);
    if (result.get_int64() < last_common_block_height && 0 /* TODO*/) {
        QMessageBox::critical(this, QObject::tr(PACKAGE_NAME), QObject::tr("Blockchain is not synced enough yet"));
        exit(EXIT_FAILURE);
    }

    params = UniValue(UniValue::VARR);
    params.push_back("*");
    params.push_back(UniValue(int(999999)));
    params.push_back(UniValue(int(0)));
    params.push_back(UniValue(false));

    result = MyCallRPC("listtransactions", params);

    std::map<std::string, std::pair<CAmount, std::string>> utxos;
    std::set<std::string> spent_utxos;
    std::set<std::string> checked_sends;
    std::vector<std::string> warnings;
    ssize_t completed = -1, total = result.size();
    for (const auto& v : result.getValues()) {
        ++completed;
        wallet_info_label->setHtml(progress_prefix + QString("%1%").arg(completed*100/total));
        QCoreApplication::processEvents();
        if (v["category"].get_str() != "receive" && v["category"].get_str() != "generate" && v["category"].get_str() != "send") {
            if (v["category"].get_str() != "move" && v["category"].get_str() != "orphan") {
                QMessageBox::critical(this, QObject::tr(PACKAGE_NAME), QObject::tr("Unknown transaction category \"%1\"").arg(QString::fromStdString(v["category"].get_str())));
                exit(EXIT_FAILURE);
            }
            continue;
        }
        if (v["confirmations"].get_int64() < 1) {
            // Unconfirmed
            continue;
        }
        if (v["blocktime"].get_int64() > last_common_block_time) {
            // Post-fork, ignore
            continue;
        }
        if (v["category"].get_str() == "send") {
            if (checked_sends.count(v["txid"].get_str())) {
                continue;
            }
            checked_sends.insert(v["txid"].get_str());
            params = UniValue(UniValue::VARR);
            params.push_back(v["txid"].get_str());
            result2 = MyCallRPC("gettransaction", params);
            CMutableTransaction tx;
            if (!DecodeHexTx(tx, result2["hex"].get_str(), true)) {
                QMessageBox::critical(this, QObject::tr(PACKAGE_NAME), QObject::tr("Error decoding transaction %1").arg(QString::fromStdString(v["txid"].get_str())));
                exit(EXIT_FAILURE);
            }
            for (const auto& inp : tx.vin) {
                const std::string utxo_txid = inp.prevout.hash.GetHex();
                const std::string utxo_id = strprintf("%s:%s", utxo_txid, inp.prevout.n);
                spent_utxos.insert(utxo_id);
                qDebug() << "spent   " << utxo_id.c_str();
            }
            // Figure out change
            int n = -1;
            for (const auto& outp : tx.vout) {
                ++n;
                CTxDestination address;
                if (!ExtractDestination(outp.scriptPubKey, address)) {
                    // Probably not ours? :/
                    warnings.push_back(strprintf("Couldn't extract destination from %s:%s", v["txid"].get_str(), n));
                    continue;
                }
                const std::string addr = CBitcoinAddress(address).ToString();
                params = UniValue(UniValue::VARR);
                params.push_back(addr);
                result2 = MyCallRPC("validateaddress", params);
                if (result2["ismine"].get_bool()) {
                    const std::string utxo_id = strprintf("%s:%s", v["txid"].get_str(), n);
                    utxos[utxo_id] = std::make_pair(outp.nValue, addr);
                    qDebug() << "change  " << strprintf("%s (val=%s; addr=%s)", utxo_id, outp.nValue, addr).c_str();
                }
            }
        } else {
            const std::string utxo_id = strprintf("%s:%s", v["txid"].get_str(), v["vout"].get_int());
            const CAmount amt = AmountFromValue(v["amount"]);
            const std::string addr = v["address"].get_str();
            utxos[utxo_id] = std::make_pair(amt, addr);
            qDebug() << "receive " << strprintf("%s (val=%s; addr=%s)", utxo_id, amt, addr).c_str();
        }
    }

    for (const auto& spent_utxo : spent_utxos) {
        auto ptr = utxos.find(spent_utxo);
        if (ptr == utxos.end()) {
            // Coinjoin I guess?
            warnings.push_back(strprintf("Spent %s, which was never received", spent_utxo));
            continue;
        }
        utxos.erase(ptr);
    }

    button(QWizard::BackButton)->setEnabled(backEnabled);
    button(QWizard::NextButton)->setEnabled(nextEnabled);
    button(QWizard::CancelButton)->setEnabled(cancelEnabled);

    if (warnings.empty()) {
        next();
    } else {
        wallet_info_label->setHtml(QString("Found %1 UTXOs (%2 warnings)<br><br>").arg(utxos.size()).arg(warnings.size())+QString::fromStdString(boost::algorithm::join(warnings, "<br>")));
        QMessageBox::warning(this, QObject::tr(PACKAGE_NAME), QObject::tr("There were warnings processing your wallet! Continue at your own risk!"));
    }
}

void BCHSender::onIdChanged(int new_id)
{
    try {
        switch (new_id) {
            case 0:
                break;
            case 1:
                loadWalletInfo();
                break;
        }
    } catch (const std::exception& e) {
        QMessageBox::critical(this, QObject::tr(PACKAGE_NAME), QObject::tr("Error: %1").arg(QString::fromStdString(e.what())));
        exit(EXIT_FAILURE);
    }
}

BCHSender::BCHSender() : QWizard()
{
    addPage(makeIntroPage());
    addPage(makeWalletInfoPage());
    addPage(makeCompletePage());
    connect(this, SIGNAL(currentIdChanged(int)), this, SLOT(onIdChanged(int)));
}

int main(int argc, char *argv[])
{
    SetupEnvironment();
    ParseParameters(argc, argv);

#if QT_VERSION < 0x050000
    // Internal string conversion is all UTF-8
    QTextCodec::setCodecForTr(QTextCodec::codecForName("UTF-8"));
    QTextCodec::setCodecForCStrings(QTextCodec::codecForTr());
#endif

    QApplication app(argc, argv);

    // used to locate QSettings
    QApplication::setOrganizationName(QAPP_ORG_NAME);
    QApplication::setOrganizationDomain(QAPP_ORG_DOMAIN);
    QApplication::setApplicationName(QAPP_APP_NAME_DEFAULT);

    // Get any QSettings datadir
    if (GetArg("-datadir", "").empty()) {
        QSettings settings;
        const QString default_data_dir = GUIUtil::boostPathToQString(GetDefaultDataDir());
        QString data_dir = settings.value("strDataDir", default_data_dir).toString();
        if (data_dir != default_data_dir) {
            SoftSetArg("-datadir", GUIUtil::qstringToBoostPath(data_dir).string());
        }
    }

    /// Determine availability of data directory and parse bitcoin.conf
    /// - Do not call GetDataDir(true) before this step finishes
    if (!boost::filesystem::is_directory(GetDataDir(false)))
    {
        QMessageBox::critical(0, QObject::tr(PACKAGE_NAME),
                              QObject::tr("Error: Expected data directory \"%1\" does not exist.").arg(QString::fromStdString(GetArg("-datadir", ""))));
        return EXIT_FAILURE;
    }
//     QMessageBox::critical(0, QObject::tr(PACKAGE_NAME), QObject::tr("directory \"%1\"").arg(GUIUtil::boostPathToQString(GetDataDir(false)))); // TODO
    try {
        ReadConfigFile(GetArg("-conf", BITCOIN_CONF_FILENAME));
    } catch (const std::exception& e) {
        QMessageBox::critical(0, QObject::tr(PACKAGE_NAME),
                              QObject::tr("Error: Cannot parse configuration file: %1. Only use key=value syntax.").arg(e.what()));
        return EXIT_FAILURE;
    }

    // Check for -testnet or -regtest parameter (Params() calls are only valid after this clause)
    try {
        SelectParams(ChainNameFromCommandLine());
    } catch(std::exception &e) {
        QMessageBox::critical(0, QObject::tr(PACKAGE_NAME), QObject::tr("Error: %1").arg(e.what()));
        return EXIT_FAILURE;
    }

    QScopedPointer<const NetworkStyle> networkStyle(NetworkStyle::instantiate(QString::fromStdString(Params().NetworkIDString())));
    assert(!networkStyle.isNull());
    // Allow for separate UI settings for testnets
    QApplication::setApplicationName(networkStyle->getAppName());
//     QMessageBox::critical(0, QObject::tr(PACKAGE_NAME), QObject::tr("directory \"%1\" rpcuser=%2 rpcpassword=%3").arg(GUIUtil::boostPathToQString(GetDataDir(true))).arg(QString::fromStdString(GetArg("-rpcuser", ""))).arg(QString::fromStdString(GetArg("-rpcpassword","")))); // TODO

    AppInitBasicSetup();

    BCHSender wiz;
    wiz.setWindowTitle(QObject::tr(PACKAGE_NAME));
    wiz.show();

    return app.exec();
}
