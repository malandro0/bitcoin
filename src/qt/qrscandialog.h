// Copyright (c) 2017 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QT_QRSCANDIALOG_H
#define BITCOIN_QT_QRSCANDIALOG_H

#include <atomic>

#include <QDialog>

#include "zbar.h"

class QRScanThread;
class SendCoinsEntry;

QRScanDialog : public QDialog
{
    Q_OBJECT

protected:
    friend class QRScanThread;

    zbar::Video *video;
    zbar::Windows *window;
    zbar::ImageScanner *scanner;
    QRScanThread *thread;
    bool have_result;
    SendCoinsRecipient *result;

public:
    QRScanDialog(SendCoinsEntry *parent);

    SendCoinsRecipient exec();
}

QRScanThread : public QThread
{
    Q_OBJECT

private:
    QRScanDialog& qrscan;
    std::atomic<unsigned> refs;

public:
    QRScanThread(QObject *parent, QRScanDialog&);

    void run();
    void destroy();
}

#endif // BITCOIN_QT_QRSCANDIALOG_H
