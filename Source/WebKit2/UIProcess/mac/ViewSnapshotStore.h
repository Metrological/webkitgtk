/*
 * Copyright (C) 2014 Apple Inc. All rights reserved.
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

#ifndef ViewSnapshotStore_h
#define ViewSnapshotStore_h

#if !PLATFORM(IOS)

#include <chrono>
#include <wtf/HashMap.h>
#include <wtf/Noncopyable.h>
#include <wtf/RetainPtr.h>
#include <wtf/text/WTFString.h>

namespace WebKit {

class WebBackForwardListItem;
class WebPageProxy;

class ViewSnapshotStore {
    WTF_MAKE_NONCOPYABLE(ViewSnapshotStore);
public:
    ViewSnapshotStore();
    ~ViewSnapshotStore();

    static ViewSnapshotStore& shared();

    void recordSnapshot(WebPageProxy&);
    std::pair<RetainPtr<IOSurfaceRef>, uint64_t> snapshotAndRenderTreeSize(WebBackForwardListItem*);

    void disableSnapshotting() { m_enabled = false; }
    void enableSnapshotting() { m_enabled = true; }

private:
    void pruneSnapshots(WebPageProxy&);

    struct Snapshot {
        RetainPtr<IOSurfaceRef> surface;
        RetainPtr<CGContextRef> surfaceContext;

        std::chrono::steady_clock::time_point creationTime;
    };

    HashMap<String, Snapshot> m_snapshotMap;
    HashMap<String, uint64_t> m_renderTreeSizeMap;

    bool m_enabled;
};

} // namespace WebKit

#endif // !PLATFORM(IOS)

#endif // ViewSnapshotStore_h
