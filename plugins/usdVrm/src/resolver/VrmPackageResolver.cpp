// SPDX-License-Identifier: Apache-2.0
//
// ArPackageResolver for texture assets embedded in .vrm / GLB containers.
//
// The file-format reader authors package-relative texture paths such as:
//     /path/avatar.vrm[images/0123456789abcdef.png]
// Hio resolves the innermost extension (png/jpg) and asks Ar to open the asset;
// this resolver parses the GLB and returns the matching embedded image bytes.

#include "pxr/pxr.h"

#include "pxr/base/tf/diagnostic.h"
#include "pxr/base/tf/registryManager.h"
#include "pxr/base/tf/type.h"
#include "pxr/usd/ar/asset.h"
#include "pxr/usd/ar/definePackageResolver.h"
#include "pxr/usd/ar/inMemoryAsset.h"
#include "pxr/usd/ar/packageResolver.h"
#include "pxr/usd/ar/resolvedPath.h"
#include "pxr/usd/ar/resolver.h"
#include "pxr/usd/ar/threadLocalScopedCache.h"

#include <vrmContainer/GlbContainer.h>

#include <cgltf.h>

#include <cstring>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>

PXR_NAMESPACE_OPEN_SCOPE

namespace
{

const char* _ImageExt(const cgltf_image* img)
{
    if (img->mime_type) {
        if (std::strcmp(img->mime_type, "image/png") == 0) return "png";
        if (std::strcmp(img->mime_type, "image/jpeg") == 0) return "jpg";
        return nullptr;
    }
    return nullptr;
}

bool _BufferViewBytes(const cgltf_buffer_view* view,
                      vrmContainer::ByteView* out)
{
    if (!view || !out) return false;
    if (view->data) {
        *out = {static_cast<const std::byte*>(view->data), view->size};
        return true;
    }
    if (!view->buffer || !view->buffer->data) return false;
    const vrmContainer::ByteView buffer(
        static_cast<const std::byte*>(view->buffer->data), view->buffer->size);
    return vrmContainer::MakeBufferView(buffer, view->offset, view->size, out);
}

const char* _SniffImageExt(vrmContainer::ByteView bytes,
                           const cgltf_image* img)
{
    const auto* data = reinterpret_cast<const unsigned char*>(bytes.data());
    if (bytes.size() >= 4 && data[0] == 0x89 && data[1] == 'P' &&
        data[2] == 'N' && data[3] == 'G') {
        return "png";
    }
    if (bytes.size() >= 3 && data[0] == 0xFF && data[1] == 0xD8 &&
        data[2] == 0xFF) {
        return "jpg";
    }
    return _ImageExt(img);
}

bool _ReadPackageBytes(const std::string& packagePath,
                       std::shared_ptr<const char>* outBuffer,
                       size_t* outSize)
{
    std::shared_ptr<ArAsset> asset =
        ArGetResolver().OpenAsset(ArResolvedPath(packagePath));
    if (!asset) {
        return false;
    }

    std::shared_ptr<const char> buffer = asset->GetBuffer();
    if (!buffer) {
        std::shared_ptr<ArAsset> detached = asset->GetDetachedAsset();
        if (!detached) {
            return false;
        }
        buffer = detached->GetBuffer();
        if (!buffer) {
            return false;
        }
        asset = std::move(detached);
    }

    *outBuffer = std::move(buffer);
    *outSize = asset->GetSize();
    return true;
}

struct _EmbeddedImage
{
    std::shared_ptr<const char> bytes;
    size_t size = 0;
};

struct _PackageIndex
{
    std::unordered_map<std::string, _EmbeddedImage> images;
};

struct _ResolverCache
{
    std::unordered_map<std::string, std::shared_ptr<_PackageIndex>> packages;
};

std::shared_ptr<const char> _CopyImageBytes(vrmContainer::ByteView bytes)
{
    std::shared_ptr<char> image(
        new char[bytes.size()], std::default_delete<char[]>());
    if (!bytes.empty()) {
        std::memcpy(image.get(), bytes.data(), bytes.size());
    }
    return image;
}

std::shared_ptr<_PackageIndex> _BuildPackageIndex(const std::string& packagePath)
{
    auto index = std::make_shared<_PackageIndex>();
    std::shared_ptr<const char> packageBuffer;
    size_t packageSize = 0;
    if (!_ReadPackageBytes(packagePath, &packageBuffer, &packageSize)) {
        return index;
    }

    const vrmContainer::ByteView packageBytes(
        reinterpret_cast<const std::byte*>(packageBuffer.get()), packageSize);
    vrmContainer::GlbView glb;
    if (!vrmContainer::ParseGlb(packageBytes, &glb)) {
        return index;
    }

    cgltf_options options = {};
    cgltf_data* data = nullptr;
    cgltf_result res =
        cgltf_parse(&options, packageBuffer.get(), packageSize, &data);
    if (res != cgltf_result_success || !data) {
        return index;
    }

    if (cgltf_load_buffers(&options, data, packagePath.c_str()) ==
        cgltf_result_success) {
        for (cgltf_size i = 0; i < data->images_count; ++i) {
            const cgltf_image* img = &data->images[i];
            if (!img->buffer_view) {
                continue;
            }

            const cgltf_buffer_view* bv = img->buffer_view;
            vrmContainer::ByteView imageBytes;
            if (!_BufferViewBytes(bv, &imageBytes)) {
                continue;
            }
            const char* ext = _SniffImageExt(imageBytes, img);
            if (!ext) {
                continue;
            }

            _EmbeddedImage image;
            image.bytes = _CopyImageBytes(imageBytes);
            image.size = imageBytes.size();
            index->images[
                vrmContainer::MakeEmbeddedResourcePath(imageBytes, ext)] =
                std::move(image);
        }
    }

    cgltf_free(data);
    return index;
}

const _EmbeddedImage* _FindEmbeddedImage(const _PackageIndex& index,
                                         const std::string& packagedPath)
{
    if (packagedPath.rfind("images/", 0) != 0) {
        return nullptr;
    }
    const auto it = index.images.find(packagedPath);
    return it == index.images.end() ? nullptr : &it->second;
}

} // namespace

