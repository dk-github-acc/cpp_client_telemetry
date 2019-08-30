#include "mat/config.h"
#ifdef HAVE_MAT_UTC
// Copyright (c) Microsoft. All rights reserved.
#ifndef UTCTELEMETRYSYSTEM_HPP
#define UTCTELEMETRYSYSTEM_HPP

#include "pal/PAL.hpp"

#include "system/TelemetrySystemBase.hpp"
#include "LogConfiguration.hpp"
#include "bond/BondSerializer.hpp"
#include "system/Contexts.hpp"

#include <map>

#include "traceloggingdynamic.h"

namespace ARIASDK_NS_BEGIN
{

    const char* const ARIA_METADATA_COMMON_TEXT = "n:";
    const char* const ARIA_METADATA_TYPE_TEXT = ";t:";
    const char* const IKEY_PRE_TEXT = "P-ARIA-";
    const char* const UTC_TYPE = "recType";
    const char* const UTC_PART_A_EXTS_BC = "PartAExt_bc";
    const char* const UTC_MAKE = "make";
    const char* const UTC_MODEL = "model";
    const char* const UTC_TIMEZONE = "tz";
    const char* const UTC_APP_LANG = "appLang";
    const char* const UTC_BC_SOURCE = "source";
    const char* const UTC_BC_ADVERTISEMENT_ID = "advertisingId";
    const char* const UTC_APP_EXPERIMENT_IDS = "PartA_Ext_App_ExpId";
    const char* const UTC_APP_ETAG = "expEtag";
    const char* const UTC_PART_A_EXTS_ARIA = "PartAExt_aria";
    const char* const UTC_LIB_VERSION = "libVer";
    const char* const UTC_PARTA_IKEY = "PartA_iKey";
    const char* const UTC_PARTA_APP_NAME = "PartA_Ext_App_Name";
    const char* const UTC_PARTA_NET_PROVIDER = "PartA_Ext_Net_Provider";
    const char* const UTC_PARTA_NET_COST = "PartA_Ext_Net_Cost";
    const char* const UTC_PARTA_NET_TYPE = "PartA_Ext_Net_Type";
    const char* const UTC_PARTA_APP_SEQ_NUM = "PartA_Ext_App_SeqNum";
    const char* const UTC_PARTA_APP_SESSION_ID = "sesId";
    const char* const UTC_PARTA_RECORD_TIMESTAMP = "evtTime";
    const char* const UTC_PARTA_APP_ASID = "PartA_Ext_App_AsId";
    const char* const UTC_PARTA_APP_USERID = "PartA_Ext_App_UserId";
    const char* const UTC_PARTA_OS_LOCALE = "PartA_Ext_Os_Locale";
    const char* const UTC_PARTA_USER_AUTH_ID = "PartA_Ext_User_AuthId";
    const char* const UTC_PARTA_ARIA_METADATA = "PartA_Ext_AriaMD";
    const char* const UTC_PARTA_ARIA_METADATA_FIELDS = "fields";
    const char* const UTC_PARTA_METADATA_PRIVTAGS = "PartA_PrivTags";

    enum AriaType : UINT16
    {
        AriaTypeString = 0,  // (Default, not emitted in �t� field)
        AriaTypeBool = 1,  // 32 bit
        AriaTypeInt64 = 2,  // 64 bit
        AriaTypeDouble = 3,  // double
        AriaTypeDateTime = 4,  // 64 bit
        AriaTypeGuid = 5   // 128 bit
    };

    typedef struct providerdata
    {
        ULONGLONG providerHandle;
        std::vector<BYTE> providerMetaVector;
    } ProviderData;

    class UtcTelemetrySystem : public TelemetrySystemBase
    {
    public:

        UtcTelemetrySystem(
            ILogManager& logManager,
            IRuntimeConfig& runtimeConfig
            // No Offline storage DB
            // No HTTP client
            // No bandwidth controller
        );

        ~UtcTelemetrySystem();

    protected:

        void handleIncomingEventPrepared(IncomingEventContextPtr const& event) override;

        int sendEventToUTC(IncomingEventContextPtr const& eventCtx);

        ProviderData getProviderFortoken(const std::string& tenantToken);

        void PutData(std::vector< ::CsProtocol::Data>& ext,
            std::vector<std::string>& MD,
            tld::EventMetadataBuilder<std::vector<BYTE>>& builder,
            tld::EventDataBuilder<std::vector<BYTE>>& dbuilder);

    protected:
        std::mutex providerTokenLock;
        std::map<std::string, ProviderData> tokenToProviderDataMap;
        std::map<std::string, bool> tokenToIkeyaMap;

    public:
        RouteSink<UtcTelemetrySystem, IncomingEventContextPtr const&> incomingEventPrepared{ this, &UtcTelemetrySystem::handleIncomingEventPrepared };

    };

} ARIASDK_NS_END

#endif
#endif