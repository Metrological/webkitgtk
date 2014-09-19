/*
 * Copyright (C) 2011 Apple Inc. All rights reserved.
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
#import "WKAccessibilityWebPageObjectMac.h"

#if PLATFORM(MAC)

#import "WebFrame.h"
#import "WebPage.h"
#import "WKArray.h"
#import "WKNumber.h"
#import "WKRetainPtr.h"
#import "WKSharedAPICast.h"
#import "WKString.h"
#import "WKStringCF.h"
#import <WebCore/AXObjectCache.h>
#import <WebCore/FrameView.h>
#import <WebCore/MainFrame.h>
#import <WebCore/Page.h>
#import <WebCore/ScrollView.h>
#import <WebCore/Scrollbar.h>
#import <WebKitSystemInterface.h>
#import <wtf/ObjcRuntimeExtras.h>

using namespace WebCore;
using namespace WebKit;

@implementation WKAccessibilityWebPageObject

- (void)dealloc
{
    WKUnregisterUniqueIdForElement(self);
    [m_parent release];
    [super dealloc];
}

- (BOOL)accessibilityIsIgnored
{
    return NO;
}

- (NSArray *)accessibilityAttributeNames
{
    if (!m_attributeNames)
        m_attributeNames = adoptNS([[NSArray alloc] initWithObjects:
                            NSAccessibilityRoleAttribute, NSAccessibilityRoleDescriptionAttribute, NSAccessibilityFocusedAttribute,
                            NSAccessibilityParentAttribute, NSAccessibilityWindowAttribute, NSAccessibilityTopLevelUIElementAttribute,
                            NSAccessibilityPositionAttribute, NSAccessibilitySizeAttribute, NSAccessibilityChildrenAttribute, nil]);
    
    return m_attributeNames.get();
}

- (NSArray *)accessibilityParameterizedAttributeNames
{
    WKRetainPtr<WKArrayRef> result = adoptWK(m_page->pageOverlayController().copyAccessibilityAttributesNames(true));
    if (!result)
        return nil;
    
    NSMutableArray *names = [NSMutableArray array];
    size_t count = WKArrayGetSize(result.get());
    for (size_t k = 0; k < count; k++) {
        WKTypeRef item = WKArrayGetItemAtIndex(result.get(), k);
        if (toImpl(item)->type() == API::String::APIType) {
            RetainPtr<CFStringRef> name = adoptCF(WKStringCopyCFString(kCFAllocatorDefault, (WKStringRef)item));
            [names addObject:(NSString *)name.get()];
        }
    }
    
    return names;
}

- (BOOL)accessibilityIsAttributeSettable:(NSString *)attribute
{
    return NO;
}

- (void)accessibilitySetValue:(id)value forAttribute:(NSString *)attribute
{
}

- (NSPoint)convertScreenPointToRootView:(NSPoint)point
{
    return m_page->screenToRootView(IntPoint(point.x, point.y));
}

- (NSArray *)accessibilityActionNames
{
    return [NSArray array];
}

- (NSArray *)accessibilityChildren
{
    id wrapper = [self accessibilityRootObjectWrapper];
    if (!wrapper)
        return [NSArray array];
    
    return [NSArray arrayWithObject:wrapper];
}

- (id)accessibilityAttributeValue:(NSString *)attribute
{
    if (!WebCore::AXObjectCache::accessibilityEnabled())
        WebCore::AXObjectCache::enableAccessibility();
    
    if ([attribute isEqualToString:NSAccessibilityParentAttribute])
        return m_parent;
    
    if ([attribute isEqualToString:NSAccessibilityWindowAttribute])
        return [m_parent accessibilityAttributeValue:NSAccessibilityWindowAttribute];
    
    if ([attribute isEqualToString:NSAccessibilityTopLevelUIElementAttribute])
        return [m_parent accessibilityAttributeValue:NSAccessibilityTopLevelUIElementAttribute];
    
    if ([attribute isEqualToString:NSAccessibilityRoleAttribute])
        return NSAccessibilityGroupRole;
    
    if ([attribute isEqualToString:NSAccessibilityRoleDescriptionAttribute])
        return NSAccessibilityRoleDescription(NSAccessibilityGroupRole, nil);
    
    if ([attribute isEqualToString:NSAccessibilityFocusedAttribute])
        return [NSNumber numberWithBool:NO];
    
    if (!m_page)
        return nil;
    
    if ([attribute isEqualToString:NSAccessibilityPositionAttribute]) {
        const WebCore::FloatPoint& point = m_page->accessibilityPosition();
        return [NSValue valueWithPoint:NSMakePoint(point.x(), point.y())];
    }
    
    if ([attribute isEqualToString:NSAccessibilitySizeAttribute]) {
        const IntSize& s = m_page->size();
        return [NSValue valueWithSize:NSMakeSize(s.width(), s.height())];
    }
    
    if ([attribute isEqualToString:NSAccessibilityChildrenAttribute])
        return [self accessibilityChildren];
    
    return nil;
}

- (id)accessibilityAttributeValue:(NSString *)attribute forParameter:(id)parameter
{
    WKRetainPtr<WKTypeRef> pageOverlayParameter = 0;
    
    if ([parameter isKindOfClass:[NSValue class]] && strcmp([(NSValue*)parameter objCType], @encode(NSPoint)) == 0) {
        NSPoint point = [self convertScreenPointToRootView:[(NSValue *)parameter pointValue]];
        pageOverlayParameter = WKPointCreate(WKPointMake(point.x, point.y));
    }
    
    WKRetainPtr<WKStringRef> attributeRef = adoptWK(WKStringCreateWithCFString((CFStringRef)attribute));
    WKRetainPtr<WKTypeRef> result = adoptWK(m_page->pageOverlayController().copyAccessibilityAttributeValue(attributeRef.get(), pageOverlayParameter.get()));
    if (!result)
        return nil;
    
    if (toImpl(result.get())->type() == API::String::APIType)
        return CFBridgingRelease(WKStringCopyCFString(kCFAllocatorDefault, (WKStringRef)result.get()));
    if (toImpl(result.get())->type() == API::Boolean::APIType)
        return [NSNumber numberWithBool:WKBooleanGetValue(static_cast<WKBooleanRef>(result.get()))];
    
    return nil;
}

- (BOOL)accessibilityShouldUseUniqueId
{
    return YES;
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
- (id)accessibilityHitTest:(NSPoint)point
{
    if (!m_page)
        return nil;
    
    IntPoint convertedPoint = m_page->screenToRootView(IntPoint(point));
    if (WebCore::FrameView* frameView = m_page->mainFrameView())
        convertedPoint.moveBy(frameView->scrollPosition());
    if (WebCore::Page* page = m_page->corePage())
        convertedPoint.move(0, -page->topContentInset());
    
    return [[self accessibilityRootObjectWrapper] accessibilityHitTest:convertedPoint];
}
#pragma clang diagnostic pop

@end

#endif // PLATFORM(MAC)

