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

#import "config.h"
#import "ScrollingTreeScrollingNodeIOS.h"

#if ENABLE(ASYNC_SCROLLING)

#import "FrameView.h"
#import "ScrollingCoordinator.h"
#import "ScrollingTree.h"
#import "ScrollingStateTree.h"
#import "Settings.h"
#import "TileController.h"
#import "WebLayer.h"

#import <QuartzCore/QuartzCore.h>

namespace WebCore {

PassOwnPtr<ScrollingTreeScrollingNode> ScrollingTreeScrollingNodeIOS::create(ScrollingTree& scrollingTree, ScrollingNodeID nodeID)
{
    return adoptPtr(new ScrollingTreeScrollingNodeIOS(scrollingTree, nodeID));
}

ScrollingTreeScrollingNodeIOS::ScrollingTreeScrollingNodeIOS(ScrollingTree& scrollingTree, ScrollingNodeID nodeID)
    : ScrollingTreeScrollingNode(scrollingTree, nodeID)
{
}

ScrollingTreeScrollingNodeIOS::~ScrollingTreeScrollingNodeIOS()
{
}

void ScrollingTreeScrollingNodeIOS::updateBeforeChildren(const ScrollingStateNode& stateNode)
{
    ScrollingTreeScrollingNode::updateBeforeChildren(stateNode);
    const auto& scrollingStateNode = toScrollingStateScrollingNode(stateNode);

    if (scrollingStateNode.hasChangedProperty(ScrollingStateNode::ScrollLayer))
        m_scrollLayer = scrollingStateNode.layer();

    if (scrollingStateNode.hasChangedProperty(ScrollingStateScrollingNode::CounterScrollingLayer))
        m_counterScrollingLayer = scrollingStateNode.counterScrollingLayer();

    if (scrollingStateNode.hasChangedProperty(ScrollingStateScrollingNode::HeaderLayer))
        m_headerLayer = scrollingStateNode.headerLayer();

    if (scrollingStateNode.hasChangedProperty(ScrollingStateScrollingNode::FooterLayer))
        m_footerLayer = scrollingStateNode.footerLayer();

    if (scrollingStateNode.hasChangedProperty(ScrollingStateScrollingNode::ReasonsForSynchronousScrolling)) {
        if (shouldUpdateScrollLayerPositionSynchronously()) {
            // We're transitioning to the slow "update scroll layer position on the main thread" mode.
            // Initialize the probable main thread scroll position with the current scroll layer position.
            if (scrollingStateNode.hasChangedProperty(ScrollingStateScrollingNode::RequestedScrollPosition))
                m_probableMainThreadScrollPosition = scrollingStateNode.requestedScrollPosition();
            else {
                CGPoint scrollLayerPosition = m_scrollLayer.get().position;
                m_probableMainThreadScrollPosition = IntPoint(-scrollLayerPosition.x, -scrollLayerPosition.y);
            }
        }
    }
}

void ScrollingTreeScrollingNodeIOS::updateAfterChildren(const ScrollingStateNode& stateNode)
{
    ScrollingTreeScrollingNode::updateAfterChildren(stateNode);

    const auto& scrollingStateNode = toScrollingStateScrollingNode(stateNode);

    // Update the scroll position after child nodes have been updated, because they need to have updated their constraints before any scrolling happens.
    if (scrollingStateNode.hasChangedProperty(ScrollingStateScrollingNode::RequestedScrollPosition))
        setScrollPosition(scrollingStateNode.requestedScrollPosition());
}

IntPoint ScrollingTreeScrollingNodeIOS::scrollPosition() const
{
    if (shouldUpdateScrollLayerPositionSynchronously())
        return m_probableMainThreadScrollPosition;

    CGPoint scrollLayerPosition = m_scrollLayer.get().position;
    return IntPoint(-scrollLayerPosition.x + scrollOrigin().x(), -scrollLayerPosition.y + scrollOrigin().y());
}

void ScrollingTreeScrollingNodeIOS::setScrollPosition(const IntPoint& scrollPosition)
{
    IntPoint newScrollPosition = scrollPosition;
    newScrollPosition = newScrollPosition.shrunkTo(maximumScrollPosition());
    newScrollPosition = newScrollPosition.expandedTo(minimumScrollPosition());

    setScrollPositionWithoutContentEdgeConstraints(newScrollPosition);
}

void ScrollingTreeScrollingNodeIOS::setScrollPositionWithoutContentEdgeConstraints(const IntPoint& scrollPosition)
{
    if (shouldUpdateScrollLayerPositionSynchronously()) {
        m_probableMainThreadScrollPosition = scrollPosition;
        scrollingTree().scrollingTreeNodeDidScroll(scrollingNodeID(), scrollPosition, SetScrollingLayerPosition);
        return;
    }

    setScrollLayerPosition(scrollPosition);
    scrollingTree().scrollingTreeNodeDidScroll(scrollingNodeID(), scrollPosition);
}

void ScrollingTreeScrollingNodeIOS::setScrollLayerPosition(const IntPoint& position)
{
    ASSERT(!shouldUpdateScrollLayerPositionSynchronously());
    m_scrollLayer.get().position = CGPointMake(-position.x() + scrollOrigin().x(), -position.y() + scrollOrigin().y());

    ScrollBehaviorForFixedElements behaviorForFixed = scrollBehaviorForFixedElements();
    IntPoint scrollOffset = position - toIntSize(scrollOrigin());
    IntSize scrollOffsetForFixedChildren = FrameView::scrollOffsetForFixedPosition(viewportRect(), totalContentsSize(), scrollOffset, scrollOrigin(), frameScaleFactor(), false, behaviorForFixed, headerHeight(), footerHeight());
    if (m_counterScrollingLayer)
        m_counterScrollingLayer.get().position = FloatPoint(scrollOffsetForFixedChildren);

    // Generally the banners should have the same horizontal-position computation as a fixed element. However,
    // the banners are not affected by the frameScaleFactor(), so if there is currently a non-1 frameScaleFactor()
    // then we should recompute scrollOffsetForFixedChildren for the banner with a scale factor of 1.
    float horizontalScrollOffsetForBanner = scrollOffsetForFixedChildren.width();
    if (frameScaleFactor() != 1)
        horizontalScrollOffsetForBanner = FrameView::scrollOffsetForFixedPosition(viewportRect(), totalContentsSize(), scrollOffset, scrollOrigin(), 1, false, behaviorForFixed, headerHeight(), footerHeight()).width();

    if (m_headerLayer)
        m_headerLayer.get().position = FloatPoint(horizontalScrollOffsetForBanner, 0);

    if (m_footerLayer)
        m_footerLayer.get().position = FloatPoint(horizontalScrollOffsetForBanner, totalContentsSize().height() - footerHeight());

    if (!m_children)
        return;

    IntRect viewportRect = this->viewportRect();
    viewportRect.setLocation(IntPoint(scrollOffsetForFixedChildren));

    size_t size = m_children->size();
    for (size_t i = 0; i < size; ++i)
        m_children->at(i)->parentScrollPositionDidChange(viewportRect, FloatSize());
}

IntPoint ScrollingTreeScrollingNodeIOS::minimumScrollPosition() const
{
    IntPoint position;
    
    if (scrollingTree().rootNode() == this && scrollingTree().scrollPinningBehavior() == PinToBottom)
        position.setY(maximumScrollPosition().y());

    return position;
}

IntPoint ScrollingTreeScrollingNodeIOS::maximumScrollPosition() const
{
    IntPoint position(totalContentsSizeForRubberBand().width() - viewportRect().width(),
                      totalContentsSizeForRubberBand().height() - viewportRect().height());

    position.clampNegativeToZero();

    if (scrollingTree().rootNode() == this && scrollingTree().scrollPinningBehavior() == PinToTop)
        position.setY(minimumScrollPosition().y());

    return position;
}

void ScrollingTreeScrollingNodeIOS::scrollBy(const IntSize& offset)
{
    setScrollPosition(scrollPosition() + offset);
}

void ScrollingTreeScrollingNodeIOS::scrollByWithoutContentEdgeConstraints(const IntSize& offset)
{
    setScrollPositionWithoutContentEdgeConstraints(scrollPosition() + offset);
}

} // namespace WebCore

#endif // ENABLE(ASYNC_SCROLLING)
