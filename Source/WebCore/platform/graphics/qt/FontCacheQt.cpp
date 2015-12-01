/*
    Copyright (C) 2008 Nokia Corporation and/or its subsidiary(-ies)
    Copyright (C) 2008 Holger Hans Peter Freyther
    Copyright (C) 2006, 2008 Apple Inc. All rights reserved.
    Copyright (C) 2007 Nicholas Shanks <webkit@nickshanks.com>

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Library General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Library General Public License for more details.

    You should have received a copy of the GNU Library General Public License
    along with this library; see the file COPYING.LIB.  If not, write to
    the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
    Boston, MA 02110-1301, USA.

    This class provides all functionality needed for loading images, style sheets and html
    pages from the web. It has a memory cache for these objects.
*/
#include "config.h"
#include "FontCache.h"

#include "Font.h"
#include "FontDescription.h"
#include "FontPlatformData.h"
#include <utility>
#include <wtf/ListHashSet.h>
#include <wtf/StdLibExtras.h>
#include <wtf/text/StringHash.h>
#include <wtf/text/WTFString.h>

#include "NotImplemented.h"

#include <QFont>
#include <QFontDatabase>
#include <QTextLayout>

using namespace WTF;

namespace WebCore {

void FontCache::platformInit()
{
}

static QRawFont rawFontForCharacters(const QString& string, const QRawFont& font)
{
    QTextLayout layout(string);
    layout.setRawFont(font);
    layout.beginLayout();
    layout.createLine();
    layout.endLayout();

    QList<QGlyphRun> glyphList = layout.glyphRuns();
    ASSERT(glyphList.size() <= 1);
    if (!glyphList.size())
        return QRawFont();

    const QGlyphRun& glyphs(glyphList.at(0));
    return glyphs.rawFont();
}

RefPtr<Font> FontCache::systemFallbackForCharacters(const FontDescription& description, const Font* originalFontData, bool, const UChar* characters, unsigned length)
{
    RefPtr<Font> fontData;
    QString qstring = QString::fromRawData(reinterpret_cast<const QChar*>(characters), length);
    QRawFont computedFont = rawFontForCharacters(qstring, originalFontData->rawFont());
    if (!computedFont.isValid())
        return 0;

    FontPlatformData* result = getCachedFontPlatformData(description, computedFont.familyName());
    if (result) {
      fontData = fontForPlatformData(*result);
    }

    return fontData.release();
}

std::unique_ptr<FontPlatformData> FontCache::createFontPlatformData(const FontDescription& fontDescription, const AtomicString& familyName)
{
    QFontDatabase db;
    if (!db.hasFamily(familyName))
        return nullptr;
    return std::make_unique<FontPlatformData>(fontDescription, familyName);
}

Vector<FontTraitsMask> FontCache::getTraitsInFamily(const AtomicString& familyName)
{
    notImplemented();
    Vector<FontTraitsMask> traits;
    return traits;
}

Ref<Font> FontCache::lastResortFallbackFont(const FontDescription& fontDescription)
{
    DEPRECATED_DEFINE_STATIC_LOCAL(AtomicString, fallbackFontName, ());
    if (!fallbackFontName.isEmpty())
        return *fontForFamily(fontDescription, fallbackFontName);

    static AtomicString timesStr("serif");
    return *fontForFamily(fontDescription, timesStr, false);
}

}// namespace WebCore
