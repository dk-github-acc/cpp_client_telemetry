// Copyright (c) Microsoft. All rights reserved.

#include "MetaStats.hpp"

#include <utils/Utils.hpp>

namespace ARIASDK_NS_BEGIN {

    /// <summary>
    /// Lock metaStats counts when rejected events come in via a separate thread
    /// </summary>
    static std::mutex rejected_callback_mtx;
    
    /// <summary>
    /// Converts RollUpKind enum value to string name.
    /// </summary>
    /// <param name="rollupKind">Kind of the rollup.</param>
    /// <returns></returns>
    static char const* RollUpKindToString(RollUpKind rollupKind)
    {
        switch (rollupKind) {
        case ACT_STATS_ROLLUP_KIND_START:
            return "start";
        case ACT_STATS_ROLLUP_KIND_STOP:
            return "stop";
        case ACT_STATS_ROLLUP_KIND_ONGOING:
            return "ongoing";
        default:
            return "unknown";
        }
    }

    /// <summary>
    /// Initialize keys in each frequency distribution
    /// </summary>
    /// <param name="firstValue">the first non-zero value of distribution spot</param>
    /// <param name="increment">used to calculate next spot</param>
    /// <param name="totalSpot">total number of spots, including the first 0</param>
    /// <param name="distribution">map</param>
    /// <param name="factor">if true, next spot = last spot * increment; otherwise, next spot = last spot + increment</param>
    void initDistributionKeys(unsigned int firstValue, unsigned int increment, unsigned int totalSpot, uint_uint_dict_t& distribution, bool factor = true)
    {
        distribution.clear();
        distribution[0] = 0;
        unsigned int lastkey = 0;
        if (factor) {
            for (unsigned int i = 1; i < totalSpot; ++i) {
                unsigned int key = (lastkey == 0) ? firstValue : (increment * lastkey);
                distribution[key] = 0;
                lastkey = key;
            }
        }
        else {
            for (unsigned int i = 1; i < totalSpot; ++i) {
                unsigned int key = (lastkey == 0) ? firstValue : (lastkey + increment);
                distribution[key] = 0;
                lastkey = key;
            }
        }
    }

    /// <summary>
    /// Update the occurence within the corresponding group of Map distribution
    /// </summary>
    /// <param name="distribution">a distribution to be updated</param>
    /// <param name="value">unsigned int, sample value, must be in some group of the given distribution</param>
    void updateMap(uint_uint_dict_t& distribution, unsigned int value)
    {
        if (distribution.empty()) {
            return;
        }

        uint_uint_dict_t::iterator it = distribution.begin();
        for (; it != distribution.end(); ++it) {
            if (value < it->first) {
                break;
            }
        }

        if (it == distribution.begin()) {
            // If value is not in any range, we still put it in first range.
            //LOG_WARN("value %u is less than distribution start (< %u)", value, it->first);
            it->second++;
        }
        else {
            (--it)->second++;
        }
    }

    /// <summary>
    /// A template function with typename T.
    /// The definition and implement must be in the same file.
    /// Only Clear values of each frequency distribution while keys are maintained.
    /// </summary>
    /// <param name="distribution">map<T, unsigned int></param>
    template<typename T, typename V>
    void clearMapValues(std::map<T, V>& distribution)
    {
        for (auto& item : distribution) {
            item.second = 0;
        }
    }

    template<typename T>
    static void insertNonZero(std::map<std::string, ::AriaProtocol::Value>& target, std::string const& key, T const& value)
    {
        if (value != 0)
        {
            ::AriaProtocol::Value temp;
            temp.stringValue = toString(value);
            target[key] = temp;
        }
    }

    /// <summary>
    /// Add A Map struture map to Record Extension Field
    /// For example,
    /// 1) range distribution
    /// map = { 1:2, 2 : 3, 3 : 4 }
    /// record.data.properties = { "name_0_1":"2", "name_1_2" : "3", "name_3_plus" : "4" }
    /// 2) otherwise
    /// record.data.properties = { "name_1":"2", "name_2" : "3", "name_3" : "4" }
    /// </summary>
    /// <param name="record">telemetry::Record</param>
    /// <param name="distributionName">prefix of extension key name</param>
    /// <param name="distribution">map<unsigned int, unsigned int></param>
    /// <param name="range">indicate if the frequency distribution stored in map is based on multiple groups or multiple points</param>
    static void addAggregatedMapToRecordFields(::AriaProtocol::Record& record, std::string const& distributionName,
        uint_uint_dict_t const& distribution, bool range = true)
    {
        if (distribution.empty()) {
            return;
        }
        if (record.data.size() == 0)
        {
            ::AriaProtocol::Data data;
            record.data.push_back(data);
        }

        std::map<std::string, ::AriaProtocol::Value>& ext = record.data[0].properties;
        uint_uint_dict_t::const_iterator it, next;
        std::string fieldValue;

        // ext field will be in format distributionName_i_j
        for (it = distribution.begin(), next = it; it != distribution.end(); ++it) {
            ++next;
            if (next == distribution.end()) {
                if (range) {
                    fieldValue += ">" + toString(it->first) + ":" + toString(it->second);
                }
                else {
                    fieldValue += toString(it->first) + ":" + toString(it->second);
                }
            }
            else {
                if (range) {
                    fieldValue += toString(it->first) + "-" + toString(next->first) + ":" + toString(it->second) + ",";
                }
                else {
                    fieldValue += toString(it->first) + ":" + toString(it->second) + ",";
                }
            }
        }

        ::AriaProtocol::Value temp;
        temp.stringValue = fieldValue;
        ext[distributionName] = temp;
    }

