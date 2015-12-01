/*
 * Copyright (C) 2015 Apple Inc. All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef MemoryIndex_h
#define MemoryIndex_h

#if ENABLE(INDEXED_DATABASE)

#include "IDBGetResult.h"
#include "IDBIndexInfo.h"
#include <memory>

namespace WebCore {

class ThreadSafeDataBuffer;

struct IDBKeyRangeData;

namespace IndexedDB {
enum class IndexRecordType;
}

namespace IDBServer {

class MemoryIndex {
    friend std::unique_ptr<MemoryIndex> std::make_unique<MemoryIndex>(const WebCore::IDBIndexInfo&);
public:
    static std::unique_ptr<MemoryIndex> create(const IDBIndexInfo&);

    ~MemoryIndex();

    const IDBIndexInfo& info() const { return m_info; }

    IDBGetResult valueForKeyRange(IndexedDB::IndexRecordType, const IDBKeyRangeData&) const;
    uint64_t countForKeyRange(const IDBKeyRangeData&);
    
private:
    MemoryIndex(const IDBIndexInfo&);

    IDBIndexInfo m_info;
};

} // namespace IDBServer
} // namespace WebCore

#endif // ENABLE(INDEXED_DATABASE)
#endif // MemoryIndex_h
