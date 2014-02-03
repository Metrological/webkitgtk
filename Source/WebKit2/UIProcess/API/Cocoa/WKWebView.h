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

#import <Foundation/Foundation.h>
#import <WebKit2/WKFoundation.h>

#if WK_API_ENABLED

#if TARGET_OS_IPHONE
#import <UIKit/UIKit.h>
#else
#import <AppKit/AppKit.h>
#endif

@class WKWebViewConfiguration;

/*!
 A @link WKWebView @/link displays interactive Web content.
 @helperclass @link WKWebViewConfiguration @/link
 Used to configure @link WKWebView @/link instances.
 */
#if TARGET_OS_IPHONE
WK_API_CLASS
@interface WKWebView : UIView
#else
WK_API_CLASS
@interface WKWebView : NSView
#endif

/*! @abstract A copy of the configuration with which the @link WKWebView @/link was initialized. */
@property (nonatomic, readonly) WKWebViewConfiguration *configuration;

/*! @abstract Returns a view initialized with the specified frame and configuration.
 @param frame The frame for the new view.
 @param configuration The configuration for the new view.
 @result An initialized view, or nil if the object could not be initialized.
 @discussion This is a designated initializer. You can use @link -initWithFrame: @/link to
 initialize an instance with the default configuration.

 The initializer copies
 @link //apple_ref/doc/methodparam/WKWebView/initWithFrame:configuration:/configuration
 configuration@/link, so mutating it after initialization has no effect on the
 @link WKWebView @/link instance.
 */
- (instancetype)initWithFrame:(CGRect)frame configuration:(WKWebViewConfiguration *)configuration WK_DESIGNATED_INITIALIZER;

// FIXME: This should return a WKNavigation object.
- (void)loadRequest:(NSURLRequest *)request;

@end

#endif