    /// <summary>
    /// Add A Map struture to Record Extension Field
    /// For example,
    /// map= {"a":2, "b":3, "c":4}
    /// record.data.properties = {"name_a":"2", "name_b":"3", "name_c":"4"}
    /// </summary>
    /// <param name="record">telemetry::Record</param>
    /// <param name="distributionName">prefix of the key name in record extension map</param>
    /// <param name="distribution">map<std::string, unsigned int>, key is the source of event</param>
    static void addAggregatedMapToRecordFields(::AriaProtocol::Record& record, std::string const& distributionName,
        string_uint_dict_t const& distribution)
    {
        if (distribution.empty()) {
            return;
        }

        std::string fieldValue;

        // ext field will be in format distributionPrefix_i if distributionPrefix given
        for (std::map<std::string, unsigned int>::const_iterator it = distribution.begin(), next = it;
            it != distribution.end(); ++it)
        {
            ++next;
            if (next == distribution.end()) {
                fieldValue += it->first + ":" + toString(it->second);
            }
            else {
                fieldValue += it->first + ":" + toString(it->second) + ",";
            }
        }
        ::AriaProtocol::Value temp;;
        temp.stringValue = fieldValue;
        if (record.data.size() == 0)
        {
            ::AriaProtocol::Data data;
            record.data.push_back(data);
        }
        record.data[0].properties[distributionName] = temp;
    }

    /// <summary>
    /// Add count per each HTTP recode code to Record Extension Field
    /// </summary>
    /// <param name="record">telemetry::Record</param>
    /// <param name="distributionName">prefix of the key name in record extension map</param>
    /// <param name="distribution">map<unsigned int, unsigned int>, key is the http return code, value is the count</param>
    static void addCountsPerHttpReturnCodeToRecordFields(::AriaProtocol::Record& record, std::string const& prefix,
        uint_uint_dict_t const& countsPerHttpReturnCodeMap)
    {
        if (countsPerHttpReturnCodeMap.empty()) {
            return;
        }

        if (record.data.size() == 0)
        {
            ::AriaProtocol::Data data;
            record.data.push_back(data);
        }
        for (auto const& item : countsPerHttpReturnCodeMap) {
            insertNonZero(record.data[0].properties, prefix + "_" + toString(item.first), item.second);
        }
    }

    /// <summary>
    /// Add rejected count by reason to Record Extension Field
    /// </summary>
    /// <param name="record">BondTypes::Record</param>
    /// <param name="recordsRejectedCountReasonDistribution">count of rejected records by reason due to which records were rejected</param>
    static void addRecordsPerRejectedReasonToRecordFields(::AriaProtocol::Record& record, uint_uint_dict_t& recordsRejectedCountByReasonDistribution)
    {
        if (record.data.size() == 0)
        {
            ::AriaProtocol::Data data;
            record.data.push_back(data);
        }

        std::map<std::string, ::AriaProtocol::Value>& extension = record.data[0].properties;

        insertNonZero(extension, "r_inv",  recordsRejectedCountByReasonDistribution[REJECTED_REASON_INVALID_CLIENT_MESSAGE_TYPE]);
        insertNonZero(extension, "r_inv",  recordsRejectedCountByReasonDistribution[REJECTED_REASON_REQUIRED_ARGUMENT_MISSING]);
        insertNonZero(extension, "r_inv",  recordsRejectedCountByReasonDistribution[REJECTED_REASON_EVENT_NAME_MISSING]);
        insertNonZero(extension, "r_inv",  recordsRejectedCountByReasonDistribution[REJECTED_REASON_VALIDATION_FAILED]);
        insertNonZero(extension, "r_inv",  recordsRejectedCountByReasonDistribution[REJECTED_REASON_OLD_RECORD_VERSION]);
        insertNonZero(extension, "r_exp",  recordsRejectedCountByReasonDistribution[REJECTED_REASON_EVENT_EXPIRED]);
        insertNonZero(extension, "r_403",  recordsRejectedCountByReasonDistribution[REJECTED_REASON_SERVER_DECLINED]);
        insertNonZero(extension, "r_kl",   recordsRejectedCountByReasonDistribution[REJECTED_REASON_TENANT_KILLED]);
        insertNonZero(extension, "r_size", recordsRejectedCountByReasonDistribution[REJECTED_REASON_EVENT_SIZE_LIMIT_EXCEEDED]);
    }

    MetaStats::MetaStats(IRuntimeConfig& config)
        :
        m_config(config)
    {
        m_telemetryStats.statsStartTimestamp = PAL::getUtcSystemTimeMs();
        m_telemetryStats.session_startup_time_in_millisec = m_telemetryStats.statsStartTimestamp;
        resetStats(true);
        //TODO: extend IRuntimeConfig to include these vars
        m_telemetryStats.offlineStorageEnabled = true;
        m_telemetryStats.resourceManagerEnabled = false;
        m_telemetryStats.ecsClientEnabled = false;

        m_sessionId = PAL::generateUuidString();
    }

    MetaStats::~MetaStats()
    {
    }
    
