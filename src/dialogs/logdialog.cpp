/***
 * Copyright (c) 2018, Robert Alm Nilsson
 * Copyright (c) 2013, Dan Hasting
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the organization nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 ***/

#include "logdialog.h"
#include "../error.h"
#include "../global.h"

#include <QDialogButtonBox>
#include <QGridLayout>
#include <QTextEdit>

#if QT_VERSION >= 0x050200
#include <QFontDatabase>
#endif


LogDialog::LogDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("<AppName> Log").replace("<AppName>", AppName));
    setMinimumSize(600, 400);

    logLayout = new QGridLayout(this);
    logLayout->setContentsMargins(5, 10, 5, 10);

    logArea = new QTextEdit(this);
    logArea->setWordWrapMode(QTextOption::NoWrap);
    logArea->setReadOnly(true);

#if QT_VERSION >= 0x050200
    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
#else
    QFont font("Monospace");
    font.setStyleHint(QFont::TypeWriter);
#endif
    logArea->setFont(font);

    const std::vector<LogLine> &logLines = getLogLines();
    QString output;
    for (size_t i = 0; i < logLines.size(); i++) {
        const LogLine &l = logLines[i];
        QString color;
        switch (l.level) {
        case L_ERR: color = "<span style=\"color:#990000\">"; break;
        case L_WARN: color = "<span style=\"color:#999900\">"; break;
        default: color = "<span>"; break;
        }
        QString endColor = "</span>";
        output += color
            + "[" + l.from + "] "
            + errorLevelToName(l.level, true) + ": "
            + l.msg.toHtmlEscaped() + "<br>"
            + endColor;
    }
    logArea->setHtml(output);

    logButtonBox = new QDialogButtonBox(Qt::Horizontal, this);
    logButtonBox->addButton(tr("Close"), QDialogButtonBox::AcceptRole);

    logLayout->addWidget(logArea, 0, 0);
    logLayout->addWidget(logButtonBox, 1, 0);

    connect(logButtonBox, SIGNAL(accepted()), this, SLOT(close()));

    setLayout(logLayout);
}
