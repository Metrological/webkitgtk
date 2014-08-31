#include "config.h"
#include "TextureMapperBBackingStore.h"

#if USE(TEXTURE_MAPPER)

#include "GLContext.h"
#include "GraphicsLayer.h"
#include <stdio.h>

namespace WebCore {

void TextureMapperBBackingStore::paintToTextureMapper(TextureMapper* textureMapper, const FloatRect& targetRect, const TransformationMatrix& matrix, float opacity)
{
    if (m_buffer)
        m_buffer->platformLayer()->paintToTextureMapper(textureMapper, targetRect, matrix, opacity);
}

void TextureMapperBBackingStore::updateContents(TextureMapper* textureMapper, GraphicsLayer* sourceLayer, const FloatSize& size, const IntRect& targetRect, BitmapTexture::UpdateContentsFlag)
{
    if (size != m_size) {
        m_size = size;
        m_buffer = ImageBuffer::create(size, 1, ColorSpaceDeviceRGB, Accelerated);
    }

    GraphicsContext* context = m_buffer->context();
    context->setImageInterpolationQuality(textureMapper->imageInterpolationQuality());
    context->setTextDrawingMode(textureMapper->textDrawingMode());

    sourceLayer->paintGraphicsLayerContents(*context, targetRect);
}

} // namespace WebCore

#endif