    /// <summary>
    /// Resets the stats.
    /// </summary>
    /// <param name="start">if set to <c>true</c> [start].</param>
    void MetaStats::resetStats(bool start)
    {
        LOG_TRACE("resetStats start=%u", (unsigned)start);
        for (auto &tenantStats : m_telemetryTenantStats)
        {
            TelemetryStats& telemetryStats = tenantStats.second;

            //clear packageStats
            telemetryStats.packageStats.Reset();

            //clear rttStats
            auto& rttStats = telemetryStats.rttStats;
            rttStats.Reset();

            //clear logToSuccessfulSendLatencyPerLatency stats
            telemetryStats.logToSuccessfulSendLatencyPerLatency.clear();

            //clear recordStats
            auto& recordStats = telemetryStats.recordStats;
            recordStats.Reset();

            //clear recordStatsPerPriority
            for (auto& kv : telemetryStats.recordStatsPerLatency) {
                kv.second.Reset();
            }

            //clear offlineStorageStats
            OfflineStorageStats& storageStats = telemetryStats.offlineStorageStats;
            storageStats.Reset();

            telemetryStats.statsStartTimestamp = PAL::getUtcSystemTimeMs();
            telemetryStats.sessionId = m_sessionId;

            if (start) {
                telemetryStats.statsSequenceNum = 0;
                telemetryStats.sessionStartTimestamp = telemetryStats.statsStartTimestamp;
                LOG_TRACE("session start, session ID: %s", telemetryStats.sessionId.c_str());

                initDistributionKeys(m_statsConfig.rtt_first_duration_in_millisecs, m_statsConfig.rtt_next_factor,
                    m_statsConfig.rtt_total_spots, rttStats.latencyDistribution);

                for (auto& item : telemetryStats.logToSuccessfulSendLatencyPerLatency) {
                    initDistributionKeys(m_statsConfig.latency_first_duration_in_millisecs, m_statsConfig.latency_next_factor,
                        m_statsConfig.latency_total_spots, item.second.latencyDistribution);
                }

                initDistributionKeys(m_statsConfig.record_size_first_in_kb, m_statsConfig.record_size_next_factor,
                    m_statsConfig.record_size_total_spots, recordStats.sizeInKBytesDistribution);

                if (telemetryStats.offlineStorageEnabled) {
                    initDistributionKeys(m_statsConfig.storage_size_first_in_kb, m_statsConfig.storage_size_next_factor,
                        m_statsConfig.storage_size_total_spots, storageStats.saveSizeInKBytesDistribution);
                    initDistributionKeys(m_statsConfig.storage_size_first_in_kb, m_statsConfig.storage_size_next_factor,
                        m_statsConfig.storage_size_total_spots, storageStats.overwrittenSizeInKBytesDistribution);
                }
            }
            else {
                LOG_TRACE("ongoing stats, session ID: %s", telemetryStats.sessionId.c_str());
                telemetryStats.statsSequenceNum += 1;
                telemetryStats.packageStats.dropPkgsPerHttpReturnCode.clear();
                telemetryStats.packageStats.retryPkgsPerHttpReturnCode.clear();

                telemetryStats.retriesCountDistribution.clear();

                clearMapValues(rttStats.latencyDistribution);

                telemetryStats.logToSuccessfulSendLatencyPerLatency.clear();

                clearMapValues(recordStats.semanticToRecordCountMap);
                clearMapValues(recordStats.semanticToExceptionCountMap);
                clearMapValues(recordStats.sizeInKBytesDistribution);

                recordStats.droppedCountPerHttpReturnCode.clear();

                if (telemetryStats.offlineStorageEnabled) {
                    clearMapValues(storageStats.saveSizeInKBytesDistribution);
                    clearMapValues(storageStats.overwrittenSizeInKBytesDistribution);
                }
            }
        }
    }
    
