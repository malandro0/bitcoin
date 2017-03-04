// Copyright (c) 2017 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "qrscandialog.h"

#include "sendcoinsentry.h"

#include <QDialog>

#include "zbar.h"

QRScanDialog::QRScanDialog(SendCoinsEntry *parent) :
    QDialog(parent),
    video(NULL),
    window(NULL),
    scanner(NULL),
    thread(NULL),
    have_result(false),
    result(NULL)
{
    setAttribute(Qt::WA_DontCreateNativeAncestors);
    setAttribute(Qt::WA_NativeWindow);
}

void QRScanDialog::shutdownVideo() {
    delete video;
    delete window;
}

void QRScanDialog::setupVideo() {
    shutdownVideo();

#ifdef QT_QPA_PLATFORM_WINDOWS
    window = new zbar::Window(winId());
#elif defined(QT_QPA_PLATFORM_XCB)
    window = new zbar::Window(x11Info().display(), winId());
#else
#   warning "Unsupported platform for QR Scanning"
    return;
#endif
    video = new zbar::Video("/dev/video0");
    negotiate_format(*video, *window);
    resize(QSize(video->get_width(), video->get_height()));
}

SendCoinsRecipient QRScanDialog::exec()
{
    setupVideo();

    delete scanner;
    scanner = new zbar::ImageScanner();
}

QRScanThread::QRScanThread(QObject *parent, QRScanDialog& _qrscan) :
    QThread(parent),
    qrscan(_qrscan),
    refs(2)
{
}

void QRScanThread::run()
{
    try {
        qrscan.video->enable(true);
        while (refs > 0) {
            zbar::Image img = qrscan.video->next_image();
            window.draw(img);
            zbar::Image scanimg = img.convert("Y800");
            if (qrscan.scanner->scan(scanimg) <= 0) {
                continue;
            }

            auto results = qrscan.scanner->get_results();
            bool have_result = false;
            for (auto it = results.symbol_begin(); it != results.symbol_end(); ++it) {
                const Symbol& result = *it;
                if (result.get_type() != ZBAR_QRCODE) {
                    continue;
                }
                const QUrl uri(QString::fromStdString(result.get_data()));
                if (parseBitcoinURI(uri, &qrscan.result)) {
                    if (have_result) {
                        // We only support exactly one result; bail out
                        have_result = false;
                        break;
                    }
                    have_result = true;
                }
            }
            if (!have_result) {
                continue;
            }
            qrscan.have_result = true;
            break;
        }
    } catch (const std::exception& e) {
        // FIXME
    }
    destroy();
}

void QRScanThread::destroy()
{
    if (!--refs) {
        delete this;
    }
}
