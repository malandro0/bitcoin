// Copyright (c) 2019 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QT_BITCOINAMOUNTLABEL_H
#define BITCOIN_QT_BITCOINAMOUNTLABEL_H

#include <amount.h>
#include <qt/bitcoinunits.h>

#include <QLabel>
#include <QObject>
#include <QString>

QT_BEGIN_NAMESPACE
class QWidget;
QT_END_NAMESPACE

/**
 * Widget for displaying bitcoin amounts.
 */
class BitcoinAmountLabel : public QLabel
{
    Q_OBJECT

public:
    explicit BitcoinAmountLabel(QWidget* parent = nullptr);

    void setValue(const CAmount& value, int unit = -1);

    /** Change unit used to display amount. */
    void setDisplayUnit(int unit);

public Q_SLOTS:
    void setPrivacyMode(bool privacy);

private:
    CAmount m_value{0};
    int m_unit{BitcoinUnits::BTC};
    bool m_privacy{false};

};

#endif // BITCOIN_QT_BITCOINAMOUNTLABEL_H