    /// <summary>
    /// Saves private snap stats to record.
    /// </summary>
    /// <param name="records">The records.</param>
    /// <param name="rollupKind">Kind of the rollup.</param>
    /// <param name="telemetryStats">The telemetry stats.</param>
    void MetaStats::privateSnapStatsToRecord(std::vector< ::AriaProtocol::Record>& records,
        RollUpKind rollupKind,
        TelemetryStats& telemetryStats)
    {
        ::AriaProtocol::Record record;
        record.baseType = "act_stats";

        ::AriaProtocol::Value temp;
        temp.stringValue = m_sessionId;

        if (record.data.size() == 0)
        {
            ::AriaProtocol::Data data;
            record.data.push_back(data);
        }
        std::map<std::string, ::AriaProtocol::Value>& ext = record.data[0].properties;

        ext["act_stats_id"] = temp;

        //basic Fields
        //Add the tenantID (not the entire tenantToken) to the stats event

        std::string statTenantToken = m_config.GetMetaStatsTenantToken();
        record.iKey = "o:" + statTenantToken.substr(0, statTenantToken.find('-'));;
        record.name = "act_stats";

        // session fileds
        insertNonZero(ext, "s_stime", telemetryStats.sessionStartTimestamp);
        insertNonZero(ext, "stats_stime", telemetryStats.statsStartTimestamp);
        insertNonZero(ext, "s_Firststime", telemetryStats.session_startup_time_in_millisec);
        insertNonZero(ext, "stats_etime", PAL::getUtcSystemTimeMs());
        ::AriaProtocol::Value rollupKindValue;
        rollupKindValue.stringValue = RollUpKindToString(rollupKind);
        ext["stats_rollup_kind"] = rollupKindValue;
        insertNonZero(ext, "st_freq", m_config.GetMetaStatsSendIntervalSec());

        if (telemetryStats.offlineStorageEnabled) {
            OfflineStorageStats& storageStats = telemetryStats.offlineStorageStats;
            ::AriaProtocol::Value storageFormatValue;
            storageFormatValue.stringValue = storageStats.storageFormat;
            ext["off_type"] = storageFormatValue;
            if (!storageStats.lastFailureReason.empty())
            {
                ::AriaProtocol::Value lastFailureReasonValue;
                lastFailureReasonValue.stringValue = storageStats.lastFailureReason;
                ext["off_last_failure"] = lastFailureReasonValue;
            }
            insertNonZero(ext, "config_off_size", storageStats.fileSizeInBytes);
        }

        //package stats
        PackageStats& packageStats = telemetryStats.packageStats;
        insertNonZero(ext, "rqs_not_to_be_acked", packageStats.totalPkgsNotToBeAcked);
        insertNonZero(ext, "rqs_to_be_acked", packageStats.totalPkgsToBeAcked);
        insertNonZero(ext, "rqs_acked", packageStats.totalPkgsAcked);
        insertNonZero(ext, "rqs_acked_succ", packageStats.successPkgsAcked);
        insertNonZero(ext, "rqs_acked_ret", packageStats.retryPkgsAcked);
        insertNonZero(ext, "rqs_acked_drp", packageStats.dropPkgsAcked);
        addCountsPerHttpReturnCodeToRecordFields(record, "rqs_acked_drp_on_HTTP", packageStats.dropPkgsPerHttpReturnCode);
        addCountsPerHttpReturnCodeToRecordFields(record, "rqs_acked_ret_on_HTTP", packageStats.retryPkgsPerHttpReturnCode);

        insertNonZero(ext, "rm_bw_bytes_consumed_count", packageStats.totalBandwidthConsumedInBytes);

        //InternalHttpStackRetriesStats
        if (packageStats.totalPkgsAcked > 0) {
            LOG_TRACE("httpstack_retries stats is added to record extension field");
            addAggregatedMapToRecordFields(record, "rqs_fail_on_HTTP_retries_count_distribution",
                telemetryStats.retriesCountDistribution, false);
        }

        //RTTStats
        if (packageStats.successPkgsAcked > 0) {
            LOG_TRACE("rttStats is added to record ext field");
            LatencyStats& rttStats = telemetryStats.rttStats;
            insertNonZero(ext, "rtt_millisec_max", rttStats.maxOfLatencyInMilliSecs);
            insertNonZero(ext, "rtt_millisec_min", rttStats.minOfLatencyInMilliSecs);
            addAggregatedMapToRecordFields(record, "rtt_millisec_distribution", rttStats.latencyDistribution);
        }

        //RecordStats
        RecordStats& recordStats = telemetryStats.recordStats;

        insertNonZero(ext, "r_ban", recordStats.bannedCount);//records_banned_count

        insertNonZero(ext, "rcv", recordStats.receivedCount);// records_received_count

        insertNonZero(ext, "snt", recordStats.sentCount);//records_sent_count
        insertNonZero(ext, "rcds_sent_curr_session", recordStats.sentCountFromCurrentSession);
        insertNonZero(ext, "rcds_sent_prev_session", recordStats.sentCountFromPreviousSession);

        insertNonZero(ext, "rej", recordStats.rejectedCount);//records_rejected_count
        addRecordsPerRejectedReasonToRecordFields(record, recordStats.rejectedCountReasonDistribution);

        insertNonZero(ext, "drp", recordStats.droppedCount);//records_dropped_count
        insertNonZero(ext, "d_disk_full", recordStats.overflownCount);
        insertNonZero(ext, "d_io_fail", recordStats.droppedCountReasonDistribution[DROPPED_REASON_OFFLINE_STORAGE_SAVE_FAILED]);
        insertNonZero(ext, "d_retry_lmt", recordStats.droppedCountReasonDistribution[DROPPED_REASON_RETRY_EXCEEDED]);
        addCountsPerHttpReturnCodeToRecordFields(record, "rcds_drp_on_HTTP", recordStats.droppedCountPerHttpReturnCode);

        addAggregatedMapToRecordFields(record, "exceptions_per_eventtype_count", recordStats.semanticToExceptionCountMap);
        addAggregatedMapToRecordFields(record, "rcds_per_eventtype_count", recordStats.semanticToRecordCountMap);

        if (recordStats.receivedCount > 0) {
            LOG_TRACE("source stats and record size stats in recordStats"
                " are added to record ext field");
            insertNonZero(ext, "rcd_size_bytes_max", recordStats.maxOfRecordSizeInBytes);
            insertNonZero(ext, "rcd_size_bytes_min", recordStats.minOfRecordSizeInBytes);
            insertNonZero(ext, "rcds_received_size_bytes", recordStats.totalRecordsSizeInBytes);
            addAggregatedMapToRecordFields(record, "rcd_size_kb_distribution", recordStats.sizeInKBytesDistribution);
        }

        // Low priority RecordStats
        const RecordStats& lowPriorityrecordStats = telemetryStats.recordStatsPerLatency[EventLatency_Normal];
        insertNonZero(ext, "ln_r_ban", lowPriorityrecordStats.bannedCount);
        insertNonZero(ext, "ln_rcv", lowPriorityrecordStats.receivedCount);
        insertNonZero(ext, "ln_snt", lowPriorityrecordStats.sentCount);
        insertNonZero(ext, "ln_rcds_sent_count_current_session", lowPriorityrecordStats.sentCountFromCurrentSession);
        insertNonZero(ext, "ln_rcds_sent_count_previous_sessions", lowPriorityrecordStats.sentCountFromPreviousSession);
        insertNonZero(ext, "ln_drp", lowPriorityrecordStats.droppedCount);
        insertNonZero(ext, "ln_d_disk_full", lowPriorityrecordStats.overflownCount);
        insertNonZero(ext, "ln_rej", lowPriorityrecordStats.rejectedCount);
        if (lowPriorityrecordStats.receivedCount > 0) {
            LOG_TRACE("Low priority source stats and record size stats in recordStats"
                " are added to record ext field");
            insertNonZero(ext, "ln_rcds_received_size_bytes", lowPriorityrecordStats.totalRecordsSizeInBytes);
        }
        if (lowPriorityrecordStats.sentCount > 0) {
            LatencyStats& logToSuccessfulSendLatencyLow = telemetryStats.logToSuccessfulSendLatencyPerLatency[EventLatency_Normal];
            insertNonZero(ext, "ln_log_to_successful_send_latency_millisec_max", logToSuccessfulSendLatencyLow.maxOfLatencyInMilliSecs);
            insertNonZero(ext, "n_log_to_successful_send_latency_millisec_min", logToSuccessfulSendLatencyLow.minOfLatencyInMilliSecs);
            addAggregatedMapToRecordFields(record, "ln_log_to_successful_send_latency_millisec_distribution", logToSuccessfulSendLatencyLow.latencyDistribution);
        }

        // Normal priority RecordStats
        const RecordStats& normalPriorityrecordStats = telemetryStats.recordStatsPerLatency[EventLatency_CostDeferred];
        insertNonZero(ext, "ld_r_ban", normalPriorityrecordStats.bannedCount);
        insertNonZero(ext, "ld_rcv", normalPriorityrecordStats.receivedCount);
        insertNonZero(ext, "ld_snt", normalPriorityrecordStats.sentCount);
        insertNonZero(ext, "ld_rcds_sent_count_current_session", normalPriorityrecordStats.sentCountFromCurrentSession);
        insertNonZero(ext, "ld_rcds_sent_count_previous_sessions", normalPriorityrecordStats.sentCountFromPreviousSession);
        insertNonZero(ext, "ld_drp", normalPriorityrecordStats.droppedCount);
        insertNonZero(ext, "ld_d_disk_full", normalPriorityrecordStats.overflownCount);
        insertNonZero(ext, "ld_rej", normalPriorityrecordStats.rejectedCount);
        if (normalPriorityrecordStats.receivedCount > 0) {
            LOG_TRACE("Normal priority source stats and record size stats in recordStats"
                " are added to record ext field");
            insertNonZero(ext, "ld_rcds_received_size_bytes", normalPriorityrecordStats.totalRecordsSizeInBytes);
        }
        if (normalPriorityrecordStats.sentCount > 0) {
            LatencyStats& logToSuccessfulSendLatencyNormal = telemetryStats.logToSuccessfulSendLatencyPerLatency[EventLatency_CostDeferred];
            insertNonZero(ext, "ld_log_to_successful_send_latency_millisec_max", logToSuccessfulSendLatencyNormal.maxOfLatencyInMilliSecs);
            insertNonZero(ext, "ld_log_to_successful_send_latency_millisec_min", logToSuccessfulSendLatencyNormal.minOfLatencyInMilliSecs);
            addAggregatedMapToRecordFields(record, "ld_log_to_successful_send_latency_millisec_distribution", logToSuccessfulSendLatencyNormal.latencyDistribution);
        }

        // High priority RecordStats
        const RecordStats& highPriorityrecordStats = telemetryStats.recordStatsPerLatency[EventLatency_RealTime];
        insertNonZero(ext, "lr_r_ban", highPriorityrecordStats.bannedCount);
        insertNonZero(ext, "lr_rcv", highPriorityrecordStats.receivedCount);
        insertNonZero(ext, "lr_snt", highPriorityrecordStats.sentCount);
        insertNonZero(ext, "lr_rcds_sent_count_current_session", highPriorityrecordStats.sentCountFromCurrentSession);
        insertNonZero(ext, "lr_rcds_sent_count_previous_sessions", highPriorityrecordStats.sentCountFromPreviousSession);
        insertNonZero(ext, "lr_drp", highPriorityrecordStats.droppedCount);
        insertNonZero(ext, "ld_d_disk_full", normalPriorityrecordStats.overflownCount);
        insertNonZero(ext, "lr_rej", highPriorityrecordStats.rejectedCount);
        if (highPriorityrecordStats.receivedCount > 0) {
            LOG_TRACE("High priority source stats and record size stats in recordStats"
                " are added to record ext field");
            insertNonZero(ext, "lr_rcds_received_size_bytes", highPriorityrecordStats.totalRecordsSizeInBytes);
        }
        if (highPriorityrecordStats.sentCount > 0) {
            LatencyStats& logToSuccessfulSendLatencyHigh = telemetryStats.logToSuccessfulSendLatencyPerLatency[EventLatency_RealTime];
            insertNonZero(ext, "lr_log_to_successful_send_latency_millisec_max", logToSuccessfulSendLatencyHigh.maxOfLatencyInMilliSecs);
            insertNonZero(ext, "lr_log_to_successful_send_latency_millisec_min", logToSuccessfulSendLatencyHigh.minOfLatencyInMilliSecs);
            addAggregatedMapToRecordFields(record, "lr_log_to_successful_send_latency_millisec_distribution", logToSuccessfulSendLatencyHigh.latencyDistribution);
        }

        // Immediate priority RecordStats
        const RecordStats& immediatePriorityrecordStats = telemetryStats.recordStatsPerLatency[EventLatency_Max];
        insertNonZero(ext, "lm_r_ban", immediatePriorityrecordStats.bannedCount);
        insertNonZero(ext, "lm_rcv", immediatePriorityrecordStats.receivedCount);
        insertNonZero(ext, "lm_snt", immediatePriorityrecordStats.sentCount);
        insertNonZero(ext, "lm_rcds_sent_count_current_session", immediatePriorityrecordStats.sentCountFromCurrentSession);
        insertNonZero(ext, "lm_rcds_sent_count_previous_sessions", immediatePriorityrecordStats.sentCountFromPreviousSession);
        insertNonZero(ext, "lm_drp", immediatePriorityrecordStats.droppedCount);
        insertNonZero(ext, "lm_snt", immediatePriorityrecordStats.rejectedCount);
        if (immediatePriorityrecordStats.receivedCount > 0) {
            LOG_TRACE(" high latency source stats and record size stats in recordStats are added to record ext field");
            insertNonZero(ext, "lm_rcds_received_size_bytes", immediatePriorityrecordStats.totalRecordsSizeInBytes);
        }
        if (immediatePriorityrecordStats.sentCount > 0) {
            LatencyStats& logToSuccessfulSendLatencyImmediate = telemetryStats.logToSuccessfulSendLatencyPerLatency[EventLatency_Max];
            insertNonZero(ext, "lm_log_to_successful_send_latency_millisec_max", logToSuccessfulSendLatencyImmediate.maxOfLatencyInMilliSecs);
            insertNonZero(ext, "lm_log_to_successful_send_latency_millisec_min", logToSuccessfulSendLatencyImmediate.minOfLatencyInMilliSecs);
            addAggregatedMapToRecordFields(record, "lm_log_to_successful_send_latency_millisec_distribution", logToSuccessfulSendLatencyImmediate.latencyDistribution);
        }
        records.push_back(record);
    }
    