class UsdVrmPackageResolver final
    : public ArPackageResolver
{
public:
    std::string Resolve(const std::string& resolvedPackagePath,
                        const std::string& packagedPath) override
    {
        std::shared_ptr<const _PackageIndex> index =
            _FindOrBuildPackageIndex(resolvedPackagePath);
        if (index && _FindEmbeddedImage(*index, packagedPath)) {
            return packagedPath;
        }
        return std::string();
    }

    std::shared_ptr<ArAsset> OpenAsset(
        const std::string& resolvedPackagePath,
        const std::string& resolvedPackagedPath) override
    {
        std::shared_ptr<const _PackageIndex> index =
            _FindOrBuildPackageIndex(resolvedPackagePath);
        const _EmbeddedImage* image = index
            ? _FindEmbeddedImage(*index, resolvedPackagedPath)
            : nullptr;
        if (!image) {
            return nullptr;
        }
        return ArInMemoryAsset::FromBuffer(image->bytes, image->size);
    }

    void BeginCacheScope(VtValue* cacheScopeData) override
    {
        _caches.BeginCacheScope(cacheScopeData);
    }

    void EndCacheScope(VtValue* cacheScopeData) override
    {
        _caches.EndCacheScope(cacheScopeData);
    }

private:
    std::shared_ptr<_PackageIndex> _FindOrBuildPackageIndex(
        const std::string& resolvedPackagePath)
    {
        _ScopedCaches::CachePtr cache = _caches.GetCurrentCache();
        if (!cache) {
            return _BuildPackageIndex(resolvedPackagePath);
        }

        auto it = cache->packages.find(resolvedPackagePath);
        if (it != cache->packages.end()) {
            return it->second;
        }

        std::shared_ptr<_PackageIndex> index =
            _BuildPackageIndex(resolvedPackagePath);
        cache->packages[resolvedPackagePath] = index;
        return index;
    }

    using _ScopedCaches = ArThreadLocalScopedCache<_ResolverCache>;
    _ScopedCaches _caches;
};

AR_DEFINE_PACKAGE_RESOLVER(UsdVrmPackageResolver, ArPackageResolver);

PXR_NAMESPACE_CLOSE_SCOPE
