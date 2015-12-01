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
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "UniqueIDBDatabase.h"

#if ENABLE(INDEXED_DATABASE)

#include "IDBKeyRangeData.h"
#include "IDBResultData.h"
#include "IDBServer.h"
#include "IDBTransactionInfo.h"
#include "Logging.h"
#include "UniqueIDBDatabaseConnection.h"
#include <wtf/MainThread.h>
#include <wtf/ThreadSafeRefCounted.h>

namespace WebCore {
namespace IDBServer {
    
UniqueIDBDatabase::UniqueIDBDatabase(IDBServer& server, const IDBDatabaseIdentifier& identifier)
    : m_server(server)
    , m_identifier(identifier)
    , m_transactionSchedulingTimer(*this, &UniqueIDBDatabase::transactionSchedulingTimerFired)
{
}

const IDBDatabaseInfo& UniqueIDBDatabase::info() const
{
    RELEASE_ASSERT(m_databaseInfo);
    return *m_databaseInfo;
}

void UniqueIDBDatabase::openDatabaseConnection(IDBConnectionToClient& connection, const IDBRequestData& requestData)
{
    auto operation = IDBServerOperation::create(connection, requestData);
    m_pendingOpenDatabaseOperations.append(WTF::move(operation));

    if (m_databaseInfo) {
        handleOpenDatabaseOperations();
        return;
    }
    
    m_server.postDatabaseTask(createCrossThreadTask(*this, &UniqueIDBDatabase::openBackingStore, m_identifier));
}

void UniqueIDBDatabase::handleOpenDatabaseOperations()
{
    ASSERT(isMainThread());
    LOG(IndexedDB, "(main) UniqueIDBDatabase::handleOpenDatabaseOperations");

    // If a version change transaction is currently in progress, no new connections can be opened right now.
    // We will try again later.
    if (m_versionChangeDatabaseConnection)
        return;

    auto operation = m_pendingOpenDatabaseOperations.takeFirst();

    // 3.3.1 Opening a database
    // If requested version is undefined, then let requested version be 1 if db was created in the previous step,
    // or the current version of db otherwise.
    uint64_t requestedVersion = operation->requestData().requestedVersion();
    if (!requestedVersion)
        requestedVersion = m_databaseInfo->version() ? m_databaseInfo->version() : 1;

    // 3.3.1 Opening a database
    // If the database version higher than the requested version, abort these steps and return a VersionError.
    if (requestedVersion < m_databaseInfo->version()) {
        auto result = IDBResultData::error(operation->requestData().requestIdentifier(), IDBError(IDBExceptionCode::VersionError));
        operation->connection().didOpenDatabase(result);
        return;
    }

    Ref<UniqueIDBDatabaseConnection> connection = UniqueIDBDatabaseConnection::create(*this, operation->connection());
    UniqueIDBDatabaseConnection* rawConnection = &connection.get();

    if (requestedVersion == m_databaseInfo->version()) {
        addOpenDatabaseConnection(WTF::move(connection));

        auto result = IDBResultData::openDatabaseSuccess(operation->requestData().requestIdentifier(), *rawConnection);
        operation->connection().didOpenDatabase(result);
        return;
    }

    ASSERT(!m_versionChangeOperation);
    ASSERT(!m_versionChangeDatabaseConnection);

    m_versionChangeOperation = adoptRef(operation.leakRef());
    m_versionChangeDatabaseConnection = rawConnection;

    // 3.3.7 "versionchange" transaction steps
    // If there's no other open connections to this database, the version change process can begin immediately.
    if (!hasAnyOpenConnections()) {
        startVersionChangeTransaction();
        return;
    }

    // Otherwise we have to notify all those open connections and wait for them to close.
    notifyConnectionsOfVersionChange();
}

bool UniqueIDBDatabase::hasAnyOpenConnections() const
{
    return !m_openDatabaseConnections.isEmpty();
}

static uint64_t generateUniqueCallbackIdentifier()
{
    ASSERT(isMainThread());
    static uint64_t currentID = 0;
    return ++currentID;
}

uint64_t UniqueIDBDatabase::storeCallback(ErrorCallback callback)
{
    uint64_t identifier = generateUniqueCallbackIdentifier();
    ASSERT(!m_errorCallbacks.contains(identifier));
    m_errorCallbacks.add(identifier, callback);
    return identifier;
}

uint64_t UniqueIDBDatabase::storeCallback(KeyDataCallback callback)
{
    uint64_t identifier = generateUniqueCallbackIdentifier();
    ASSERT(!m_keyDataCallbacks.contains(identifier));
    m_keyDataCallbacks.add(identifier, callback);
    return identifier;
}

uint64_t UniqueIDBDatabase::storeCallback(GetResultCallback callback)
{
    uint64_t identifier = generateUniqueCallbackIdentifier();
    ASSERT(!m_getResultCallbacks.contains(identifier));
    m_getResultCallbacks.add(identifier, callback);
    return identifier;
}

uint64_t UniqueIDBDatabase::storeCallback(CountCallback callback)
{
    uint64_t identifier = generateUniqueCallbackIdentifier();
    ASSERT(!m_countCallbacks.contains(identifier));
    m_countCallbacks.add(identifier, callback);
    return identifier;
}

void UniqueIDBDatabase::startVersionChangeTransaction()
{
    LOG(IndexedDB, "(main) UniqueIDBDatabase::startVersionChangeTransaction");

    ASSERT(!m_versionChangeTransaction);
    ASSERT(m_versionChangeOperation);
    ASSERT(m_versionChangeDatabaseConnection);

    auto operation = m_versionChangeOperation;
    m_versionChangeOperation = nullptr;

    uint64_t requestedVersion = operation->requestData().requestedVersion();
    if (!requestedVersion)
        requestedVersion = m_databaseInfo->version() ? m_databaseInfo->version() : 1;

    addOpenDatabaseConnection(*m_versionChangeDatabaseConnection);

    m_versionChangeTransaction = &m_versionChangeDatabaseConnection->createVersionChangeTransaction(requestedVersion);
    m_inProgressTransactions.set(m_versionChangeTransaction->info().identifier(), m_versionChangeTransaction);
    m_server.postDatabaseTask(createCrossThreadTask(*this, &UniqueIDBDatabase::beginTransactionInBackingStore, m_versionChangeTransaction->info()));

    auto result = IDBResultData::openDatabaseUpgradeNeeded(operation->requestData().requestIdentifier(), *m_versionChangeTransaction);
    operation->connection().didOpenDatabase(result);
}

void UniqueIDBDatabase::beginTransactionInBackingStore(const IDBTransactionInfo& info)
{
    LOG(IndexedDB, "(db) UniqueIDBDatabase::beginTransactionInBackingStore");
    m_backingStore->beginTransaction(info);
}

void UniqueIDBDatabase::notifyConnectionsOfVersionChange()
{
    ASSERT(m_versionChangeOperation);
    ASSERT(m_versionChangeDatabaseConnection);

    uint64_t requestedVersion = m_versionChangeOperation->requestData().requestedVersion();

    LOG(IndexedDB, "(main) UniqueIDBDatabase::notifyConnectionsOfVersionChange - %" PRIu64, requestedVersion);

    // 3.3.7 "versionchange" transaction steps
    // Fire a versionchange event at each connection in m_openDatabaseConnections that is open.
    // The event must not be fired on connections which has the closePending flag set.
    for (auto connection : m_openDatabaseConnections) {
        if (connection->closePending())
            continue;

        connection->fireVersionChangeEvent(requestedVersion);
    }
}

void UniqueIDBDatabase::addOpenDatabaseConnection(Ref<UniqueIDBDatabaseConnection>&& connection)
{
    ASSERT(!m_openDatabaseConnections.contains(&connection.get()));
    m_openDatabaseConnections.add(adoptRef(connection.leakRef()));
}

void UniqueIDBDatabase::openBackingStore(const IDBDatabaseIdentifier& identifier)
{
    ASSERT(!isMainThread());
    LOG(IndexedDB, "(db) UniqueIDBDatabase::openBackingStore");

    ASSERT(!m_backingStore);
    m_backingStore = m_server.createBackingStore(identifier);
    auto databaseInfo = m_backingStore->getOrEstablishDatabaseInfo();

    m_server.postDatabaseTaskReply(createCrossThreadTask(*this, &UniqueIDBDatabase::didOpenBackingStore, databaseInfo));
}

void UniqueIDBDatabase::didOpenBackingStore(const IDBDatabaseInfo& info)
{
    ASSERT(isMainThread());
    LOG(IndexedDB, "(main) UniqueIDBDatabase::didOpenBackingStore");
    
    m_databaseInfo = std::make_unique<IDBDatabaseInfo>(info);

    handleOpenDatabaseOperations();
}

void UniqueIDBDatabase::createObjectStore(UniqueIDBDatabaseTransaction& transaction, const IDBObjectStoreInfo& info, ErrorCallback callback)
{
    ASSERT(isMainThread());
    LOG(IndexedDB, "(main) UniqueIDBDatabase::createObjectStore");

    uint64_t callbackID = storeCallback(callback);
    m_server.postDatabaseTask(createCrossThreadTask(*this, &UniqueIDBDatabase::performCreateObjectStore, callbackID, transaction.info().identifier(), info));
}

void UniqueIDBDatabase::performCreateObjectStore(uint64_t callbackIdentifier, const IDBResourceIdentifier& transactionIdentifier, const IDBObjectStoreInfo& info)
{
    ASSERT(!isMainThread());
    LOG(IndexedDB, "(db) UniqueIDBDatabase::performCreateObjectStore");

    ASSERT(m_backingStore);
    m_backingStore->createObjectStore(transactionIdentifier, info);

    IDBError error;
    m_server.postDatabaseTaskReply(createCrossThreadTask(*this, &UniqueIDBDatabase::didPerformCreateObjectStore, callbackIdentifier, error, info));
}

void UniqueIDBDatabase::didPerformCreateObjectStore(uint64_t callbackIdentifier, const IDBError& error, const IDBObjectStoreInfo& info)
{
    ASSERT(isMainThread());
    LOG(IndexedDB, "(main) UniqueIDBDatabase::didPerformCreateObjectStore");

    if (error.isNull())
        m_databaseInfo->addExistingObjectStore(info);

    performErrorCallback(callbackIdentifier, error);
}

void UniqueIDBDatabase::deleteObjectStore(UniqueIDBDatabaseTransaction& transaction, const String& objectStoreName, ErrorCallback callback)
{
    ASSERT(isMainThread());
    LOG(IndexedDB, "(main) UniqueIDBDatabase::deleteObjectStore");

    uint64_t callbackID = storeCallback(callback);
    m_server.postDatabaseTask(createCrossThreadTask(*this, &UniqueIDBDatabase::performDeleteObjectStore, callbackID, transaction.info().identifier(), objectStoreName));
}

void UniqueIDBDatabase::performDeleteObjectStore(uint64_t callbackIdentifier, const IDBResourceIdentifier& transactionIdentifier, const String& objectStoreName)
{
    ASSERT(!isMainThread());
    LOG(IndexedDB, "(db) UniqueIDBDatabase::performDeleteObjectStore");

    ASSERT(m_backingStore);
    m_backingStore->deleteObjectStore(transactionIdentifier, objectStoreName);

    IDBError error;
    m_server.postDatabaseTaskReply(createCrossThreadTask(*this, &UniqueIDBDatabase::didPerformDeleteObjectStore, callbackIdentifier, error, objectStoreName));
}

void UniqueIDBDatabase::didPerformDeleteObjectStore(uint64_t callbackIdentifier, const IDBError& error, const String& objectStoreName)
{
    ASSERT(isMainThread());
    LOG(IndexedDB, "(main) UniqueIDBDatabase::didPerformDeleteObjectStore");

    if (error.isNull())
        m_databaseInfo->deleteObjectStore(objectStoreName);

    performErrorCallback(callbackIdentifier, error);
}

void UniqueIDBDatabase::clearObjectStore(UniqueIDBDatabaseTransaction& transaction, uint64_t objectStoreIdentifier, ErrorCallback callback)
{
    ASSERT(isMainThread());
    LOG(IndexedDB, "(main) UniqueIDBDatabase::clearObjectStore");

    uint64_t callbackID = storeCallback(callback);
    m_server.postDatabaseTask(createCrossThreadTask(*this, &UniqueIDBDatabase::performClearObjectStore, callbackID, transaction.info().identifier(), objectStoreIdentifier));
}

void UniqueIDBDatabase::performClearObjectStore(uint64_t callbackIdentifier, const IDBResourceIdentifier& transactionIdentifier, uint64_t objectStoreIdentifier)
{
    ASSERT(!isMainThread());
    LOG(IndexedDB, "(db) UniqueIDBDatabase::performClearObjectStore");

    ASSERT(m_backingStore);
    m_backingStore->clearObjectStore(transactionIdentifier, objectStoreIdentifier);

    IDBError error;
    m_server.postDatabaseTaskReply(createCrossThreadTask(*this, &UniqueIDBDatabase::didPerformClearObjectStore, callbackIdentifier, error));
}

void UniqueIDBDatabase::didPerformClearObjectStore(uint64_t callbackIdentifier, const IDBError& error)
{
    ASSERT(isMainThread());
    LOG(IndexedDB, "(main) UniqueIDBDatabase::didPerformClearObjectStore");

    performErrorCallback(callbackIdentifier, error);
}

void UniqueIDBDatabase::createIndex(UniqueIDBDatabaseTransaction& transaction, const IDBIndexInfo& info, ErrorCallback callback)
{
    ASSERT(isMainThread());
    LOG(IndexedDB, "(main) UniqueIDBDatabase::createIndex");

    uint64_t callbackID = storeCallback(callback);
    m_server.postDatabaseTask(createCrossThreadTask(*this, &UniqueIDBDatabase::performCreateIndex, callbackID, transaction.info().identifier(), info));
}

void UniqueIDBDatabase::performCreateIndex(uint64_t callbackIdentifier, const IDBResourceIdentifier& transactionIdentifier, const IDBIndexInfo& info)
{
    ASSERT(!isMainThread());
    LOG(IndexedDB, "(db) UniqueIDBDatabase::performCreateIndex");

    ASSERT(m_backingStore);
    m_backingStore->createIndex(transactionIdentifier, info);

    IDBError error;
    m_server.postDatabaseTaskReply(createCrossThreadTask(*this, &UniqueIDBDatabase::didPerformCreateIndex, callbackIdentifier, error, info));
}

void UniqueIDBDatabase::didPerformCreateIndex(uint64_t callbackIdentifier, const IDBError& error, const IDBIndexInfo& info)
{
    ASSERT(isMainThread());
    LOG(IndexedDB, "(main) UniqueIDBDatabase::didPerformCreateIndex");

    if (error.isNull()) {
        ASSERT(m_databaseInfo);
        auto* objectStoreInfo = m_databaseInfo->infoForExistingObjectStore(info.objectStoreIdentifier());
        ASSERT(objectStoreInfo);
        objectStoreInfo->addExistingIndex(info);
    }

    performErrorCallback(callbackIdentifier, error);
}

void UniqueIDBDatabase::putOrAdd(const IDBRequestData& requestData, const IDBKeyData& keyData, const ThreadSafeDataBuffer& valueData, IndexedDB::ObjectStoreOverwriteMode overwriteMode, KeyDataCallback callback)
{
    ASSERT(isMainThread());
    LOG(IndexedDB, "(main) UniqueIDBDatabase::putOrAdd");

    uint64_t callbackID = storeCallback(callback);
    m_server.postDatabaseTask(createCrossThreadTask(*this, &UniqueIDBDatabase::performPutOrAdd, callbackID, requestData.transactionIdentifier(), requestData.objectStoreIdentifier(), keyData, valueData, overwriteMode));
}

void UniqueIDBDatabase::performPutOrAdd(uint64_t callbackIdentifier, const IDBResourceIdentifier& transactionIdentifier, uint64_t objectStoreIdentifier, const IDBKeyData& keyData, const ThreadSafeDataBuffer& valueData, IndexedDB::ObjectStoreOverwriteMode overwriteMode)
{
    ASSERT(!isMainThread());
    LOG(IndexedDB, "(db) UniqueIDBDatabase::performPutOrAdd");

    ASSERT(m_backingStore);
    ASSERT(objectStoreIdentifier);

    IDBKeyData usedKey;
    IDBError error;

    auto objectStoreInfo = m_databaseInfo->infoForExistingObjectStore(objectStoreIdentifier);
    if (!objectStoreInfo) {
        error = IDBError(IDBExceptionCode::InvalidStateError, ASCIILiteral("Object store cannot be found in the backing store"));
        m_server.postDatabaseTaskReply(createCrossThreadTask(*this, &UniqueIDBDatabase::didPerformPutOrAdd, callbackIdentifier, error, usedKey));
        return;
    }

    if (objectStoreInfo->autoIncrement() && !keyData.isValid()) {
        uint64_t keyNumber;
        error = m_backingStore->generateKeyNumber(transactionIdentifier, objectStoreIdentifier, keyNumber);
        if (!error.isNull()) {
            m_server.postDatabaseTaskReply(createCrossThreadTask(*this, &UniqueIDBDatabase::didPerformPutOrAdd, callbackIdentifier, error, usedKey));
            return;
        }
        
        usedKey.setNumberValue(keyNumber);
    } else
        usedKey = keyData;

    if (overwriteMode == IndexedDB::ObjectStoreOverwriteMode::NoOverwrite) {
        bool keyExists;
        error = m_backingStore->keyExistsInObjectStore(transactionIdentifier, objectStoreIdentifier, usedKey, keyExists);
        if (error.isNull() && keyExists)
            error = IDBError(IDBExceptionCode::ConstraintError, ASCIILiteral("Key already exists in the object store"));

        if (!error.isNull()) {
            m_server.postDatabaseTaskReply(createCrossThreadTask(*this, &UniqueIDBDatabase::didPerformPutOrAdd, callbackIdentifier, error, usedKey));
            return;
        }
    }

    // 3.4.1 Object Store Storage Operation
    // ...If a record already exists in store ...
    // then remove the record from store using the steps for deleting records from an object store...
    // This is important because formally deleting it from from the object store also removes it from the appropriate indexes.
    error = m_backingStore->deleteRange(transactionIdentifier, objectStoreIdentifier, usedKey);
    if (!error.isNull()) {
        m_server.postDatabaseTaskReply(createCrossThreadTask(*this, &UniqueIDBDatabase::didPerformPutOrAdd, callbackIdentifier, error, usedKey));
        return;
    }

    error = m_backingStore->putRecord(transactionIdentifier, objectStoreIdentifier, usedKey, valueData);

    m_server.postDatabaseTaskReply(createCrossThreadTask(*this, &UniqueIDBDatabase::didPerformPutOrAdd, callbackIdentifier, error, usedKey));
}

void UniqueIDBDatabase::didPerformPutOrAdd(uint64_t callbackIdentifier, const IDBError& error, const IDBKeyData& resultKey)
{
    ASSERT(isMainThread());
    LOG(IndexedDB, "(main) UniqueIDBDatabase::didPerformPutOrAdd");

    performKeyDataCallback(callbackIdentifier, error, resultKey);
}

void UniqueIDBDatabase::getRecord(const IDBRequestData& requestData, const IDBKeyRangeData& range, GetResultCallback callback)
{
    ASSERT(isMainThread());
    LOG(IndexedDB, "(main) UniqueIDBDatabase::getRecord");

    uint64_t callbackID = storeCallback(callback);

    if (uint64_t indexIdentifier = requestData.indexIdentifier())
        m_server.postDatabaseTask(createCrossThreadTask(*this, &UniqueIDBDatabase::performGetIndexRecord, callbackID, requestData.transactionIdentifier(), requestData.objectStoreIdentifier(), indexIdentifier, requestData.indexRecordType(), range));
    else
        m_server.postDatabaseTask(createCrossThreadTask(*this, &UniqueIDBDatabase::performGetRecord, callbackID, requestData.transactionIdentifier(), requestData.objectStoreIdentifier(), range));
}

void UniqueIDBDatabase::performGetRecord(uint64_t callbackIdentifier, const IDBResourceIdentifier& transactionIdentifier, uint64_t objectStoreIdentifier, const IDBKeyRangeData& keyRangeData)
{
    ASSERT(!isMainThread());
    LOG(IndexedDB, "(db) UniqueIDBDatabase::performGetRecord");

    ASSERT(m_backingStore);

    ThreadSafeDataBuffer valueData;
    IDBError error = m_backingStore->getRecord(transactionIdentifier, objectStoreIdentifier, keyRangeData, valueData);

    m_server.postDatabaseTaskReply(createCrossThreadTask(*this, &UniqueIDBDatabase::didPerformGetRecord, callbackIdentifier, error, valueData));
}

void UniqueIDBDatabase::performGetIndexRecord(uint64_t callbackIdentifier, const IDBResourceIdentifier& transactionIdentifier, uint64_t objectStoreIdentifier, uint64_t indexIdentifier, IndexedDB::IndexRecordType recordType, const IDBKeyRangeData& range)
{
    ASSERT(!isMainThread());
    LOG(IndexedDB, "(db) UniqueIDBDatabase::performGetIndexRecord");

    ASSERT(m_backingStore);

    IDBGetResult result;
    IDBError error = m_backingStore->getIndexRecord(transactionIdentifier, objectStoreIdentifier, indexIdentifier, recordType, range, result);

    m_server.postDatabaseTaskReply(createCrossThreadTask(*this, &UniqueIDBDatabase::didPerformGetRecord, callbackIdentifier, error, result));
}

void UniqueIDBDatabase::didPerformGetRecord(uint64_t callbackIdentifier, const IDBError& error, const IDBGetResult& result)
{
    ASSERT(isMainThread());
    LOG(IndexedDB, "(main) UniqueIDBDatabase::didPerformGetRecord");

    performGetResultCallback(callbackIdentifier, error, result);
}

void UniqueIDBDatabase::getCount(const IDBRequestData& requestData, const IDBKeyRangeData& range, CountCallback callback)
{
    ASSERT(isMainThread());
    LOG(IndexedDB, "(main) UniqueIDBDatabase::getCount");

    uint64_t callbackID = storeCallback(callback);
    m_server.postDatabaseTask(createCrossThreadTask(*this, &UniqueIDBDatabase::performGetCount, callbackID, requestData.transactionIdentifier(), requestData.objectStoreIdentifier(), requestData.indexIdentifier(), range));
}

void UniqueIDBDatabase::performGetCount(uint64_t callbackIdentifier, const IDBResourceIdentifier& transactionIdentifier, uint64_t objectStoreIdentifier, uint64_t indexIdentifier, const IDBKeyRangeData& keyRangeData)
{
    ASSERT(!isMainThread());
    LOG(IndexedDB, "(db) UniqueIDBDatabase::performGetCount");

    ASSERT(m_backingStore);
    ASSERT(objectStoreIdentifier);

    uint64_t count;
    IDBError error = m_backingStore->getCount(transactionIdentifier, objectStoreIdentifier, indexIdentifier, keyRangeData, count);

    m_server.postDatabaseTaskReply(createCrossThreadTask(*this, &UniqueIDBDatabase::didPerformGetCount, callbackIdentifier, error, count));
}

void UniqueIDBDatabase::didPerformGetCount(uint64_t callbackIdentifier, const IDBError& error, uint64_t count)
{
    ASSERT(isMainThread());
    LOG(IndexedDB, "(main) UniqueIDBDatabase::didPerformGetCount");

    performCountCallback(callbackIdentifier, error, count);
}

void UniqueIDBDatabase::deleteRecord(const IDBRequestData& requestData, const IDBKeyRangeData& keyRangeData, ErrorCallback callback)
{
    ASSERT(isMainThread());
    LOG(IndexedDB, "(main) UniqueIDBDatabase::deleteRecord");

    uint64_t callbackID = storeCallback(callback);
    m_server.postDatabaseTask(createCrossThreadTask(*this, &UniqueIDBDatabase::performDeleteRecord, callbackID, requestData.transactionIdentifier(), requestData.objectStoreIdentifier(), keyRangeData));
}

void UniqueIDBDatabase::performDeleteRecord(uint64_t callbackIdentifier, const IDBResourceIdentifier& transactionIdentifier, uint64_t objectStoreIdentifier, const IDBKeyRangeData& range)
{
    ASSERT(!isMainThread());
    LOG(IndexedDB, "(db) UniqueIDBDatabase::performDeleteRecord");

    IDBError error = m_backingStore->deleteRange(transactionIdentifier, objectStoreIdentifier, range);

    m_server.postDatabaseTaskReply(createCrossThreadTask(*this, &UniqueIDBDatabase::didPerformDeleteRecord, callbackIdentifier, error));
}

void UniqueIDBDatabase::didPerformDeleteRecord(uint64_t callbackIdentifier, const IDBError& error)
{
    ASSERT(isMainThread());
    LOG(IndexedDB, "(main) UniqueIDBDatabase::didPerformDeleteRecord");

    performErrorCallback(callbackIdentifier, error);
}

void UniqueIDBDatabase::commitTransaction(UniqueIDBDatabaseTransaction& transaction, ErrorCallback callback)
{
    ASSERT(isMainThread());
    LOG(IndexedDB, "(main) UniqueIDBDatabase::commitTransaction");

    ASSERT(&transaction.databaseConnection().database() == this);

    if (m_versionChangeTransaction == &transaction) {
        ASSERT(&m_versionChangeTransaction->databaseConnection() == m_versionChangeDatabaseConnection);
        m_databaseInfo->setVersion(transaction.info().newVersion());
        m_versionChangeTransaction = nullptr;
        m_versionChangeDatabaseConnection = nullptr;
    }

    uint64_t callbackID = storeCallback(callback);
    m_server.postDatabaseTask(createCrossThreadTask(*this, &UniqueIDBDatabase::performCommitTransaction, callbackID, transaction.info().identifier()));
}

void UniqueIDBDatabase::performCommitTransaction(uint64_t callbackIdentifier, const IDBResourceIdentifier& transactionIdentifier)
{
    ASSERT(!isMainThread());
    LOG(IndexedDB, "(db) UniqueIDBDatabase::performCommitTransaction");

    IDBError error = m_backingStore->commitTransaction(transactionIdentifier);
    m_server.postDatabaseTaskReply(createCrossThreadTask(*this, &UniqueIDBDatabase::didPerformCommitTransaction, callbackIdentifier, error, transactionIdentifier));
}

void UniqueIDBDatabase::didPerformCommitTransaction(uint64_t callbackIdentifier, const IDBError& error, const IDBResourceIdentifier& transactionIdentifier)
{
    ASSERT(isMainThread());
    LOG(IndexedDB, "(main) UniqueIDBDatabase::didPerformCommitTransaction");

    inProgressTransactionCompleted(transactionIdentifier);

    performErrorCallback(callbackIdentifier, error);
}

void UniqueIDBDatabase::abortTransaction(UniqueIDBDatabaseTransaction& transaction, ErrorCallback callback)
{
    ASSERT(isMainThread());
    LOG(IndexedDB, "(main) UniqueIDBDatabase::abortTransaction");

    ASSERT(&transaction.databaseConnection().database() == this);

    uint64_t callbackID = storeCallback(callback);
    m_server.postDatabaseTask(createCrossThreadTask(*this, &UniqueIDBDatabase::performAbortTransaction, callbackID, transaction.info().identifier()));
}

void UniqueIDBDatabase::performAbortTransaction(uint64_t callbackIdentifier, const IDBResourceIdentifier& transactionIdentifier)
{
    ASSERT(!isMainThread());
    LOG(IndexedDB, "(db) UniqueIDBDatabase::performAbortTransaction");

    IDBError error = m_backingStore->abortTransaction(transactionIdentifier);
    m_server.postDatabaseTaskReply(createCrossThreadTask(*this, &UniqueIDBDatabase::didPerformAbortTransaction, callbackIdentifier, error, transactionIdentifier));
}

void UniqueIDBDatabase::didPerformAbortTransaction(uint64_t callbackIdentifier, const IDBError& error, const IDBResourceIdentifier& transactionIdentifier)
{
    ASSERT(isMainThread());
    LOG(IndexedDB, "(main) UniqueIDBDatabase::didPerformAbortTransaction");

    if (m_versionChangeTransaction && m_versionChangeTransaction->info().identifier() == transactionIdentifier) {
        ASSERT(&m_versionChangeTransaction->databaseConnection() == m_versionChangeDatabaseConnection);
        ASSERT(m_versionChangeTransaction->originalDatabaseInfo());
        m_databaseInfo = std::make_unique<IDBDatabaseInfo>(*m_versionChangeTransaction->originalDatabaseInfo());

        m_versionChangeTransaction = nullptr;
        m_versionChangeDatabaseConnection = nullptr;
    }

    inProgressTransactionCompleted(transactionIdentifier);

    performErrorCallback(callbackIdentifier, error);
}

void UniqueIDBDatabase::transactionDestroyed(UniqueIDBDatabaseTransaction& transaction)
{
    if (m_versionChangeTransaction == &transaction)
        m_versionChangeTransaction = nullptr;
}

void UniqueIDBDatabase::connectionClosedFromClient(UniqueIDBDatabaseConnection& connection)
{
    ASSERT(isMainThread());
    LOG(IndexedDB, "(main) UniqueIDBDatabase::connectionClosedFromClient");

    if (m_versionChangeDatabaseConnection == &connection)
        m_versionChangeDatabaseConnection = nullptr;

    ASSERT(m_openDatabaseConnections.contains(&connection));

    auto removedConnection = m_openDatabaseConnections.take(&connection);
    if (removedConnection->hasNonFinishedTransactions()) {
        m_closePendingDatabaseConnections.add(WTF::move(removedConnection));
        return;
    }

    // Now that a database connection has closed, previously blocked transactions might be runnable.
    invokeTransactionScheduler();
}

void UniqueIDBDatabase::enqueueTransaction(Ref<UniqueIDBDatabaseTransaction>&& transaction)
{
    LOG(IndexedDB, "UniqueIDBDatabase::enqueueTransaction");

    ASSERT(transaction->info().mode() != IndexedDB::TransactionMode::VersionChange);

    m_pendingTransactions.append(WTF::move(transaction));

    invokeTransactionScheduler();
}

void UniqueIDBDatabase::invokeTransactionScheduler()
{
    if (!m_transactionSchedulingTimer.isActive())
        m_transactionSchedulingTimer.startOneShot(0);
}

void UniqueIDBDatabase::transactionSchedulingTimerFired()
{
    LOG(IndexedDB, "(main) UniqueIDBDatabase::transactionSchedulingTimerFired");

    if (m_pendingTransactions.isEmpty()) {
        if (!hasAnyOpenConnections() && m_versionChangeOperation) {
            startVersionChangeTransaction();
            return;
        }
    }

    bool hadDeferredTransactions = false;
    auto transaction = takeNextRunnableTransaction(hadDeferredTransactions);

    if (transaction) {
        m_inProgressTransactions.set(transaction->info().identifier(), transaction);
        for (auto objectStore : transaction->objectStoreIdentifiers())
            m_objectStoreTransactionCounts.add(objectStore);

        activateTransactionInBackingStore(*transaction);

        // If no transactions were deferred, it's possible we can start another transaction right now.
        if (!hadDeferredTransactions)
            invokeTransactionScheduler();
    }
}

void UniqueIDBDatabase::activateTransactionInBackingStore(UniqueIDBDatabaseTransaction& transaction)
{
    LOG(IndexedDB, "(main) UniqueIDBDatabase::activateTransactionInBackingStore");

    RefPtr<UniqueIDBDatabase> self(this);
    RefPtr<UniqueIDBDatabaseTransaction> refTransaction(&transaction);

    auto callback = [this, self, refTransaction](const IDBError& error) {
        refTransaction->didActivateInBackingStore(error);
    };

    uint64_t callbackID = storeCallback(callback);
    m_server.postDatabaseTask(createCrossThreadTask(*this, &UniqueIDBDatabase::performActivateTransactionInBackingStore, callbackID, transaction.info()));
}

void UniqueIDBDatabase::performActivateTransactionInBackingStore(uint64_t callbackIdentifier, const IDBTransactionInfo& info)
{
    LOG(IndexedDB, "(db) UniqueIDBDatabase::performActivateTransactionInBackingStore");

    IDBError error = m_backingStore->beginTransaction(info);
    m_server.postDatabaseTaskReply(createCrossThreadTask(*this, &UniqueIDBDatabase::didPerformActivateTransactionInBackingStore, callbackIdentifier, error));
}

void UniqueIDBDatabase::didPerformActivateTransactionInBackingStore(uint64_t callbackIdentifier, const IDBError& error)
{
    LOG(IndexedDB, "(main) UniqueIDBDatabase::didPerformActivateTransactionInBackingStore");

    invokeTransactionScheduler();

    performErrorCallback(callbackIdentifier, error);
}

template<typename T> bool scopesOverlap(const T& aScopes, const Vector<uint64_t>& bScopes)
{
    for (auto scope : bScopes) {
        if (aScopes.contains(scope))
            return true;
    }

    return false;
}

RefPtr<UniqueIDBDatabaseTransaction> UniqueIDBDatabase::takeNextRunnableTransaction(bool& hadDeferredTransactions)
{
    Deque<RefPtr<UniqueIDBDatabaseTransaction>> deferredTransactions;
    RefPtr<UniqueIDBDatabaseTransaction> currentTransaction;

    while (!m_pendingTransactions.isEmpty()) {
        currentTransaction = m_pendingTransactions.takeFirst();

        switch (currentTransaction->info().mode()) {
        case IndexedDB::TransactionMode::ReadOnly:
            // If there are any deferred transactions, the first one is a read-write transaction we need to unblock.
            // Therefore, skip this read-only transaction if its scope overlaps with that read-write transaction.
            if (!deferredTransactions.isEmpty()) {
                ASSERT(deferredTransactions.first()->info().mode() == IndexedDB::TransactionMode::ReadWrite);
                if (scopesOverlap(deferredTransactions.first()->objectStoreIdentifiers(), currentTransaction->objectStoreIdentifiers()))
                    deferredTransactions.append(WTF::move(currentTransaction));
            }

            break;
        case IndexedDB::TransactionMode::ReadWrite:
            // If this read-write transaction's scope overlaps with running transactions, it must be deferred.
            if (scopesOverlap(m_objectStoreTransactionCounts, currentTransaction->objectStoreIdentifiers()))
                deferredTransactions.append(WTF::move(currentTransaction));

            break;
        case IndexedDB::TransactionMode::VersionChange:
            // Version change transactions should never be scheduled in the traditional manner.
            RELEASE_ASSERT_NOT_REACHED();
        }

        // If we didn't defer the currentTransaction above, it can be run now.
        if (currentTransaction)
            break;
    }

    hadDeferredTransactions = !deferredTransactions.isEmpty();
    if (!hadDeferredTransactions)
        return WTF::move(currentTransaction);

    // Prepend the deferred transactions back on the beginning of the deque for future scheduling passes.
    while (!deferredTransactions.isEmpty())
        m_pendingTransactions.prepend(deferredTransactions.takeLast());

    return WTF::move(currentTransaction);
}

void UniqueIDBDatabase::inProgressTransactionCompleted(const IDBResourceIdentifier& transactionIdentifier)
{
    auto transaction = m_inProgressTransactions.take(transactionIdentifier);
    ASSERT(transaction);

    if (m_versionChangeTransaction == transaction)
        m_versionChangeTransaction = nullptr;

    for (auto objectStore : transaction->objectStoreIdentifiers())
        m_objectStoreTransactionCounts.remove(objectStore);

    // Previously blocked transactions might now be unblocked.
    invokeTransactionScheduler();
}

void UniqueIDBDatabase::performErrorCallback(uint64_t callbackIdentifier, const IDBError& error)
{
    auto callback = m_errorCallbacks.take(callbackIdentifier);
    ASSERT(callback);
    callback(error);
}

void UniqueIDBDatabase::performKeyDataCallback(uint64_t callbackIdentifier, const IDBError& error, const IDBKeyData& resultKey)
{
    auto callback = m_keyDataCallbacks.take(callbackIdentifier);
    ASSERT(callback);
    callback(error, resultKey);
}

void UniqueIDBDatabase::performGetResultCallback(uint64_t callbackIdentifier, const IDBError& error, const IDBGetResult& resultData)
{
    auto callback = m_getResultCallbacks.take(callbackIdentifier);
    ASSERT(callback);
    callback(error, resultData);
}

void UniqueIDBDatabase::performCountCallback(uint64_t callbackIdentifier, const IDBError& error, uint64_t count)
{
    auto callback = m_countCallbacks.take(callbackIdentifier);
    ASSERT(callback);
    callback(error, count);
}

} // namespace IDBServer
} // namespace WebCore

#endif // ENABLE(INDEXED_DATABASE)
