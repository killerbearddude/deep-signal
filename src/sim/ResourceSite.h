#pragma once

// A registered working location is separate from a colony. Only earned,
// commissioned groups contribute capabilities; these stocks are real inventory.
#include "sim/Domain.h"
#include "sim/SitePackageRules.h"
#include <optional>
#include <string>
#include <vector>

namespace deep {
struct InstalledSiteModule {
    SiteDevelopmentProgramId programId;
    int packageRow = 0;
    int catalogVersion = kSiteModuleCatalogVersion;
    SiteModuleKind kind = SiteModuleKind::IceExtraction;
    int quantity = 1;
    std::int64_t commissionedDay = 0;
    bool operator==(const InstalledSiteModule&) const = default;
};
// Standing authority survives builder closeout. Floors and lifetime allowance
// protect future duty only; lowering them never erases recorded expenditure.
struct SiteOperatingPolicy {
    std::optional<PersonId> leaderId;
    bool enabled = true;
    double requestedIcePerDay = 10;
    double reactorFuelFloor = 0;
    double compositesFloor = 0;
    std::optional<double> lifetimeDutyAllowance;
    bool operator==(const SiteOperatingPolicy&) const = default;
};
enum class SiteOperatingIssueCause { None, ZeroRecovery, SupplyLost, DutyAllowance };
struct SiteOperatingIssue {
    SiteOperatingIssueCause cause = SiteOperatingIssueCause::None;
    // Observed episode identity, not a fluctuating amount or hidden geology key.
    std::int64_t episodeStartedDay = 0;
    std::string message;
    bool acknowledged = true;
    bool operator==(const SiteOperatingIssue&) const = default;
};
// Positive supported time is charged at opening, regardless of later yield.
// The group prefix pins the historical hardware and costs through expansions.
struct SiteDutyReceipt {
    std::int64_t day = 0;
    int operatingRevision = 1;
    PersonId leaderId;
    std::size_t installedGroupCutoff = 0;
    double duty = 0;
    double reactorFuel = 0;
    double composites = 0;
    double rawHandling = 0;
    SiteOperatingPolicy policy;
    bool operator==(const SiteDutyReceipt&) const = default;
};
// Only actual positive-nominal attempts generate these records. Zero recovery
// is an observed result, never a reserve/absence/accessibility measurement.
struct SiteExtractionReceipt {
    std::int64_t day = 0;
    int operatingRevision = 1;
    PersonId leaderId;
    std::size_t installedGroupCutoff = 0;
    double nominalAttempt = 0;
    double recoveredIce = 0;
    double freeRawRoom = 0;
    double availableHandling = 0;
    std::string observation;
    bool operator==(const SiteExtractionReceipt&) const = default;
};
struct SiteOperatingReport {
    std::int64_t startDay = 0;
    std::int64_t endDay = 0;
    bool isNinetyDayReview = false;
    int operatingRevision = 1;
    SiteOperatingPolicy policy;
    std::size_t installedGroupCutoff = 0;
    double supportedDuty = 0;
    double reactorFuel = 0;
    double composites = 0;
    int attempts = 0;
    double recoveredIce = 0;
    double rawOccupancy = 0;
    double rawCapacity = 0;
    // Period net loads minus source returns; may reverse an earlier period.
    double exportedIce = 0;
    double deliveredIce = 0;
    std::string waitingReason;
    std::int64_t auditThroughId = 0;
};
struct ResourceSite {
    SiteId id;
    BodyId bodyId;
    std::string name;
    std::int64_t createdDay = 0;
    std::optional<InstitutionId> institutionId;
    // Protective packaging permits ship-handled processed staging at a cold
    // site. Raw stock instead requires finite commissioned passive storage.
    ProcessedMaterialSet processedStock;
    MineralSet rawStock;
    std::vector<InstalledSiteModule> installed;
    SiteOperatingPolicy operatingPolicy;
    int operatingRevision = 1;
    std::vector<SiteDutyReceipt> dutyReceipts;
    std::vector<SiteExtractionReceipt> extractionReceipts;
    std::vector<SiteOperatingReport> reports;
    std::int64_t reportStartDay = 0;
    std::int64_t nextReportDay = 30;
    SiteOperatingIssue issue;
};
} // namespace deep
