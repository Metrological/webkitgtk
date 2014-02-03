/*
 * Copyright (C) 2013, 2014 Apple Inc. All rights reserved.
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

#import "config.h"
#import "ViewGestureController.h"

#if !PLATFORM(IOS)

#import "NativeWebWheelEvent.h"
#import "WebPageGroup.h"
#import "ViewGestureControllerMessages.h"
#import "ViewGestureGeometryCollectorMessages.h"
#import "ViewSnapshotStore.h"
#import "WebBackForwardList.h"
#import "WebPageMessages.h"
#import "WebPageProxy.h"
#import "WebPreferences.h"
#import "WebProcessProxy.h"
#import <Cocoa/Cocoa.h>
#import <QuartzCore/QuartzCore.h>
#import <WebCore/WebCoreCALayerExtras.h>

#if defined(__has_include) && __has_include(<IOSurface/IOSurfacePrivate.h>)
#import <IOSurface/IOSurfacePrivate.h>
#else
enum {
    kIOSurfacePurgeableNonVolatile = 0,
    kIOSurfacePurgeableVolatile = 1,
    kIOSurfacePurgeableEmpty = 2,
};
#endif

#if __MAC_OS_X_VERSION_MIN_REQUIRED >= 1090
extern "C" IOReturn IOSurfaceSetPurgeable(IOSurfaceRef buffer, uint32_t newState, uint32_t *oldState);
#endif

#if defined(__has_include) && __has_include(<QuartzCore/QuartzCorePrivate.h>)
#import <QuartzCore/QuartzCorePrivate.h>
#else
@interface CAFilter : NSObject <NSCopying, NSMutableCopying, NSCoding>
@end
#endif

@interface CAFilter (Details)
+ (CAFilter *)filterWithType:(NSString *)type;
@end

extern NSString * const kCAFilterGaussianBlur;

using namespace WebCore;

static const double minMagnification = 1;
static const double maxMagnification = 3;

static const double minElasticMagnification = 0.75;
static const double maxElasticMagnification = 4;

static const double zoomOutBoost = 1.6;
static const double zoomOutResistance = 0.10;

static const float smartMagnificationElementPadding = 0.05;
static const float smartMagnificationPanScrollThreshold = 100;

static const double swipeOverlayShadowOpacity = 0.66;
static const double swipeOverlayShadowRadius = 3;

static const float swipeSnapshotRemovalRenderTreeSizeTargetFraction = 0.5;
static const std::chrono::seconds swipeSnapshotRemovalWatchdogDuration = 3_s;

namespace WebKit {

ViewGestureController::ViewGestureController(WebPageProxy& webPageProxy)
    : m_webPageProxy(webPageProxy)
    , m_lastSmartMagnificationUnscaledTargetRectIsValid(false)
    , m_activeGestureType(ViewGestureType::None)
    , m_visibleContentRectIsValid(false)
    , m_frameHandlesMagnificationGesture(false)
    , m_swipeTransitionStyle(SwipeTransitionStyle::Overlap)
    , m_swipeWatchdogTimer(this, &ViewGestureController::swipeSnapshotWatchdogTimerFired)
    , m_hasPendingSwipe(false)
{
    m_webPageProxy.process().addMessageReceiver(Messages::ViewGestureController::messageReceiverName(), m_webPageProxy.pageID(), *this);
}

ViewGestureController::~ViewGestureController()
{
    m_webPageProxy.process().removeMessageReceiver(Messages::ViewGestureController::messageReceiverName(), m_webPageProxy.pageID());
}

static double resistanceForDelta(double deltaScale, double currentScale)
{
    // Zoom out with slight acceleration, until we reach minimum scale.
    if (deltaScale < 0 && currentScale > minMagnification)
        return zoomOutBoost;

    // Zoom in with no acceleration, until we reach maximum scale.
    if (deltaScale > 0 && currentScale < maxMagnification)
        return 1;

    // Outside of the extremes, resist further scaling.
    double limit = currentScale < minMagnification ? minMagnification : maxMagnification;
    double scaleDistance = abs(limit - currentScale);
    double scalePercent = std::min(std::max(scaleDistance / limit, 0.), 1.);
    double resistance = zoomOutResistance + scalePercent * (0.01 - zoomOutResistance);

    return resistance;
}

FloatPoint ViewGestureController::scaledMagnificationOrigin(FloatPoint origin, double scale)
{
    FloatPoint scaledMagnificationOrigin(origin);
    scaledMagnificationOrigin.moveBy(m_visibleContentRect.location());
    float magnificationOriginScale = 1 - (scale / m_webPageProxy.pageScaleFactor());
    scaledMagnificationOrigin.scale(magnificationOriginScale, magnificationOriginScale);
    return scaledMagnificationOrigin;
}

void ViewGestureController::didCollectGeometryForMagnificationGesture(FloatRect visibleContentRect, bool frameHandlesMagnificationGesture)
{
    m_activeGestureType = ViewGestureType::Magnification;
    m_visibleContentRect = visibleContentRect;
    m_visibleContentRectIsValid = true;
    m_frameHandlesMagnificationGesture = frameHandlesMagnificationGesture;
}

void ViewGestureController::handleMagnificationGesture(double scale, FloatPoint origin)
{
    ASSERT(m_activeGestureType == ViewGestureType::None || m_activeGestureType == ViewGestureType::Magnification);

    if (m_activeGestureType == ViewGestureType::None) {
        // FIXME: We drop the first frame of the gesture on the floor, because we don't have the visible content bounds yet.
        m_magnification = m_webPageProxy.pageScaleFactor();
        m_webPageProxy.process().send(Messages::ViewGestureGeometryCollector::CollectGeometryForMagnificationGesture(), m_webPageProxy.pageID());

        return;
    }

    // We're still waiting for the DidCollectGeometry callback.
    if (!m_visibleContentRectIsValid)
        return;

    m_activeGestureType = ViewGestureType::Magnification;

    double scaleWithResistance = resistanceForDelta(scale, m_magnification) * scale;

    m_magnification += m_magnification * scaleWithResistance;
    m_magnification = std::min(std::max(m_magnification, minElasticMagnification), maxElasticMagnification);

    m_magnificationOrigin = origin;

    if (m_frameHandlesMagnificationGesture)
        m_webPageProxy.scalePage(m_magnification, roundedIntPoint(origin));
    else
        m_webPageProxy.drawingArea()->adjustTransientZoom(m_magnification, scaledMagnificationOrigin(origin, m_magnification));
}

void ViewGestureController::endMagnificationGesture()
{
    ASSERT(m_activeGestureType == ViewGestureType::Magnification);

    double newMagnification = std::min(std::max(m_magnification, minMagnification), maxMagnification);

    if (m_frameHandlesMagnificationGesture)
        m_webPageProxy.scalePage(newMagnification, roundedIntPoint(m_magnificationOrigin));
    else
        m_webPageProxy.drawingArea()->commitTransientZoom(newMagnification, scaledMagnificationOrigin(m_magnificationOrigin, newMagnification));

    m_activeGestureType = ViewGestureType::None;
}

void ViewGestureController::handleSmartMagnificationGesture(FloatPoint origin)
{
    if (m_activeGestureType != ViewGestureType::None)
        return;

    m_webPageProxy.process().send(Messages::ViewGestureGeometryCollector::CollectGeometryForSmartMagnificationGesture(origin), m_webPageProxy.pageID());
}

static float maximumRectangleComponentDelta(FloatRect a, FloatRect b)
{
    return std::max(fabs(a.x() - b.x()), std::max(fabs(a.y() - b.y()), std::max(fabs(a.width() - b.width()), fabs(a.height() - b.height()))));
}

void ViewGestureController::didCollectGeometryForSmartMagnificationGesture(FloatPoint origin, FloatRect renderRect, FloatRect visibleContentRect, bool isReplacedElement, bool frameHandlesMagnificationGesture)
{
    if (frameHandlesMagnificationGesture)
        return;

    double currentScaleFactor = m_webPageProxy.pageScaleFactor();

    FloatRect unscaledTargetRect = renderRect;
    unscaledTargetRect.scale(1 / currentScaleFactor);
    unscaledTargetRect.inflateX(unscaledTargetRect.width() * smartMagnificationElementPadding);
    unscaledTargetRect.inflateY(unscaledTargetRect.height() * smartMagnificationElementPadding);

    double targetMagnification = visibleContentRect.width() / unscaledTargetRect.width();

    // For replaced elements like images, we want to fit the whole element
    // in the view, so scale it down enough to make both dimensions fit if possible.
    if (isReplacedElement)
        targetMagnification = std::min(targetMagnification, static_cast<double>(visibleContentRect.height() / unscaledTargetRect.height()));

    targetMagnification = std::min(std::max(targetMagnification, minMagnification), maxMagnification);

    // Allow panning between elements via double-tap while magnified, unless the target rect is
    // similar to the last one, in which case we'll zoom all the way out.
    if (currentScaleFactor > 1
        && m_lastSmartMagnificationUnscaledTargetRectIsValid
        && maximumRectangleComponentDelta(m_lastSmartMagnificationUnscaledTargetRect, unscaledTargetRect) < smartMagnificationPanScrollThreshold)
        targetMagnification = 1;

    FloatRect targetRect(unscaledTargetRect);
    targetRect.scale(targetMagnification);
    FloatPoint targetOrigin(visibleContentRect.center());
    targetOrigin.moveBy(-targetRect.center());

    m_webPageProxy.drawingArea()->adjustTransientZoom(m_webPageProxy.pageScaleFactor(), scaledMagnificationOrigin(FloatPoint(), m_webPageProxy.pageScaleFactor()));
    m_webPageProxy.drawingArea()->commitTransientZoom(targetMagnification, targetOrigin);

    m_lastSmartMagnificationUnscaledTargetRect = unscaledTargetRect;
    m_lastSmartMagnificationUnscaledTargetRectIsValid = true;
}

bool ViewGestureController::handleScrollWheelEvent(NSEvent *event)
{
    if (m_activeGestureType != ViewGestureType::None)
        return false;

    if (event.phase != NSEventPhaseBegan)
        return false;

    m_hasPendingSwipe = false;

    if (fabs(event.scrollingDeltaX) < fabs(event.scrollingDeltaY))
        return false;

    bool willSwipeLeft = event.scrollingDeltaX > 0 && m_webPageProxy.isPinnedToLeftSide() && m_webPageProxy.backForwardList().backItem();
    bool willSwipeRight = event.scrollingDeltaX < 0 && m_webPageProxy.isPinnedToRightSide() && m_webPageProxy.backForwardList().forwardItem();
    if (!willSwipeLeft && !willSwipeRight)
        return false;

    SwipeDirection direction = willSwipeLeft ? SwipeDirection::Left : SwipeDirection::Right;

    if (!event.hasPreciseScrollingDeltas)
        return false;

    if (![NSEvent isSwipeTrackingFromScrollEventsEnabled])
        return false;

    if (m_webPageProxy.willHandleHorizontalScrollEvents()) {
        m_hasPendingSwipe = true;
        m_pendingSwipeDirection = direction;
        return false;
    }

    trackSwipeGesture(event, direction);

    return true;
}

void ViewGestureController::wheelEventWasNotHandledByWebCore(NSEvent *event)
{
    if (!m_hasPendingSwipe)
        return;

    m_hasPendingSwipe = false;
    trackSwipeGesture(event, m_pendingSwipeDirection);
}

void ViewGestureController::trackSwipeGesture(NSEvent *event, SwipeDirection direction)
{
    ViewSnapshotStore::shared().recordSnapshot(m_webPageProxy);

    CGFloat maxProgress = (direction == SwipeDirection::Left) ? 1 : 0;
    CGFloat minProgress = (direction == SwipeDirection::Right) ? -1 : 0;
    RefPtr<WebBackForwardListItem> targetItem = (direction == SwipeDirection::Left) ? m_webPageProxy.backForwardList().backItem() : m_webPageProxy.backForwardList().forwardItem();
    __block bool swipeCancelled = false;

    [event trackSwipeEventWithOptions:0 dampenAmountThresholdMin:minProgress max:maxProgress usingHandler:^(CGFloat progress, NSEventPhase phase, BOOL isComplete, BOOL *stop) {
        if (phase == NSEventPhaseBegan)
            this->beginSwipeGesture(targetItem.get(), direction);
        CGFloat clampedProgress = std::min(std::max(progress, minProgress), maxProgress);
        this->handleSwipeGesture(targetItem.get(), clampedProgress, direction);
        if (phase == NSEventPhaseCancelled)
            swipeCancelled = true;
        if (isComplete)
            this->endSwipeGesture(targetItem.get(), swipeCancelled);
    }];
}

void ViewGestureController::beginSwipeGesture(WebBackForwardListItem* targetItem, SwipeDirection direction)
{
    m_activeGestureType = ViewGestureType::Swipe;

    CALayer *rootLayer = m_webPageProxy.acceleratedCompositingRootLayer();

    m_swipeSnapshotLayer = adoptNS([[CALayer alloc] init]);
    [m_swipeSnapshotLayer setBackgroundColor:CGColorGetConstantColor(kCGColorWhite)];

    RetainPtr<IOSurfaceRef> snapshot = ViewSnapshotStore::shared().snapshotAndRenderTreeSize(targetItem).first;

    if (snapshot) {
#if __MAC_OS_X_VERSION_MIN_REQUIRED >= 1090
        uint32_t purgeabilityState = kIOSurfacePurgeableNonVolatile;
        IOSurfaceSetPurgeable(snapshot.get(), kIOSurfacePurgeableNonVolatile, &purgeabilityState);

        if (purgeabilityState != kIOSurfacePurgeableEmpty)
            [m_swipeSnapshotLayer setContents:(id)snapshot.get()];
#else
        [m_swipeSnapshotLayer setContents:(id)snapshot.get()];
#endif
    }

    [m_swipeSnapshotLayer setContentsGravity:kCAGravityTopLeft];
    [m_swipeSnapshotLayer setContentsScale:m_webPageProxy.deviceScaleFactor()];
    [m_swipeSnapshotLayer setFrame:rootLayer.frame];
    [m_swipeSnapshotLayer setAnchorPoint:CGPointZero];
    [m_swipeSnapshotLayer setPosition:CGPointZero];
    [m_swipeSnapshotLayer setName:@"Gesture Swipe Snapshot Layer"];
    [m_swipeSnapshotLayer web_disableAllActions];

    if (m_webPageProxy.pageGroup().preferences()->viewGestureDebuggingEnabled()) {
        CAFilter* filter = [CAFilter filterWithType:kCAFilterGaussianBlur];
        [filter setValue:[NSNumber numberWithFloat:3] forKey:@"inputRadius"];
        [m_swipeSnapshotLayer setFilters:@[filter]];
    }

    if (m_swipeTransitionStyle == SwipeTransitionStyle::Overlap) {
        RetainPtr<CGPathRef> shadowPath = adoptCF(CGPathCreateWithRect([rootLayer bounds], 0));

        [m_swipeSnapshotLayer setShadowColor:CGColorGetConstantColor(kCGColorBlack)];
        [m_swipeSnapshotLayer setShadowOpacity:swipeOverlayShadowOpacity];
        [m_swipeSnapshotLayer setShadowRadius:swipeOverlayShadowRadius];
        [m_swipeSnapshotLayer setShadowPath:shadowPath.get()];

        [rootLayer setShadowColor:CGColorGetConstantColor(kCGColorBlack)];
        [rootLayer setShadowOpacity:swipeOverlayShadowOpacity];
        [rootLayer setShadowRadius:swipeOverlayShadowRadius];
        [rootLayer setShadowPath:shadowPath.get()];
    }

    if (direction == SwipeDirection::Left)
        [rootLayer.superlayer insertSublayer:m_swipeSnapshotLayer.get() below:rootLayer];
    else
        [rootLayer.superlayer insertSublayer:m_swipeSnapshotLayer.get() above:rootLayer];
}

void ViewGestureController::handleSwipeGesture(WebBackForwardListItem* targetItem, double progress, SwipeDirection direction)
{
    ASSERT(m_activeGestureType == ViewGestureType::Swipe);

    CALayer *rootLayer = m_webPageProxy.acceleratedCompositingRootLayer();
    double width = rootLayer.bounds.size.width;
    double swipingLayerOffset = floor(width * progress);

    if (m_swipeTransitionStyle == SwipeTransitionStyle::Overlap) {
        if (direction == SwipeDirection::Left)
            [rootLayer setPosition:CGPointMake(swipingLayerOffset, 0)];
        else
            [m_swipeSnapshotLayer setPosition:CGPointMake(width + swipingLayerOffset, 0)];
    } else if (m_swipeTransitionStyle == SwipeTransitionStyle::Push) {
        [rootLayer setPosition:CGPointMake(swipingLayerOffset, 0)];
        [m_swipeSnapshotLayer setPosition:CGPointMake((direction == SwipeDirection::Left ? -width : width) + swipingLayerOffset, 0)];
    }
}

void ViewGestureController::endSwipeGesture(WebBackForwardListItem* targetItem, bool cancelled)
{
    ASSERT(m_activeGestureType == ViewGestureType::Swipe);

    CALayer *rootLayer = m_webPageProxy.acceleratedCompositingRootLayer();

    [rootLayer setShadowOpacity:0];
    [rootLayer setShadowRadius:0];

    if (cancelled) {
        removeSwipeSnapshot();
        return;
    }

    uint64_t renderTreeSize = ViewSnapshotStore::shared().snapshotAndRenderTreeSize(targetItem).second;
    m_webPageProxy.process().send(Messages::ViewGestureGeometryCollector::SetRenderTreeSizeNotificationThreshold(renderTreeSize * swipeSnapshotRemovalRenderTreeSizeTargetFraction), m_webPageProxy.pageID());

    // We don't want to replace the current back-forward item's snapshot
    // like we normally would when going back or forward, because we are
    // displaying the destination item's snapshot.
    ViewSnapshotStore::shared().disableSnapshotting();
    m_webPageProxy.goToBackForwardItem(targetItem);
    ViewSnapshotStore::shared().enableSnapshotting();

    if (!renderTreeSize) {
        removeSwipeSnapshot();
        return;
    }

    m_swipeWatchdogTimer.startOneShot(swipeSnapshotRemovalWatchdogDuration.count());
}

void ViewGestureController::didHitRenderTreeSizeThreshold()
{
    removeSwipeSnapshot();
}

void ViewGestureController::swipeSnapshotWatchdogTimerFired(WebCore::Timer<ViewGestureController>*)
{
    removeSwipeSnapshot();
}

void ViewGestureController::removeSwipeSnapshot()
{
    m_swipeWatchdogTimer.stop();

    if (m_activeGestureType != ViewGestureType::Swipe)
        return;

#if __MAC_OS_X_VERSION_MIN_REQUIRED >= 1090
    IOSurfaceRef snapshotSurface = (IOSurfaceRef)[m_swipeSnapshotLayer contents];
    if (snapshotSurface)
        IOSurfaceSetPurgeable(snapshotSurface, kIOSurfacePurgeableVolatile, nullptr);
#endif

    [m_webPageProxy.acceleratedCompositingRootLayer() setPosition:CGPointZero];
    [m_swipeSnapshotLayer removeFromSuperlayer];
    m_swipeSnapshotLayer = nullptr;

    m_activeGestureType = ViewGestureType::None;
}

void ViewGestureController::endActiveGesture()
{
    if (m_activeGestureType == ViewGestureType::Magnification) {
        endMagnificationGesture();
        m_visibleContentRectIsValid = false;
    }
}

double ViewGestureController::magnification() const
{
    if (m_activeGestureType == ViewGestureType::Magnification)
        return m_magnification;

    return m_webPageProxy.pageScaleFactor();
}

} // namespace WebKit

#endif // !PLATFORM(IOS)