    /// <summary>
    /// Saves tenant stats to record for given current RollUpKind
    /// </summary>
    /// <param name="records">The records.</param>
    /// <param name="rollupKind">Kind of the rollup.</param>
    void MetaStats::snapStatsToRecord(std::vector< ::AriaProtocol::Record>& records, RollUpKind rollupKind)
    {
        LOG_TRACE("snapStatsToRecord");

        for (auto &tenantStats : m_telemetryTenantStats)
        {
            TelemetryStats& telemetryStats = tenantStats.second;
            privateSnapStatsToRecord(records, rollupKind, telemetryStats);
        }

        if (RollUpKind::ACT_STATS_ROLLUP_KIND_ONGOING != rollupKind)
        {
            std::string statTenantToken = m_config.GetMetaStatsTenantToken();
            m_telemetryStats.tenantId = statTenantToken.substr(0, statTenantToken.find('-'));
            privateSnapStatsToRecord(records, rollupKind, m_telemetryStats);
        }
    }
    
    /// <summary>
    /// Clears the stats.
    /// </summary>
    /// <param name="telemetryStats">The telemetry stats.</param>
    void MetaStats::privateClearStats(TelemetryStats& telemetryStats)
    {
        telemetryStats.packageStats.dropPkgsPerHttpReturnCode.clear();
        telemetryStats.packageStats.retryPkgsPerHttpReturnCode.clear();

        telemetryStats.retriesCountDistribution.clear();

        telemetryStats.rttStats.latencyDistribution.clear();

        telemetryStats.logToSuccessfulSendLatencyPerLatency.clear();

        RecordStats& recordStats = telemetryStats.recordStats;
        recordStats.sizeInKBytesDistribution.clear();
        recordStats.semanticToRecordCountMap.clear();
        recordStats.semanticToExceptionCountMap.clear();
        recordStats.droppedCountPerHttpReturnCode.clear();

        OfflineStorageStats& storageStats = telemetryStats.offlineStorageStats;
        storageStats.saveSizeInKBytesDistribution.clear();
        storageStats.overwrittenSizeInKBytesDistribution.clear();
    }
    
