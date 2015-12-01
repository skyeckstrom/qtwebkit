add_definitions(-DQUARTZCORE_DLL -DDISABLE_COREIMAGE -DDISABLE_FRONTEND -DDISABLE_IOSURFACE -DDISABLE_RENDERSERVER
    -DDISABLE_3D_TRANSFORMS -DWEBCORE_CONTEXT_MENUS -DPSAPI_VERSION=1)

list(APPEND WebCore_INCLUDE_DIRECTORIES
    "${WEBKIT_LIBRARIES_DIR}/include"
    "${WEBKIT_LIBRARIES_DIR}/include/zlib"
    "${WEBCORE_DIR}/loader/archive/cf"
    "${WEBCORE_DIR}/platform/graphics/avfoundation"
    "${WEBCORE_DIR}/platform/graphics/avfoundation/cf"
    "${WEBCORE_DIR}/platform/graphics/ca"
    "${WEBCORE_DIR}/platform/graphics/ca/win"
    "${WEBCORE_DIR}/platform/graphics/cg"
    "${WEBCORE_DIR}/platform/network/cf"
    "${WEBCORE_DIR}/platform/spi/cf"
    "${WEBCORE_DIR}/platform/spi/cg"
)

list(APPEND WebCore_SOURCES
    editing/SmartReplaceCF.cpp

    loader/cf/ResourceLoaderCFNet.cpp

    page/CaptionUserPreferencesMediaAF.cpp

    page/win/FrameCGWin.cpp

    platform/cf/CFURLExtras.cpp
    platform/cf/CoreMediaSoftLink.cpp
    platform/cf/MediaAccessibilitySoftLink.cpp
    platform/cf/URLCF.cpp

    platform/graphics/avfoundation/InbandMetadataTextTrackPrivateAVF.cpp
    platform/graphics/avfoundation/InbandTextTrackPrivateAVF.cpp
    platform/graphics/avfoundation/MediaPlayerPrivateAVFoundation.cpp
    platform/graphics/avfoundation/MediaTimeAVFoundation.cpp
    platform/graphics/avfoundation/WebMediaSessionManagerMac.cpp

    platform/graphics/avfoundation/cf/CDMSessionAVFoundationCF.cpp
    platform/graphics/avfoundation/cf/InbandTextTrackPrivateLegacyAVCF.cpp
    platform/graphics/avfoundation/cf/InbandTextTrackPrivateAVCF.cpp
    platform/graphics/avfoundation/cf/MediaPlayerPrivateAVFoundationCF.cpp
    platform/graphics/avfoundation/cf/WebCoreAVCFResourceLoader.cpp

    platform/graphics/ca/GraphicsLayerCA.cpp
    platform/graphics/ca/LayerFlushScheduler.cpp
    platform/graphics/ca/LayerPool.cpp
    platform/graphics/ca/PlatformCALayer.cpp
    platform/graphics/ca/TileController.cpp
    platform/graphics/ca/TileCoverageMap.cpp
    platform/graphics/ca/TileGrid.cpp
    platform/graphics/ca/TransformationMatrixCA.cpp

    platform/graphics/ca/win/CACFLayerTreeHost.cpp
    platform/graphics/ca/win/LayerChangesFlusher.cpp
    platform/graphics/ca/win/PlatformCAAnimationWin.cpp
    platform/graphics/ca/win/PlatformCAFiltersWin.cpp
    platform/graphics/ca/win/PlatformCALayerWin.cpp
    platform/graphics/ca/win/PlatformCALayerWinInternal.cpp
    platform/graphics/ca/win/WebTiledBackingLayerWin.cpp
    platform/graphics/ca/win/WKCACFViewLayerTreeHost.cpp

    platform/graphics/cg/BitmapImageCG.cpp
    platform/graphics/cg/ColorCG.cpp
    platform/graphics/cg/FloatPointCG.cpp
    platform/graphics/cg/FloatRectCG.cpp
    platform/graphics/cg/FloatSizeCG.cpp
    platform/graphics/cg/GradientCG.cpp
    platform/graphics/cg/GraphicsContext3DCG.cpp
    platform/graphics/cg/GraphicsContextCG.cpp
    platform/graphics/cg/IOSurfacePool.cpp
    platform/graphics/cg/ImageBufferCG.cpp
    platform/graphics/cg/ImageBufferDataCG.cpp
    platform/graphics/cg/ImageCG.cpp
    platform/graphics/cg/ImageSourceCG.cpp
    platform/graphics/cg/ImageSourceCGWin.cpp
    platform/graphics/cg/IntPointCG.cpp
    platform/graphics/cg/IntRectCG.cpp
    platform/graphics/cg/IntSizeCG.cpp
    platform/graphics/cg/PDFDocumentImage.cpp
    platform/graphics/cg/PathCG.cpp
    platform/graphics/cg/PatternCG.cpp
    platform/graphics/cg/SubimageCacheWithTimer.cpp
    platform/graphics/cg/TransformationMatrixCG.cpp

    platform/graphics/opentype/OpenTypeCG.cpp

    platform/graphics/win/FontCGWin.cpp
    platform/graphics/win/FontCustomPlatformData.cpp
    platform/graphics/win/FontPlatformDataCGWin.cpp
    platform/graphics/win/GlyphPageTreeNodeCGWin.cpp
    platform/graphics/win/GraphicsContextCGWin.cpp
    platform/graphics/win/ImageCGWin.cpp
    platform/graphics/win/SimpleFontDataCGWin.cpp

    platform/network/cf/AuthenticationCF.cpp
    platform/network/cf/CookieJarCFNet.cpp
    platform/network/cf/CookieStorageCFNet.cpp
    platform/network/cf/CredentialStorageCFNet.cpp
    platform/network/cf/DNSCFNet.cpp
    platform/network/cf/FormDataStreamCFNet.cpp
    platform/network/cf/LoaderRunLoopCF.cpp
    platform/network/cf/NetworkStorageSessionCFNet.cpp
    platform/network/cf/ProtectionSpaceCFNet.cpp
    platform/network/cf/ProxyServerCFNet.cpp
    platform/network/cf/ResourceErrorCF.cpp
    platform/network/cf/ResourceHandleCFNet.cpp
    platform/network/cf/ResourceHandleCFURLConnectionDelegate.cpp
    platform/network/cf/ResourceRequestCFNet.cpp
    platform/network/cf/ResourceResponseCFNet.cpp
    platform/network/cf/SocketStreamHandleCFNet.cpp
    platform/network/cf/SynchronousLoaderClientCFNet.cpp
    platform/network/cf/SynchronousResourceHandleCFURLConnectionDelegate.cpp

    platform/win/DragImageCGWin.cpp
)

list(APPEND WebCore_FORWARDING_HEADERS_DIRECTORIES
    platform/graphics/ca
    platform/graphics/cg

    platform/graphics/ca/win

    platform/network/cf

    platform/spi/cf
    platform/spi/cg
)
