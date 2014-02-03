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
#import "WKProcessClassInternal.h"

#if WK_API_ENABLED

#import "WKObject.h"
#import "WKProcessClassConfigurationPrivate.h"
#import "WebContext.h"
#import <wtf/RetainPtr.h>

#if PLATFORM(IOS)
#import <WebCore/WebCoreThreadSystemInterface.h>
#endif

@implementation WKProcessClass

- (instancetype)init
{
    return [self initWithConfiguration:adoptNS([[WKProcessClassConfiguration alloc] init]).get()];
}

- (instancetype)initWithConfiguration:(WKProcessClassConfiguration *)configuration
{
    if (!(self = [super init]))
        return nil;

    _configuration = adoptNS([configuration copy]);

#if PLATFORM(IOS)
    // FIXME: Remove once <rdar://problem/15256572> is fixed.
    InitWebCoreThreadSystemInterface();
#endif

    String bundlePath;
    if (NSURL *bundleURL = [_configuration _injectedBundleURL]) {
        if (!bundleURL.isFileURL)
            [NSException raise:NSInvalidArgumentException format:@"Injected Bundle URL must be a file URL"];

        bundlePath = bundleURL.path;
    }

    API::Object::constructInWrapper<WebKit::WebContext>(self, bundlePath);

    return self;
}

- (void)dealloc
{
    _context->~WebContext();

    [super dealloc];
}

- (WKProcessClassConfiguration *)configuration
{
    return [[_configuration copy] autorelease];
}

- (API::Object&)_apiObject
{
    return *_context;
}

@end

#endif // WK_API_ENABLED
