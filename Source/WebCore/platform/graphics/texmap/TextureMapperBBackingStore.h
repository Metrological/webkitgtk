#ifndef TextureMapperBBackingSurface_h
#define TextureMapperBBackingSurface_h

#if USE(TEXTURE_MAPPER)

#include "ImageBuffer.h"
#include "TextureMapperBackingStore.h"
#include <memory>

namespace WebCore {

class BitmapTexture;
class FloatRect;
class FloatSize;
class GraphicsLayer;
class IntRect;
class TextureMapper;
class TransformationMatrix;

class TextureMapperBBackingStore : public TextureMapperBackingStore {
public:
    static PassRefPtr<TextureMapperBBackingStore> create() { return adoptRef(new TextureMapperBBackingStore); }
    virtual ~TextureMapperBBackingStore() = default;

    virtual PassRefPtr<BitmapTexture> texture() const override { return nullptr; }
    virtual void paintToTextureMapper(TextureMapper*, const FloatRect&, const TransformationMatrix&, float) override;

    void updateContents(TextureMapper*, GraphicsLayer*, const FloatSize&, const IntRect&, BitmapTexture::UpdateContentsFlag);

private:
    FloatSize m_size;
    std::unique_ptr<ImageBuffer> m_buffer;
};

} // namespace WebCore

#endif // USE(TEXTURE_MAPPER)

#endif // TextureMappingBBackingSurface_h
