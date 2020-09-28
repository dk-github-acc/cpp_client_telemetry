#include <stdexcept>
#include "LogManager.hpp"
#include "PrivacyGuard.hpp"
#import <Foundation/Foundation.h>
#import "ODWLogConfiguration.h"
#import "ODWPrivacyGuard.h"
#import "ODWPrivacyGuard_private.h"

using namespace MAT;

@implementation ODWPrivacyGuard

std::shared_ptr<PrivacyGuard> _privacyGuardPtr;

+(CommonDataContexts)convertToNativeCommonDataContexts:(ODWCommonDataContext *)odwCDC
{
    CommonDataContexts cdc;
    cdc.DomainName = [odwCDC.DomainName UTF8String];
    cdc.MachineName = [odwCDC.MachineName UTF8String];
    cdc.UserName = [odwCDC.UserName UTF8String];
    cdc.UserAlias = [odwCDC.UserAlias UTF8String];

    for(NSString* ipAddress in odwCDC.IpAddresses)
    {
        cdc.IpAddresses.push_back([ipAddress UTF8String]);
    }

    for(NSString* languageIdentifiers in odwCDC.LanguageIdentifiers)
    {
        cdc.LanguageIdentifiers.push_back([languageIdentifiers UTF8String]);
    }

    for(NSString* machineIds in odwCDC.MachineIds)
    {
        cdc.MachineIds.push_back([machineIds UTF8String]);
    }

    for(NSString* outOfScopeIdentifiers in odwCDC.OutOfScopeIdentifiers)
    {
        cdc.OutOfScopeIdentifiers.push_back([outOfScopeIdentifiers UTF8String]);
    }

    return cdc;
}

+(void)initializePrivacyGuard:(ILogger *)logger withODWCommonDataContext:(ODWCommonDataContext *)commonDataContextsObject
{
    auto cdc = std::make_unique<CommonDataContexts>([ODWPrivacyGuard convertToNativeCommonDataContexts:commonDataContextsObject]);

    _privacyGuardPtr = std::make_shared<PrivacyGuard> (logger, std::move(cdc));
    LogManager::GetInstance()->SetDataInspector(_privacyGuardPtr);
}

+(void)setEnabled:(bool)enabled
{
    if(_privacyGuardPtr != nullptr)
    {
        _privacyGuardPtr->SetEnabled(enabled);
    }
}

+(bool)enabled
{
    return _privacyGuardPtr != nullptr && _privacyGuardPtr->IsEnabled();
}

+(void)appendCommonDataContext:(ODWCommonDataContext *) freshCommonDataContexts
{
    auto cdc = std::make_unique<CommonDataContexts>([ODWPrivacyGuard convertToNativeCommonDataContexts:freshCommonDataContexts]);

    _privacyGuardPtr->AppendCommonDataContext(std::move(cdc));
}

/*!
 @brief Add ignored concern to prevent generation of notification signals when this
 concern is found for the given EventName and Field combination.
 @param EventName Event that the ignored concern should apply to. <b>Note:</b> If the ignored concern applies to Semantic Context field, set the Event name to 'SemanticContext'.
 @param FieldName Field that the ignored concern should apply to.
 @param IgnoredConcern The concern that is expected and should be ignored.
 */
+(void)addIgnoredConcern:(NSString *) EventName withNSString:(NSString *)FieldName withODWDataConcernType:(ODWDataConcernType)IgnoredConcern
{
    const std::string eventName = [EventName UTF8String];
    const std::string fieldName = [FieldName UTF8String];
    const uint8_t ignoredConcern = (uint8_t)IgnoredConcern;
    
    _privacyGuardPtr->AddIgnoredConcern(eventName, fieldName, static_cast<DataConcernType>(ignoredConcern));
}

@end
