/*
 * Copyright (C) 2006 Zack Rusin <zack@kde.org>
 * Copyright (C) 2006, 2007 Apple Inc.  All rights reserved.
 * Copyright (C) 2008 Nokia Corporation and/or its subsidiary(-ies)
 * Copyright (C) 2013 Cisco Systems, Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE COMPUTER, INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE COMPUTER, INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. 
 */

#include "config.h"
#include "Pasteboard.h"

#include "CachedImage.h"
#include "DocumentFragment.h"
#include "DragData.h"
#include "Editor.h"
#include "Frame.h"
#include "HTMLElement.h"
#include "Image.h"
#include "NotImplemented.h"
#include "RenderImage.h"
#include "markup.h"
#include <qclipboard.h>
#include <qdebug.h>
#include <qguiapplication.h>
#include <qmimedata.h>
#include <qtextcodec.h>
#include <qurl.h>

namespace WebCore {

static bool isTextMimeType(const String& type)
{
    return type == "text/plain" || type.startsWith("text/plain;");
}

static bool isHtmlMimeType(const String& type)
{
    return type == "text/html" || type.startsWith("text/html;");
}

std::unique_ptr<Pasteboard> Pasteboard::create(const QMimeData* readableClipboard, bool isForDragAndDrop)
{
    return std::make_unique<Pasteboard>(readableClipboard, isForDragAndDrop);
}

std::unique_ptr<Pasteboard> Pasteboard::createForCopyAndPaste()
{
    return create();
}

std::unique_ptr<Pasteboard> Pasteboard::createPrivate()
{
    return create(0, true /* Not really for drag-and-drop, but shouldn't actively update the system pasteboard */);
}

std::unique_ptr<Pasteboard> Pasteboard::createForDragAndDrop()
{
    return create(0, true);
}

std::unique_ptr<Pasteboard> Pasteboard::createForDragAndDrop(const DragData& dragData)
{
    return create(dragData.platformData(), true);
}

Pasteboard::Pasteboard(const QMimeData* readableClipboard, bool isForDragAndDrop)
    : m_selectionMode(false)
    , m_readableData(readableClipboard)
    , m_writableData(0)
    , m_isForDragAndDrop(isForDragAndDrop)
{
}

Pasteboard::~Pasteboard()
{
    if (m_writableData && isForCopyAndPaste())
        m_writableData = 0;
    else
        delete m_writableData;
    m_readableData = 0;
}

void Pasteboard::writeSelection(
  Range& selectedRange, bool canSmartCopyOrDelete, Frame& frame,
  ShouldSerializeSelectedTextForDataTransfer shouldSerializeSelectedTextForDataTransfer)
{
    if (!m_writableData)
        m_writableData = new QMimeData;
    QString text = shouldSerializeSelectedTextForDataTransfer == IncludeImageAltTextForDataTransfer ? frame.editor().selectedTextForDataTransfer() : frame.editor().selectedText();
    text.replace(QChar(0xa0), QLatin1Char(' '));
    m_writableData->setText(text);

    QString markup = createMarkup(selectedRange, 0, AnnotateForInterchange, false, ResolveNonLocalURLs);
#ifdef Q_OS_MAC
    markup.prepend(QLatin1String("<html><head><meta http-equiv=\"Content-Type\" content=\"text/html; charset=utf-8\" /></head><body>"));
    markup.append(QLatin1String("</body></html>"));
    m_writableData->setData(QLatin1String("text/html"), markup.toUtf8());
#else
    m_writableData->setHtml(markup);
#endif

    if (canSmartCopyOrDelete)
        m_writableData->setData(QStringLiteral("application/vnd.qtwebkit.smartpaste"), QByteArray());
    if (isForCopyAndPaste())
        updateSystemPasteboard();
}

bool Pasteboard::canSmartReplace()
{
    if (const QMimeData* data = readData())
        return data->hasFormat(QStringLiteral("application/vnd.qtwebkit.smartpaste"));

    return false;
}

PassRefPtr<DocumentFragment> Pasteboard::documentFragment(Frame& frame, Range& context,
                                                          bool allowPlainText, bool& chosePlainText)
{
    const QMimeData* mimeData = readData();
    if (!mimeData)
        return 0;

    chosePlainText = false;

    if (mimeData->hasHtml()) {
        QString html = mimeData->html();
        if (!html.isEmpty()) {
            RefPtr<DocumentFragment> fragment = createFragmentFromMarkup(*frame.document(), html, "", DisallowScriptingAndPluginContent);
            if (fragment)
                return fragment.release();
        }
    }

    if (mimeData->hasImage()) {
        if (mimeData->hasUrls()) {
            QList<QUrl> urls = mimeData->urls();
            QString title = mimeData->text();
            if (!title.isEmpty())
                title = QStringLiteral(" title=\"") + title + QStringLiteral("\"");
            if (urls.count() == 1) {
                QString html = QStringLiteral("<img src=\"") + urls.first().toString(QUrl::FullyEncoded) + QStringLiteral("\"") + title + QStringLiteral(">");
                RefPtr<DocumentFragment> fragment = createFragmentFromMarkup(*frame.document(), html, "", DisallowScriptingAndPluginContent);
                if (fragment)
                    return fragment.release();
            }
        }
        // FIXME: We could fallback to a raw encoded data URL.
    }

    if (allowPlainText && mimeData->hasText()) {
        chosePlainText = true;
        RefPtr<DocumentFragment> fragment = createFragmentFromText(context, mimeData->text());
        if (fragment)
            return fragment.release();
    }
    return 0;
}

void Pasteboard::writePlainText(const String& text, SmartReplaceOption smartReplaceOption)
{
    if (!m_writableData)
        m_writableData = new QMimeData;
    QString qtext = text;
    qtext.replace(QChar(0xa0), QLatin1Char(' '));
    m_writableData->setText(qtext);
    if (smartReplaceOption == CanSmartReplace)
        m_writableData->setData(QLatin1String("application/vnd.qtwebkit.smartpaste"), QByteArray());
    if (isForCopyAndPaste())
        updateSystemPasteboard();
}

void Pasteboard::writeImage(Element& node, const URL& url, const String& title)
{
    notImplemented();
}

bool Pasteboard::isSelectionMode() const
{
    return m_selectionMode;
}

void Pasteboard::setSelectionMode(bool selectionMode)
{
    m_selectionMode = selectionMode;
}

const QMimeData* Pasteboard::readData() const
{
    if (m_readableData)
        return m_readableData;
    if (m_writableData)
        return m_writableData;
#ifndef QT_NO_CLIPBOARD
    if (isForCopyAndPaste())
        return QGuiApplication::clipboard()->mimeData(m_selectionMode ? QClipboard::Selection : QClipboard::Clipboard);
#endif
    return 0;
}

bool Pasteboard::hasData()
{
    const QMimeData *data = readData();
    if (!data)
        return false;
    return data->formats().count() > 0;
}

void Pasteboard::clear(const String& type)
{
    if (m_writableData) {
        m_writableData->removeFormat(type);
        if (m_writableData->formats().isEmpty()) {
            if (isForDragAndDrop())
                delete m_writableData;
            m_writableData = 0;
        }
    }
    if (isForCopyAndPaste())
        updateSystemPasteboard();
}

void Pasteboard::clear()
{
#ifndef QT_NO_CLIPBOARD
    if (isForCopyAndPaste())
        QGuiApplication::clipboard()->setMimeData(0);
    else
#endif
    delete m_writableData;
    m_writableData = 0;
}

String Pasteboard::readString(const String& type)
{
    const QMimeData* data = readData();
    if (!data)
        return String();

    if (isHtmlMimeType(type) && data->hasHtml())
        return data->html();

    if (isTextMimeType(type) && data->hasText())
        return data->text();

    QByteArray rawData = data->data(type);
    QString stringData = QTextCodec::codecForName("UTF-16")->toUnicode(rawData);
    return stringData;
}

void Pasteboard::writeString(const String& type, const String& data)
{
    if (!m_writableData)
        m_writableData = new QMimeData;

    if (isTextMimeType(type))
        m_writableData->setText(QString(data));
    else if (isHtmlMimeType(type))
        m_writableData->setHtml(QString(data));
    else {
        QByteArray array(reinterpret_cast<const char*>(data.characters8()), data.length() * 2);
        m_writableData->setData(QString(type), array);
    }
}

// extensions beyond IE's API
Vector<String> Pasteboard::types()
{
    const QMimeData* data = readData();
    if (!data)
        return Vector<String>();

    Vector<String> result;
    QStringList formats = data->formats();
    for (int i = 0; i < formats.count(); ++i)
        result.append(formats.at(i));
    return result;
}

Vector<String> Pasteboard::readFilenames()
{
    const QMimeData* data = readData();
    if (!data)
        return Vector<String>();

    Vector<String> fileList;
    QList<QUrl> urls = data->urls();

    for (int i = 0; i < urls.size(); i++) {
        QUrl url = urls[i];
        if (url.scheme() != QLatin1String("file"))
            continue;
        fileList.append(url.toLocalFile());
    }

    return fileList;
}

void Pasteboard::setDragImage(DragImageRef, const IntPoint& hotSpot)
{
    notImplemented();
}

void Pasteboard::updateSystemPasteboard()
{
    ASSERT(isForCopyAndPaste());
#ifndef QT_NO_CLIPBOARD
    QGuiApplication::clipboard()->setMimeData(m_writableData, m_selectionMode ? QClipboard::Selection : QClipboard::Clipboard);
    invalidateWritableData();
#endif
}

void Pasteboard::writePasteboard(const Pasteboard& sourcePasteboard)
{
    ASSERT(isForCopyAndPaste());
#ifndef QT_NO_CLIPBOARD
    QGuiApplication::clipboard()->setMimeData(sourcePasteboard.clipboardData());
    sourcePasteboard.invalidateWritableData();
#endif
}

void Pasteboard::read(PasteboardPlainText &)
{
    notImplemented();
}

void Pasteboard::write(const PasteboardURL &)
{
    notImplemented();
}

}
