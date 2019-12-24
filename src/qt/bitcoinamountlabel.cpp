// Copyright (c) 2019 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/bitcoinamountlabel.h>

#include <qt/bitcoinunits.h>

#include <QChar>
#include <QLabel>
#include <QString>
#include <QStringList>
#include <QWidget>

#include <cassert>

BitcoinAmountLabel::BitcoinAmountLabel(QWidget* parent)
    : QLabel(parent)
{
}

void BitcoinAmountLabel::setValue(const CAmount& value, const int unit) {
    if (unit != -1) m_unit = unit;
    m_value = value;
    if (m_privacy) {
        QString s = BitcoinUnits::formatWithUnit(m_unit, 0, false, BitcoinUnits::separatorAlways);
        s.replace('0', '#');
        setText(s);
    } else {
        setText(BitcoinUnits::formatWithUnit(m_unit, value, false, BitcoinUnits::separatorAlways));
    }
}

void BitcoinAmountLabel::setDisplayUnit(int unit)
{
    m_unit = unit;
    setValue(m_value);
}

void BitcoinAmountLabel::setPrivacyMode(bool privacy)
{
    m_privacy = privacy;
    setValue(m_value);
}
