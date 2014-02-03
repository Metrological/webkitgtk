/*
* Copyright (C) 2012 Adobe Systems Incorporated. All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions
* are met:
*
* 1. Redistributions of source code must retain the above
*    copyright notice, this list of conditions and the following
*    disclaimer.
* 2. Redistributions in binary form must reproduce the above
*    copyright notice, this list of conditions and the following
*    disclaimer in the documentation and/or other materials
*    provided with the distribution.
*
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDER "AS IS" AND ANY
* EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
* IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
* PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER BE
* LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY,
* OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
* PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
* PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
* THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
* TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
* THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
* SUCH DAMAGE.
*/

#ifndef ShapeInfo_h
#define ShapeInfo_h

#if ENABLE(CSS_SHAPES)

#include "FloatRect.h"
#include "LayoutUnit.h"
#include "RenderStyle.h"
#include "Shape.h"
#include "ShapeValue.h"
#include <wtf/OwnPtr.h>
#include <wtf/Vector.h>

namespace WebCore {

template<class KeyType, class InfoType>
class MappedInfo {
public:
    static InfoType& ensureInfo(const KeyType& key)
    {
        InfoMap& infoMap = MappedInfo<KeyType, InfoType>::infoMap();
        if (InfoType* info = infoMap.get(&key))
            return *info;
        typename InfoMap::AddResult result = infoMap.add(&key, std::make_unique<InfoType>(key));
        return *result.iterator->value;
    }
    static void removeInfo(const KeyType& key) { infoMap().remove(&key); }
    static InfoType* info(const KeyType& key) { return infoMap().get(&key); }

private:
    typedef HashMap<const KeyType*, std::unique_ptr<InfoType>> InfoMap;
    static InfoMap& infoMap()
    {
        DEFINE_STATIC_LOCAL(InfoMap, staticInfoMap, ());
        return staticInfoMap;
    }
};

template<class RenderType>
class ShapeInfo {
    WTF_MAKE_FAST_ALLOCATED;
public:
    virtual ~ShapeInfo() { }

    void setShapeSize(LayoutUnit logicalWidth, LayoutUnit logicalHeight)
    {
        LayoutBox box = resolvedLayoutBox();
        switch (box) {
        case MarginBox:
            logicalHeight += m_renderer.marginLogicalHeight();
            logicalWidth += m_renderer.marginLogicalWidth();
            break;
        case BorderBox:
            break;
        case PaddingBox:
            logicalHeight -= m_renderer.borderLogicalHeight();
            logicalWidth -= m_renderer.borderLogicalWidth();
            break;
        case ContentBox:
            logicalHeight -= m_renderer.borderAndPaddingLogicalHeight();
            logicalWidth -= m_renderer.borderAndPaddingLogicalWidth();
            break;
        case BoundingBox:
        case BoxMissing:
            ASSERT_NOT_REACHED();
            break;
        }

        LayoutSize newLogicalSize(logicalWidth, logicalHeight);
        if (m_shapeLogicalSize == newLogicalSize)
            return;
        dirtyShapeSize();
        m_shapeLogicalSize = newLogicalSize;
    }

    SegmentList computeSegmentsForLine(LayoutUnit lineTop, LayoutUnit lineHeight) const;

    LayoutUnit shapeLogicalTop() const { return computedShapeLogicalBoundingBox().y() + logicalTopOffset(); }
    LayoutUnit shapeLogicalBottom() const { return computedShapeLogicalBoundingBox().maxY() + logicalTopOffset(); }
    LayoutUnit shapeLogicalLeft() const { return computedShapeLogicalBoundingBox().x() + logicalLeftOffset(); }
    LayoutUnit shapeLogicalRight() const { return computedShapeLogicalBoundingBox().maxX() + logicalLeftOffset(); }
    LayoutUnit shapeLogicalWidth() const { return computedShapeLogicalBoundingBox().width(); }
    LayoutUnit shapeLogicalHeight() const { return computedShapeLogicalBoundingBox().height(); }

