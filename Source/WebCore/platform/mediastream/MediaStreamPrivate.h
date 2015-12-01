/*
 * Copyright (C) 2011, 2015 Ericsson AB. All rights reserved.
 * Copyright (C) 2012 Google Inc. All rights reserved.
 * Copyright (C) 2013 Nokia Corporation and/or its subsidiary(-ies).
 * Copyright (C) 2015 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name of Ericsson nor the names of its contributors
 *    may be used to endorse or promote products derived from this
 *    software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef MediaStreamPrivate_h
#define MediaStreamPrivate_h

#if ENABLE(MEDIA_STREAM)

#include "FloatSize.h"
#include "MediaStreamTrack.h"
#include "MediaStreamTrackPrivate.h"
#include <wtf/HashMap.h>
#include <wtf/MediaTime.h>
#include <wtf/RefCounted.h>
#include <wtf/RefPtr.h>
#include <wtf/Vector.h>
#include <wtf/WeakPtr.h>

namespace WebCore {

class MediaStream;

class MediaStreamPrivate : public MediaStreamTrackPrivate::Observer, public RefCounted<MediaStreamPrivate> {
public:
    class Observer {
    public:
        virtual ~Observer() { }

        virtual void characteristicsChanged() { }
        virtual void activeStatusChanged() { }
        virtual void didAddTrack(MediaStreamTrackPrivate&) { }
        virtual void didRemoveTrack(MediaStreamTrackPrivate&) { }
    };

    static RefPtr<MediaStreamPrivate> create(const Vector<RefPtr<RealtimeMediaSource>>& audioSources, const Vector<RefPtr<RealtimeMediaSource>>& videoSources);
    static RefPtr<MediaStreamPrivate> create(const MediaStreamTrackPrivateVector&);

    virtual ~MediaStreamPrivate();

    enum class NotifyClientOption { Notify, DontNotify };

    void addObserver(Observer&);
    void removeObserver(Observer&);

    String id() const { return m_id; }

    MediaStreamTrackPrivateVector tracks() const;

    bool active() const { return m_isActive; }
    void updateActiveState(NotifyClientOption);

    void addTrack(RefPtr<MediaStreamTrackPrivate>&&, NotifyClientOption = NotifyClientOption::Notify);
    void removeTrack(MediaStreamTrackPrivate&, NotifyClientOption = NotifyClientOption::Notify);

    void startProducingData();
    void stopProducingData();
    bool isProducingData() const;

    PlatformLayer* platformLayer() const;
    RefPtr<Image> currentFrameImage();
    void paintCurrentFrameInContext(GraphicsContext&, const FloatRect&);

    bool hasVideo();
    bool hasAudio();

    FloatSize intrinsicSize() const;

    WeakPtr<MediaStreamPrivate> createWeakPtr() { return m_weakPtrFactory.createWeakPtr(); }

private:
    MediaStreamPrivate(const String&, const MediaStreamTrackPrivateVector&);

    void trackEnded(MediaStreamTrackPrivate&) override { }
    void trackMutedChanged(MediaStreamTrackPrivate&) override;
    void trackStatesChanged(MediaStreamTrackPrivate&) override;
    void trackEnabledChanged(MediaStreamTrackPrivate&) override;

    void characteristicsChanged();

    void scheduleDeferredTask(std::function<void()>);

    WeakPtrFactory<MediaStreamPrivate> m_weakPtrFactory;
    Vector<Observer*> m_observers;
    String m_id;
    MediaStreamTrackPrivate* m_activeVideoTrack { nullptr };
    HashMap<String, RefPtr<MediaStreamTrackPrivate>> m_trackSet;
    bool m_isActive { false };
};

typedef Vector<RefPtr<MediaStreamPrivate>> MediaStreamPrivateVector;

} // namespace WebCore

#endif // ENABLE(MEDIA_STREAM)

#endif // MediaStreamPrivate_h