    /// <summary>
    /// Clears the stats.
    /// </summary>
    void MetaStats::clearStats()
    {
        LOG_TRACE("clearStats");

        for (auto& tenantStats : m_telemetryTenantStats)
        {
            TelemetryStats& telemetryStats = tenantStats.second;
            privateClearStats(telemetryStats);
        }
        privateClearStats(m_telemetryStats);
    }
    
    /// <summary>
    /// Determines whether stats data available.
    /// </summary>
    /// <returns>
    ///   <c>true</c> if [has stats data available]; otherwise, <c>false</c>.
    /// </returns>
    bool MetaStats::hasStatsDataAvailable() const
    {
        unsigned int rejectedCount = 0;
        unsigned int bannedCount = 0;
        unsigned int droppedCount = 0;
        unsigned int receivedCountnotStats = 0;

        for (const auto& tenantStats : m_telemetryTenantStats)
        {
            const auto& telemetryStats = tenantStats.second;
            rejectedCount += telemetryStats.recordStats.rejectedCount;
            bannedCount += telemetryStats.recordStats.bannedCount;
            droppedCount += telemetryStats.recordStats.droppedCount;
            receivedCountnotStats += telemetryStats.recordStats.receivedCount - telemetryStats.recordStats.receivedMetastatsCount;
        }
        return (rejectedCount > 0 ||   // not used
            bannedCount > 0 ||      // not used
            droppedCount > 0 ||     // not used
            receivedCountnotStats > 0 ||
            m_telemetryStats.packageStats.totalPkgsAcked > m_telemetryStats.packageStats.totalMetastatsOnlyPkgsAcked ||
            m_telemetryStats.packageStats.totalPkgsToBeAcked > m_telemetryStats.packageStats.totalMetastatsOnlyPkgsToBeAcked);
    }
    
    /// <summary>
    /// Generates the stats event.
    /// </summary>
    /// <param name="rollupKind">Kind of the rollup.</param>
    /// <returns></returns>
    std::vector< ::AriaProtocol::Record> MetaStats::generateStatsEvent(RollUpKind rollupKind)
    {
        LOG_TRACE("generateStatsEvent");

        std::vector< ::AriaProtocol::Record> records;

        if (hasStatsDataAvailable() || rollupKind != RollUpKind::ACT_STATS_ROLLUP_KIND_ONGOING) {
            snapStatsToRecord(records, rollupKind);
            resetStats(false);
        }

        if (rollupKind == ACT_STATS_ROLLUP_KIND_STOP) {
            clearStats();
        }

        return records;
    }
    