    LayoutUnit logicalLineTop() const { return m_shapeLineTop + logicalTopOffset(); }
    LayoutUnit logicalLineBottom() const { return m_shapeLineTop + m_lineHeight + logicalTopOffset(); }
    LayoutUnit logicalLineBottom(LayoutUnit lineHeight) const { return m_shapeLineTop + lineHeight + logicalTopOffset(); }

    LayoutUnit shapeContainingBlockLogicalHeight() const { return (m_renderer.style().boxSizing() == CONTENT_BOX) ? (m_shapeLogicalSize.height() + m_renderer.borderAndPaddingLogicalHeight()) : m_shapeLogicalSize.height(); }

    virtual bool lineOverlapsShapeBounds() const = 0;

    void dirtyShapeSize() { m_shape.clear(); }
    bool shapeSizeDirty() { return !m_shape.get(); }
    const RenderType& owner() const { return m_renderer; }
    LayoutSize shapeSize() const { return m_shapeLogicalSize; }

    LayoutRect computedShapePhysicalBoundingBox() const
    {
        LayoutRect physicalBoundingBox = computedShapeLogicalBoundingBox();
        physicalBoundingBox.setX(physicalBoundingBox.x() + logicalLeftOffset());
        physicalBoundingBox.setY(physicalBoundingBox.y() + logicalTopOffset());
        if (m_renderer.style().isFlippedBlocksWritingMode())
            physicalBoundingBox.setY(m_renderer.logicalHeight() - physicalBoundingBox.maxY());
        if (!m_renderer.style().isHorizontalWritingMode())
            physicalBoundingBox = physicalBoundingBox.transposedRect();
        return physicalBoundingBox;
    }

    FloatPoint shapeToRendererPoint(FloatPoint point) const
    {
        FloatPoint result = FloatPoint(point.x() + logicalLeftOffset(), point.y() + logicalTopOffset());
        if (m_renderer.style().isFlippedBlocksWritingMode())
            result.setY(m_renderer.logicalHeight() - result.y());
        if (!m_renderer.style().isHorizontalWritingMode())
            result = result.transposedPoint();
        return result;
    }

    FloatSize shapeToRendererSize(FloatSize size) const
    {
        if (!m_renderer.style().isHorizontalWritingMode())
            return size.transposedSize();
        return size;
    }

    const Shape& computedShape() const;

protected:
    explicit ShapeInfo(const RenderType& renderer)
        : m_renderer(renderer)
    {
    }

    virtual LayoutBox resolvedLayoutBox() const = 0;
    virtual LayoutRect computedShapeLogicalBoundingBox() const = 0;
    virtual ShapeValue* shapeValue() const = 0;
    virtual void getIntervals(LayoutUnit, LayoutUnit, SegmentList&) const = 0;

    virtual WritingMode writingMode() const { return m_renderer.style().writingMode(); }

    LayoutUnit logicalTopOffset() const
    {
        LayoutBox box = resolvedLayoutBox();
        switch (box) {
        case MarginBox: return -m_renderer.marginBefore();
        case BorderBox: return LayoutUnit();
        case PaddingBox: return m_renderer.borderBefore();
        case ContentBox: return m_renderer.borderAndPaddingBefore();
        case BoundingBox: break;
        case BoxMissing: break;
        }
        ASSERT_NOT_REACHED();
        return LayoutUnit();
    }

    LayoutUnit logicalLeftOffset() const
    {
        if (m_renderer.isRenderRegion())
            return LayoutUnit();
        LayoutBox box = resolvedLayoutBox();
        switch (box) {
        case MarginBox: return -m_renderer.marginStart();
        case BorderBox: return LayoutUnit();
        case PaddingBox: return m_renderer.borderStart();
        case ContentBox: return m_renderer.borderAndPaddingStart();
        case BoundingBox: break;
        case BoxMissing: break;
        }
        ASSERT_NOT_REACHED();
        return LayoutUnit();
    }

    LayoutUnit m_shapeLineTop;
    LayoutUnit m_lineHeight;

    const RenderType& m_renderer;

private:
    mutable OwnPtr<Shape> m_shape;
    LayoutSize m_shapeLogicalSize;
};

bool checkShapeImageOrigin(Document&, CachedImage&);

}
#endif
#endif