    /// <summary>
    /// Updates stats on incoming event.
    /// </summary>
    /// <param name="tenanttoken">The tenanttoken.</param>
    /// <param name="size">The size.</param>
    /// <param name="latency">The latency.</param>
    /// <param name="metastats">if set to <c>true</c> [metastats].</param>
    void MetaStats::updateOnEventIncoming(std::string const& tenanttoken, unsigned size, EventLatency latency, bool metastats)
    {
        if (!metastats)
        {
            if (m_telemetryTenantStats[tenanttoken].tenantId.empty())
            {
                m_telemetryTenantStats[tenanttoken].tenantId = tenanttoken.substr(0, tenanttoken.find('-'));
            }

            RecordStats& recordStats = m_telemetryTenantStats[tenanttoken].recordStats;
            recordStats.receivedCount++;

            updateMap(recordStats.sizeInKBytesDistribution, size / 1024);

            recordStats.maxOfRecordSizeInBytes = std::max<unsigned>(recordStats.maxOfRecordSizeInBytes, size);
            recordStats.minOfRecordSizeInBytes = std::min<unsigned>(recordStats.minOfRecordSizeInBytes, size);
            recordStats.totalRecordsSizeInBytes += size;

            if (latency >= 0) {
                RecordStats& recordStatsPerPriority = m_telemetryTenantStats[tenanttoken].recordStatsPerLatency[latency];
                recordStatsPerPriority.receivedCount++;
                recordStatsPerPriority.totalRecordsSizeInBytes += size;
            }
        }

        //overall stats for all tennat tokens together
        RecordStats&  recordStats = m_telemetryStats.recordStats;
        recordStats.receivedCount++;
        if (metastats) {
            recordStats.receivedMetastatsCount++;
        }

        updateMap(recordStats.sizeInKBytesDistribution, size / 1024);

        recordStats.maxOfRecordSizeInBytes = std::max<unsigned>(recordStats.maxOfRecordSizeInBytes, size);
        recordStats.minOfRecordSizeInBytes = std::min<unsigned>(recordStats.minOfRecordSizeInBytes, size);
        recordStats.totalRecordsSizeInBytes += size;

        if (latency >= 0) {
            RecordStats& recordStatsPerPriority = m_telemetryStats.recordStatsPerLatency[latency];
            recordStatsPerPriority.receivedCount++;
            recordStatsPerPriority.totalRecordsSizeInBytes += size;
        }
    }
    
    /// <summary>
    /// Updates stats on post data success.
    /// </summary>
    /// <param name="postDataLength">Length of the post data.</param>
    /// <param name="metastatsOnly">if set to <c>true</c> [metastats only].</param>
    void MetaStats::updateOnPostData(unsigned postDataLength, bool metastatsOnly)
    {
        m_telemetryStats.packageStats.totalBandwidthConsumedInBytes += postDataLength;
        m_telemetryStats.packageStats.totalPkgsToBeAcked++;
        if (metastatsOnly) {
            m_telemetryStats.packageStats.totalMetastatsOnlyPkgsToBeAcked++;
        }
    }
    
    /// <summary>
    /// Updates stats on successful package send.
    /// </summary>
    /// <param name="recordIdsAndTenantids">The record ids and tenantids.</param>
    /// <param name="eventLatency">The event latency.</param>
    /// <param name="retryFailedTimes">The retry failed times.</param>
    /// <param name="durationMs">The duration ms.</param>
    /// <param name="latencyToSendMs">The latency to send ms.</param>
    /// <param name="metastatsOnly">if set to <c>true</c> [metastats only].</param>
    void MetaStats::updateOnPackageSentSucceeded(std::map<std::string, std::string> const& recordIdsAndTenantids, EventLatency eventLatency, unsigned retryFailedTimes, unsigned durationMs, std::vector<unsigned> const& latencyToSendMs, bool metastatsOnly)
    {
        unsigned const recordsSentCount = static_cast<unsigned>(latencyToSendMs.size());

        PackageStats& packageStats = m_telemetryStats.packageStats;
        packageStats.totalPkgsAcked++;
        packageStats.successPkgsAcked++;
        if (metastatsOnly) {
            packageStats.totalMetastatsOnlyPkgsAcked++;
        }
        m_telemetryStats.retriesCountDistribution[retryFailedTimes]++;

        //duration: distribution, max, min
        LatencyStats& rttStats = m_telemetryStats.rttStats;
        updateMap(rttStats.latencyDistribution, durationMs);
        rttStats.maxOfLatencyInMilliSecs = std::max<unsigned>(rttStats.maxOfLatencyInMilliSecs, durationMs);
        rttStats.minOfLatencyInMilliSecs = std::min<unsigned>(rttStats.minOfLatencyInMilliSecs, durationMs);


        //for overall stats
        {
            LatencyStats& logToSuccessfulSendLatency = m_telemetryStats.logToSuccessfulSendLatencyPerLatency[eventLatency];
            for (unsigned latencyMs : latencyToSendMs) {
                updateMap(logToSuccessfulSendLatency.latencyDistribution, latencyMs);
                logToSuccessfulSendLatency.maxOfLatencyInMilliSecs = std::max(logToSuccessfulSendLatency.maxOfLatencyInMilliSecs, latencyMs);
                logToSuccessfulSendLatency.minOfLatencyInMilliSecs = std::min(logToSuccessfulSendLatency.minOfLatencyInMilliSecs, latencyMs);
            }

            RecordStats& recordStats = m_telemetryStats.recordStats;
            recordStats.sentCount += recordsSentCount;

            // TODO: [MG] fix it after ongoing stats implemented or discarded
            if (1) {
                recordStats.sentCountFromCurrentSession += recordsSentCount;
            }
            else {
                recordStats.sentCountFromPreviousSession += recordsSentCount;
            }

            //update per-priority record stats
            if (eventLatency >= 0) {
                RecordStats& recordStatsPerPriority = m_telemetryStats.recordStatsPerLatency[eventLatency];
                recordStatsPerPriority.sentCount += recordsSentCount;
                // TODO: [MG] fix it after ongoing stats implemented or discarded
                if (1) {
                    recordStatsPerPriority.sentCountFromCurrentSession += recordsSentCount;
                }
                else {
                    recordStatsPerPriority.sentCountFromPreviousSession += recordsSentCount;
                }
            }
        }

        //per tenant stats
        std::string stattenantToken = m_config.GetMetaStatsTenantToken();
        for (const auto& entry : recordIdsAndTenantids)
        {
            std::string tenantToken = entry.second;

            if (m_telemetryTenantStats.end() == m_telemetryTenantStats.find(tenantToken))
            {
                continue;
            }
            TelemetryStats& telemetryStats = m_telemetryTenantStats[tenantToken];
            LatencyStats& logToSuccessfulSendLatency = telemetryStats.logToSuccessfulSendLatencyPerLatency[eventLatency];
            for (unsigned latencyMs : latencyToSendMs) {
                updateMap(logToSuccessfulSendLatency.latencyDistribution, latencyMs);
                logToSuccessfulSendLatency.maxOfLatencyInMilliSecs = std::max(logToSuccessfulSendLatency.maxOfLatencyInMilliSecs, latencyMs);
                logToSuccessfulSendLatency.minOfLatencyInMilliSecs = std::min(logToSuccessfulSendLatency.minOfLatencyInMilliSecs, latencyMs);
            }

            RecordStats& recordStats = telemetryStats.recordStats;
            recordStats.sentCount += 1;

            //TODO: fix it after ongoing stats implemented or discarded
            if (1) {
                recordStats.sentCountFromCurrentSession += 1;
            }
            else {
                recordStats.sentCountFromPreviousSession += 1;
            }

            //update per-priority record stats
            if (eventLatency >= 0) {
                RecordStats& recordStatsPerPriority = telemetryStats.recordStatsPerLatency[eventLatency];
                recordStatsPerPriority.sentCount += 1;
                //TODO: fix it after ongoing stats implemented or discarded
                if (1) {
                    recordStatsPerPriority.sentCountFromCurrentSession += 1;
                }
                else {
                    recordStatsPerPriority.sentCountFromPreviousSession += 1;
                }
            }
        }
    }
    
    /// <summary>
    /// Update stats on package failure.
    /// </summary>
    /// <param name="statusCode">The status code.</param>
    void MetaStats::updateOnPackageFailed(int statusCode)
    {
        PackageStats& packageStats = m_telemetryStats.packageStats;
        packageStats.totalPkgsAcked++;
        packageStats.dropPkgsAcked++;
        packageStats.dropPkgsPerHttpReturnCode[statusCode]++;
    }
    
    /// <summary>
    /// Update stats on package retry.
    /// </summary>
    /// <param name="statusCode">The status code.</param>
    /// <param name="retryFailedTimes">The retry failed times.</param>
    void MetaStats::updateOnPackageRetry(int statusCode, unsigned retryFailedTimes)
    {
        PackageStats& packageStats = m_telemetryStats.packageStats;
        packageStats.totalPkgsAcked++;
        packageStats.retryPkgsAcked++;
        packageStats.retryPkgsPerHttpReturnCode[statusCode]++;

        m_telemetryStats.retriesCountDistribution[retryFailedTimes]++;
    }
    
    /// <summary>
    /// Update stats on records dropped.
    /// </summary>
    /// <param name="reason">The reason.</param>
    /// <param name="droppedCount">The dropped count.</param>
    void MetaStats::updateOnRecordsDropped(EventDroppedReason reason, std::map<std::string, size_t> const& droppedCount)
    {
        int overallCount = 0;
        for (const auto& dropcouttenant : droppedCount)
        {
            auto& temp = m_telemetryTenantStats[dropcouttenant.first];
            temp.recordStats.droppedCountReasonDistribution[reason] += static_cast<unsigned int>(dropcouttenant.second);
            temp.recordStats.droppedCount += static_cast<unsigned int>(dropcouttenant.second);
            overallCount += static_cast<unsigned int>(dropcouttenant.second);
        }
        m_telemetryStats.recordStats.droppedCountReasonDistribution[reason] += overallCount;
        m_telemetryStats.recordStats.droppedCount += overallCount;
    }
    
    /// <summary>
    /// Update stats on records storage overflow.
    /// </summary>
    /// <param name="overflownCount">The overflown count.</param>
    void MetaStats::updateOnRecordsOverFlown(std::map<std::string, size_t> const& overflownCount)
    {
        int overallCount = 0;
        for (const auto& overflowntenant : overflownCount)
        {
            auto& temp = m_telemetryTenantStats[overflowntenant.first];
            temp.recordStats.overflownCount += static_cast<unsigned int>(overflowntenant.second);
            overallCount += static_cast<unsigned int>(overflowntenant.second);
        }
        m_telemetryStats.recordStats.overflownCount += overallCount;
    }
    
    /// <summary>
    /// Update stats on records rejected.
    /// </summary>
    /// <param name="reason">The reason.</param>
    /// <param name="rejectedCount">The rejected count.</param>
    void MetaStats::updateOnRecordsRejected(EventRejectedReason reason, std::map<std::string, size_t> const& rejectedCount)
    {
        int overallCount = 0;
        for (const auto& rejecttenant : rejectedCount)
        {
            TelemetryStats& temp = m_telemetryTenantStats[rejecttenant.first];
            temp.recordStats.rejectedCountReasonDistribution[reason] += static_cast<unsigned int>(rejecttenant.second);
            temp.recordStats.rejectedCount += static_cast<unsigned int>(rejecttenant.second);
            overallCount += static_cast<unsigned int>(rejecttenant.second);
        }
        m_telemetryStats.recordStats.rejectedCountReasonDistribution[reason] += overallCount;
    }
    
    /// <summary>
    /// Update on storage open.
    /// </summary>
    /// <param name="type">The type.</param>
    void MetaStats::updateOnStorageOpened(std::string const& type)
    {
        m_telemetryStats.offlineStorageStats.storageFormat = type;
    }
    
    /// <summary>
    /// Update on storage open failed.
    /// </summary>
    /// <param name="reason">The reason.</param>
    void MetaStats::updateOnStorageFailed(std::string const& reason)
    {
        m_telemetryStats.offlineStorageStats.lastFailureReason = reason;
    }

    ARIASDK_LOG_INST_COMPONENT_CLASS(RecordStats, "EventsSDK.RecordStats", "RecordStats");

} ARIASDK_NS_END
